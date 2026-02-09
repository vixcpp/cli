/**
 *
 *  @file P2PCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/P2PCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/util/Console.hpp>
#include <vix/cli/util/Ui.hpp>

#include <vix/utils/Logger.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <vix/p2p/Bootstrap.hpp>
#include <vix/p2p/Node.hpp>
#include <vix/p2p/P2P.hpp>
#include <vix/p2p/Peer.hpp>

#if defined(_WIN32)
#include <io.h>
#define VIX_ISATTY _isatty
#define VIX_FILENO _fileno
#else
#include <unistd.h>
#define VIX_ISATTY isatty
#define VIX_FILENO fileno
#endif

namespace
{
  namespace style = vix::cli::style;
  namespace cli_console = vix::cli::util;

  using Logger = vix::utils::Logger;

  struct SharedLifecycle
  {
    std::atomic<bool> running{true};
    std::atomic<bool> stopping{false};
  };

  static std::shared_ptr<SharedLifecycle> g_life;

  static void on_sigint(int)
  {
    if (g_life)
      g_life->running.store(false);
  }

  static bool is_tty_stdout()
  {
    return VIX_ISATTY(VIX_FILENO(stdout)) != 0;
  }

  static bool has_flag(const std::vector<std::string> &a, const std::string &k)
  {
    for (const auto &x : a)
      if (x == k)
        return true;
    return false;
  }

  static std::optional<std::string> arg_value(const std::vector<std::string> &a, const std::string &k)
  {
    for (std::size_t i = 0; i + 1 < a.size(); ++i)
      if (a[i] == k)
        return a[i + 1];

    const std::string prefix = k + "=";
    for (const auto &x : a)
      if (x.rfind(prefix, 0) == 0)
        return x.substr(prefix.size());

    return std::nullopt;
  }

  static std::string to_lower_copy(std::string s)
  {
    for (auto &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  static std::optional<Logger::Level> parse_log_level(std::string raw)
  {
    const std::string s = to_lower_copy(std::move(raw));
    if (s == "trace")
      return Logger::Level::Trace;
    if (s == "debug")
      return Logger::Level::Debug;
    if (s == "info")
      return Logger::Level::Info;
    if (s == "warn" || s == "warning")
      return Logger::Level::Warn;
    if (s == "error" || s == "err")
      return Logger::Level::Error;
    if (s == "critical" || s == "fatal")
      return Logger::Level::Critical;
    if (s == "off" || s == "none")
      return Logger::Level::Off;
    return std::nullopt;
  }

  static void apply_log_level_from_flag(Logger &logger, const std::string &value, bool quiet)
  {
    if (auto lvl = parse_log_level(value))
    {
      logger.setLevel(*lvl);
      if (!quiet)
        style::hint(std::string("log_level=") + value);
      return;
    }

    style::error(std::string("invalid --log-level: ") + value);
    style::hint("expected: trace, debug, info, warn, error, critical, off");
  }

  static std::optional<std::uint64_t> parse_u64(const std::string &s)
  {
    try
    {
      std::size_t idx = 0;
      unsigned long long v = std::stoull(s, &idx, 10);
      if (idx != s.size())
        return std::nullopt;
      return static_cast<std::uint64_t>(v);
    }
    catch (...)
    {
      return std::nullopt;
    }
  }

  static std::optional<std::uint16_t> parse_u16(const std::string &s)
  {
    auto v = parse_u64(s);
    if (!v || *v > 65535ULL)
      return std::nullopt;
    return static_cast<std::uint16_t>(*v);
  }

  static std::optional<vix::p2p::PeerEndpoint> parse_endpoint(const std::string &s)
  {
    std::string x = s;
    constexpr const char *kPrefix = "tcp://";
    if (x.rfind(kPrefix, 0) == 0)
      x = x.substr(std::string(kPrefix).size());

    const auto pos = x.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= x.size())
      return std::nullopt;

    vix::p2p::PeerEndpoint ep;
    ep.host = x.substr(0, pos);

    auto p = parse_u16(x.substr(pos + 1));
    if (!p)
      return std::nullopt;

    ep.port = *p;
    ep.scheme = "tcp";
    return ep;
  }

  enum class ConnectReason
  {
    Manual,
    Discovery,
    Bootstrap
  };

  struct ConnectStats
  {
    std::uint64_t connect_attempts = 0;
    std::uint64_t connect_deduped = 0;
    std::uint64_t connect_failures = 0;
    std::uint64_t backoff_skips = 0;
  };

  struct ConnectEntry
  {
    std::uint32_t failures = 0;
    std::chrono::steady_clock::time_point backoff_until{};
    std::chrono::steady_clock::time_point last_attempt{};
  };

  class ConnectGuard
  {
  public:
    explicit ConnectGuard(std::shared_ptr<SharedLifecycle> life)
        : life_(std::move(life))
    {
    }

    static std::string key_for(const vix::p2p::PeerEndpoint &ep)
    {
      std::string scheme = ep.scheme.empty() ? "tcp" : to_lower_copy(ep.scheme);
      std::ostringstream oss;
      oss << scheme << "://" << ep.host << ":" << ep.port;
      return oss.str();
    }

    bool allow_attempt(const vix::p2p::PeerEndpoint &ep, bool manual, bool quiet)
    {
      if (!life_ || life_->stopping.load())
        return false;

      const auto now = std::chrono::steady_clock::now();
      const std::string key = key_for(ep);

      std::lock_guard<std::mutex> lock(mu_);
      auto &e = table_[key];

      if (e.backoff_until.time_since_epoch().count() != 0 && now < e.backoff_until)
      {
        ++stats_.backoff_skips;
        return false;
      }

      constexpr auto kMinAttemptGap = std::chrono::milliseconds(900);
      if (!manual && e.last_attempt.time_since_epoch().count() != 0 && (now - e.last_attempt) < kMinAttemptGap)
      {
        ++stats_.connect_deduped;
        return false;
      }

      e.last_attempt = now;
      ++stats_.connect_attempts;

      (void)quiet;
      return true;
    }

    void mark_failure(const vix::p2p::PeerEndpoint &ep, bool manual)
    {
      const auto now = std::chrono::steady_clock::now();
      const std::string key = key_for(ep);

      std::lock_guard<std::mutex> lock(mu_);
      auto &e = table_[key];

      ++stats_.connect_failures;

      if (manual)
      {
        e.failures = std::min<std::uint32_t>(e.failures + 1, 8u);
        const std::uint64_t backoff_ms = std::min<std::uint64_t>(2500ULL * (1ULL << std::min<std::uint32_t>(e.failures - 1, 5u)), 12000ULL);
        e.backoff_until = now + std::chrono::milliseconds(backoff_ms);
        return;
      }

      e.failures = std::min<std::uint32_t>(e.failures + 1, 10u);
      const std::uint64_t backoff_ms = std::min<std::uint64_t>(2000ULL * (1ULL << std::min<std::uint32_t>(e.failures - 1, 6u)), 15000ULL);
      e.backoff_until = now + std::chrono::milliseconds(backoff_ms);
    }

    void mark_success(const vix::p2p::PeerEndpoint &ep)
    {
      const std::string key = key_for(ep);
      std::lock_guard<std::mutex> lock(mu_);
      auto it = table_.find(key);
      if (it == table_.end())
        return;
      it->second.failures = 0;
      it->second.backoff_until = {};
    }

    ConnectStats stats() const
    {
      std::lock_guard<std::mutex> lock(mu_);
      return stats_;
    }

    std::size_t tracked_endpoints() const
    {
      std::lock_guard<std::mutex> lock(mu_);
      return table_.size();
    }

  private:
    std::shared_ptr<SharedLifecycle> life_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, ConnectEntry> table_;
    ConnectStats stats_{};
  };

  static std::string build_stats_plain(const vix::p2p::NodeStats &st, const ConnectStats &cs, std::size_t tracked)
  {
    std::ostringstream oss;
    oss
        << "peers_total=" << st.peers_total
        << " peers_connected=" << st.peers_connected
        << " handshakes_started=" << st.handshakes_started
        << " handshakes_completed=" << st.handshakes_completed
        << " connect_attempts=" << cs.connect_attempts
        << " connect_deduped=" << cs.connect_deduped
        << " connect_failures=" << cs.connect_failures
        << " backoff_skips=" << cs.backoff_skips
        << " tracked_endpoints=" << tracked;
    return oss.str();
  }

  static void print_stats(bool tui, const std::string &line)
  {
    if (tui)
    {
      std::cout << "\r" << style::GRAY << "[vix p2p] " << style::RESET << line << "    " << std::flush;
    }
    else
    {
      std::cout << style::GRAY << "[vix p2p] " << style::RESET << line << "\n"
                << std::flush;
    }
  }

  static void usage(std::ostream &out)
  {
    out
        << "Usage:\n"
        << "  vix p2p --id <node_id> --listen <port> [options]\n"
        << "\n"
        << "Core:\n"
        << "  --id <node_id>                 Node id (string)\n"
        << "  --listen <port>                TCP listen port\n"
        << "  --connect <host:port>          Connect to a peer after start\n"
        << "  --connect-delay <ms>           Delay before connect()\n"
        << "  --run <seconds>                Auto-stop after N seconds\n"
        << "  --stats-every <ms>             Stats interval (default: 1000)\n"
        << "  --tui <on|off>                 Single-line live stats (default: auto on TTY)\n"
        << "  --quiet                        Print only final stats\n"
        << "  --no-connect                   Disable auto connect (discovery/bootstrap callbacks)\n"
        << "\n"
        << "Discovery (UDP):\n"
        << "  --discovery <on|off>           Default: on\n"
        << "  --disc-port <port>             Default: 37020\n"
        << "  --disc-mode <broadcast|multicast> Default: broadcast\n"
        << "  --disc-interval <ms>           Default: 2000\n"
        << "\n"
        << "Bootstrap (HTTP registry):\n"
        << "  --bootstrap <on|off>           Default: off\n"
        << "  --registry <url>               Default: http://127.0.0.1:8080/p2p/v1\n"
        << "  --boot-interval <seconds>      Default: 15\n"
        << "  --announce <on|off>            Default: on\n"
        << "\n"
        << "Logging:\n"
        << "  --log-level <level>            trace|debug|info|warn|error|critical|off\n"
        << "\n"
        << "Help:\n"
        << "  --help, -h\n"
        << "\n"
        << "Examples:\n"
        << "  vix p2p --id A --listen 9001\n"
        << "  vix p2p --id B --listen 9002 --connect 127.0.0.1:9001\n";
  }

  static bool parse_on_off(const std::string &s, bool &out)
  {
    if (s == "on")
    {
      out = true;
      return true;
    }
    if (s == "off")
    {
      out = false;
      return true;
    }
    return false;
  }

  static bool do_connect(
      const std::shared_ptr<SharedLifecycle> &life,
      ConnectGuard &guard,
      vix::p2p::P2PRuntime &p2p,
      const vix::p2p::PeerEndpoint &ep,
      bool manual,
      bool quiet)
  {
    if (!life || life->stopping.load())
      return false;

    if (!guard.allow_attempt(ep, manual, quiet))
      return false;

    try
    {
      p2p.connect(ep);
      guard.mark_success(ep);
      return true;
    }
    catch (...)
    {
      guard.mark_failure(ep, manual);
      return false;
    }
  }

} // namespace

namespace vix::commands::P2PCommand
{
  int run(const std::vector<std::string> &argsIn)
  {
    g_life = std::make_shared<SharedLifecycle>();
    std::signal(SIGINT, on_sigint);

    std::ios::sync_with_stdio(false);

    auto &logger = Logger::getInstance();
    std::vector<std::string> args = argsIn;

    if (args.empty() || has_flag(args, "--help") || has_flag(args, "-h"))
      return help();

    const bool quiet = has_flag(args, "--quiet");
    const bool no_connect = has_flag(args, "--no-connect");

    if (auto s = arg_value(args, "--log-level"))
      apply_log_level_from_flag(logger, *s, quiet);

    const auto id_opt = arg_value(args, "--id");
    const auto listen_opt = arg_value(args, "--listen");
    const auto connect_opt = arg_value(args, "--connect");

    std::uint64_t connect_delay_ms = 0;
    if (auto s = arg_value(args, "--connect-delay"))
    {
      auto v = parse_u64(*s);
      if (!v)
      {
        style::error("invalid --connect-delay (ms)");
        return 1;
      }
      connect_delay_ms = *v;
    }

    std::uint64_t run_seconds = 0;
    if (auto s = arg_value(args, "--run"))
    {
      auto v = parse_u64(*s);
      if (!v)
      {
        style::error("invalid --run (seconds)");
        return 1;
      }
      run_seconds = *v;
    }

    std::uint64_t stats_every_ms = 1000;
    if (auto s = arg_value(args, "--stats-every"))
    {
      auto v = parse_u64(*s);
      if (!v || *v == 0)
      {
        style::error("invalid --stats-every (ms)");
        return 1;
      }
      stats_every_ms = *v;
    }

    bool tui = is_tty_stdout();
    if (auto s = arg_value(args, "--tui"))
    {
      if (!parse_on_off(*s, tui))
      {
        style::error("invalid --tui (expected on|off)");
        return 1;
      }
    }

    if (!id_opt || !listen_opt)
    {
      style::error("missing required options: --id and/or --listen");
      style::hint("try: vix p2p --help");
      return 1;
    }

    const auto listen_port_opt = parse_u16(*listen_opt);
    if (!listen_port_opt)
    {
      style::error("invalid --listen port");
      return 1;
    }

    bool discovery_on = true;
    if (auto s = arg_value(args, "--discovery"))
    {
      if (!parse_on_off(*s, discovery_on))
      {
        style::error("invalid --discovery (expected on|off)");
        return 1;
      }
    }

    std::uint16_t disc_port = 37020;
    if (auto s = arg_value(args, "--disc-port"))
    {
      auto v = parse_u16(*s);
      if (!v)
      {
        style::error("invalid --disc-port");
        return 1;
      }
      disc_port = *v;
    }

    vix::p2p::DiscoveryMode disc_mode = vix::p2p::DiscoveryMode::Broadcast;
    if (auto s = arg_value(args, "--disc-mode"))
    {
      if (*s == "broadcast")
        disc_mode = vix::p2p::DiscoveryMode::Broadcast;
      else if (*s == "multicast")
        disc_mode = vix::p2p::DiscoveryMode::Multicast;
      else
      {
        style::error("invalid --disc-mode (expected broadcast|multicast)");
        return 1;
      }
    }

    std::uint32_t disc_interval_ms = 2000;
    if (auto s = arg_value(args, "--disc-interval"))
    {
      auto v = parse_u64(*s);
      if (!v || *v == 0 || *v > 0xFFFFFFFFULL)
      {
        style::error("invalid --disc-interval (ms)");
        return 1;
      }
      disc_interval_ms = static_cast<std::uint32_t>(*v);
    }

    bool bootstrap_on = false;
    if (auto s = arg_value(args, "--bootstrap"))
    {
      if (!parse_on_off(*s, bootstrap_on))
      {
        style::error("invalid --bootstrap (expected on|off)");
        return 1;
      }
    }

    std::string registry = "http://127.0.0.1:8080/p2p/v1";
    if (auto s = arg_value(args, "--registry"))
      registry = *s;

    std::uint64_t boot_interval_sec = 15;
    if (auto s = arg_value(args, "--boot-interval"))
    {
      auto v = parse_u64(*s);
      if (!v || *v == 0)
      {
        style::error("invalid --boot-interval (seconds)");
        return 1;
      }
      boot_interval_sec = *v;
    }

    bool announce_on = true;
    if (auto s = arg_value(args, "--announce"))
    {
      if (!parse_on_off(*s, announce_on))
      {
        style::error("invalid --announce (expected on|off)");
        return 1;
      }
    }

    vix::p2p::NodeConfig cfg;
    cfg.node_id = *id_opt;
    cfg.listen_port = *listen_port_opt;

    auto node = vix::p2p::make_tcp_node(cfg);
    std::weak_ptr<vix::p2p::Node> wnode = node;

    vix::p2p::P2PRuntime p2p(node);

    auto life = g_life;
    ConnectGuard guard(life);

    if (!quiet)
    {
      style::info("P2P starting");
      cli_console::status_line(false, "node", cfg.node_id);
      cli_console::status_line(false, "listen", std::to_string(cfg.listen_port));
      if (tui)
        style::hint("tui=on (single-line stats)");
    }

    if (bootstrap_on)
    {
      vix::p2p::BootstrapConfig bc;
      bc.self_node_id = cfg.node_id;
      bc.self_tcp_port = cfg.listen_port;
      bc.registry_url = registry;
      bc.poll_interval_ms = static_cast<std::uint32_t>(boot_interval_sec * 1000ULL);
      bc.mode = announce_on ? vix::p2p::BootstrapMode::PullAndAnnounce
                            : vix::p2p::BootstrapMode::PullOnly;

      auto boot = vix::p2p::make_http_bootstrap(
          bc,
          [life, &guard, &p2p, wnode, no_connect](const vix::p2p::BootstrapPeer &p)
          {
            if (no_connect)
              return;
            if (!life || life->stopping.load())
              return;

            auto n = wnode.lock();
            if (!n)
              return;

            vix::p2p::PeerEndpoint ep;
            ep.host = p.host;
            ep.port = p.tcp_port;
            ep.scheme = "tcp";

            (void)do_connect(life, guard, p2p, ep, /*manual=*/false, /*quiet=*/true);
          });

      node->set_bootstrap(boot);

      if (!quiet)
      {
        style::hint(std::string("bootstrap=on registry=") + registry);
        style::hint(std::string("announce=") + (announce_on ? "on" : "off"));
      }
    }

    if (discovery_on)
    {
      vix::p2p::DiscoveryConfig dc;
      dc.self_node_id = cfg.node_id;
      dc.self_tcp_port = cfg.listen_port;
      dc.discovery_port = disc_port;
      dc.mode = disc_mode;
      dc.announce_interval_ms = disc_interval_ms;

      auto disc = vix::p2p::make_udp_discovery(
          dc,
          [life, &guard, &p2p, wnode, no_connect](const vix::p2p::DiscoveryAnnouncement &a)
          {
            if (no_connect)
              return;
            if (!life || life->stopping.load())
              return;

            auto n = wnode.lock();
            if (!n)
              return;

            vix::p2p::PeerEndpoint ep;
            ep.host = a.host;
            ep.port = a.port;
            ep.scheme = "tcp";

            (void)do_connect(life, guard, p2p, ep, /*manual=*/false, /*quiet=*/true);
          });

      node->set_discovery(disc);

      if (!quiet)
      {
        style::hint(std::string("discovery=on disc_port=") + std::to_string(disc_port));
        style::hint(std::string("disc_mode=") + (disc_mode == vix::p2p::DiscoveryMode::Broadcast ? "broadcast" : "multicast"));
      }
    }

    p2p.start();

    if (connect_opt)
    {
      auto ep = parse_endpoint(*connect_opt);
      if (!ep)
      {
        style::error("invalid --connect (expected host:port or tcp://host:port)");
        life->stopping.store(true);
        life->running.store(false);
        p2p.stop();
        return 1;
      }

      if (!quiet)
        style::hint(std::string("connect=") + ep->host + ":" + std::to_string(ep->port));

      if (connect_delay_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(connect_delay_ms));

      (void)do_connect(life, guard, p2p, *ep, /*manual=*/true, quiet);
    }

    std::thread stopper;
    if (run_seconds > 0)
    {
      stopper = std::thread([life, run_seconds]()
                            {
                              std::this_thread::sleep_for(std::chrono::seconds(run_seconds));
                              if (life)
                                life->running.store(false); });
    }

    const auto tick = std::chrono::milliseconds(stats_every_ms);

    vix::p2p::NodeStats last{};
    std::chrono::steady_clock::time_point last_print{};
    while (life->running.load())
    {
      const auto now = std::chrono::steady_clock::now();
      const auto st = p2p.stats();
      const auto cs = guard.stats();
      const auto tracked = guard.tracked_endpoints();

      const bool changed =
          (st.peers_total != last.peers_total) ||
          (st.peers_connected != last.peers_connected) ||
          (st.handshakes_started != last.handshakes_started) ||
          (st.handshakes_completed != last.handshakes_completed);

      const bool time_ok =
          (last_print.time_since_epoch().count() == 0) ||
          (now - last_print) >= tick;

      if (!quiet && (changed || time_ok))
      {
        print_stats(tui, build_stats_plain(st, cs, tracked));
        last = st;
        last_print = now;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    if (stopper.joinable())
      stopper.join();

    if (!quiet)
    {
      if (tui)
        std::cout << "\n"
                  << std::flush;
      style::hint("stopping...");
    }

    life->stopping.store(true);

    node->set_discovery(nullptr);
    node->set_bootstrap(nullptr);

    p2p.stop();

    const auto final_st = p2p.stats();
    const auto final_cs = guard.stats();

    if (!quiet)
    {
      style::blank();
      vix::cli::util::section(std::cout, "Final");
      vix::cli::util::kv(std::cout, "peers_total", std::to_string(final_st.peers_total), 22);
      vix::cli::util::kv(std::cout, "peers_connected", std::to_string(final_st.peers_connected), 22);
      vix::cli::util::kv(std::cout, "handshakes_started", std::to_string(final_st.handshakes_started), 22);
      vix::cli::util::kv(std::cout, "handshakes_completed", std::to_string(final_st.handshakes_completed), 22);

      vix::cli::util::kv(std::cout, "connect_attempts", std::to_string(final_cs.connect_attempts), 22);
      vix::cli::util::kv(std::cout, "connect_deduped", std::to_string(final_cs.connect_deduped), 22);
      vix::cli::util::kv(std::cout, "connect_failures", std::to_string(final_cs.connect_failures), 22);
      vix::cli::util::kv(std::cout, "backoff_skips", std::to_string(final_cs.backoff_skips), 22);
      vix::cli::util::kv(std::cout, "tracked_endpoints", std::to_string(guard.tracked_endpoints()), 22);

      style::success("bye");
    }
    else
    {
      std::cout << build_stats_plain(final_st, final_cs, guard.tracked_endpoints()) << "\n"
                << std::flush;
    }

    return 0;
  }

  int help()
  {
    usage(std::cout);
    return 0;
  }

} // namespace vix::commands::P2PCommand
