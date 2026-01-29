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
#include <vix/cli/Utils.hpp>
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

using namespace vix::cli::style;

namespace
{
  using Logger = vix::utils::Logger;

  // P0: shared lifecycle state (running/stopping)
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
      return Logger::Level::TRACE;
    if (s == "debug")
      return Logger::Level::DEBUG;
    if (s == "info")
      return Logger::Level::INFO;
    if (s == "warn" || s == "warning")
      return Logger::Level::WARN;
    if (s == "error" || s == "err")
      return Logger::Level::ERROR;
    if (s == "critical" || s == "fatal")
      return Logger::Level::CRITICAL;
    if (s == "off" || s == "none")
      return Logger::Level::OFF;
    return std::nullopt;
  }

  static void apply_log_level_from_flag(Logger &logger, const std::string &value)
  {
    if (auto lvl = parse_log_level(value))
      logger.setLevel(*lvl);
    else
      std::cerr << "vix: invalid --log-level value '" << value
                << "'. Expected one of: trace, debug, info, warn, error, critical, off.\n";
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
    // host:port OR tcp://host:port
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

  // P1: anti double-connect + backoff (CLI-side guard)
  //
  // NOTE:
  //   The ideal place is Node::connect() (core), but this guard already
  //   eliminates spam from discovery/bootstrap/manual in the CLI.
  enum class ConnectReason
  {
    Manual,
    Discovery,
    Bootstrap
  };

  static const char *reason_name(ConnectReason r)
  {
    switch (r)
    {
    case ConnectReason::Manual:
      return "manual";
    case ConnectReason::Discovery:
      return "discovery";
    case ConnectReason::Bootstrap:
      return "bootstrap";
    default:
      return "unknown";
    }
  }

  struct ConnectStats
  {
    std::uint64_t connect_attempts = 0;
    std::uint64_t connect_deduped = 0;
    std::uint64_t connect_failures = 0; // approximated (attempt -> backoff)
    std::uint64_t backoff_skips = 0;
  };

  struct ConnectEntry
  {
    // Exponential backoff state
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
      std::string host = ep.host;
      // Keep host as-is (could be ip/hostname). If you want stronger normalize,
      // do it in core Node (DNS/case rules).
      std::ostringstream oss;
      oss << scheme << "://" << host << ":" << ep.port;
      return oss.str();
    }

    bool should_connect(const vix::p2p::PeerEndpoint &ep, ConnectReason reason, bool quiet)
    {
      if (!life_ || life_->stopping.load())
      {
        // P0.1: after stop, callbacks do nothing
        return false;
      }

      const auto now = std::chrono::steady_clock::now();
      const std::string key = key_for(ep);

      std::lock_guard<std::mutex> lock(mu_);
      auto &e = table_[key];

      // Backoff gate
      if (e.backoff_until.time_since_epoch().count() != 0 &&
          now < e.backoff_until)
      {
        ++stats_.backoff_skips;
        return false;
      }

      // Dedup gate: do not attempt too frequently even without explicit backoff.
      // This is a "connecting window" that prevents discovery from hammering.
      constexpr auto kMinAttemptGap = std::chrono::milliseconds(800);
      if (e.last_attempt.time_since_epoch().count() != 0 &&
          (now - e.last_attempt) < kMinAttemptGap)
      {
        ++stats_.connect_deduped;
        return false;
      }

      e.last_attempt = now;

      // Record attempt
      ++stats_.connect_attempts;

      // P1.4: exponential backoff (approx. failure until proven otherwise)
      // This prevents spam even when connect fails quickly.
      // In core, you'd reset failures on successful connect events.
      if (reason != ConnectReason::Manual) // keep manual responsive
      {
        e.failures = std::min<std::uint32_t>(e.failures + 1, 10u);

        const std::uint64_t base_ms = 2000;
        std::uint64_t backoff_ms = base_ms * (1ULL << std::min<std::uint32_t>(e.failures - 1, 6u)); // cap exponent
        if (backoff_ms > 15000)
          backoff_ms = 15000;

        e.backoff_until = now + std::chrono::milliseconds(backoff_ms);
        ++stats_.connect_failures; // approximated
      }

      (void)quiet;
      return true;
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

  // P2: printing stats (extended)
  static void print_stats_line(const vix::p2p::NodeStats &st,
                               const ConnectStats &cs,
                               std::size_t tracked)
  {
    std::cout
        << "peers_total=" << st.peers_total
        << " peers_connected=" << st.peers_connected
        << " handshakes_started=" << st.handshakes_started
        << " handshakes_completed=" << st.handshakes_completed
        << " connect_attempts=" << cs.connect_attempts
        << " connect_deduped=" << cs.connect_deduped
        << " connect_failures=" << cs.connect_failures
        << " backoff_skips=" << cs.backoff_skips
        << " tracked_endpoints=" << tracked
        << "\n";
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
        << "  --stats-every <ms>             Stats print interval (default: 1000)\n"
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
        << "Security (placeholders):\n"
        << "  --tls                          (reserved) enable TLS transport\n"
        << "  --psk <value>                  (reserved) pre-shared key\n"
        << "  --handshake <mode>             (reserved) handshake mode\n"
        << "\n"
        << "Help:\n"
        << "  --help, -h\n"
        << "\n"
        << "Examples:\n"
        << "  # Terminal A\n"
        << "  vix p2p --id A --listen 9001\n"
        << "\n"
        << "  # Terminal B\n"
        << "  vix p2p --id B --listen 9002 --connect 127.0.0.1:9001\n";
  }

} // namespace

namespace vix::commands::P2PCommand
{
  int run(const std::vector<std::string> &argsIn)
  {
    // Return codes:
    // 0 success
    // 2 cancelled
    // 1+ errors

    g_life = std::make_shared<SharedLifecycle>();
    std::signal(SIGINT, on_sigint);

    auto &logger = Logger::getInstance();

    std::vector<std::string> args = argsIn;

    if (args.empty() || has_flag(args, "--help") || has_flag(args, "-h"))
      return help();

    // P2.2: per-command --log-level
    if (auto s = arg_value(args, "--log-level"))
      apply_log_level_from_flag(logger, *s);

    // P2.3: placeholders (no implementation yet)
    const bool tls_flag = has_flag(args, "--tls");
    const auto psk_flag = arg_value(args, "--psk");
    const auto handshake_flag = arg_value(args, "--handshake");

    const auto id_opt = arg_value(args, "--id");
    const auto listen_opt = arg_value(args, "--listen");
    const auto connect_opt = arg_value(args, "--connect");

    const bool quiet = has_flag(args, "--quiet");
    const bool no_connect = has_flag(args, "--no-connect");

    std::uint64_t connect_delay_ms = 0;
    if (auto s = arg_value(args, "--connect-delay"))
    {
      auto v = parse_u64(*s);
      if (!v)
      {
        error("Invalid --connect-delay (ms).");
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
        error("Invalid --run (seconds).");
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
        error("Invalid --stats-every (ms).");
        return 1;
      }
      stats_every_ms = *v;
    }

    if (!id_opt || !listen_opt)
    {
      error("Missing required options: --id and/or --listen.");
      hint("Try: vix p2p --help");
      return 1;
    }

    const auto listen_port_opt = parse_u16(*listen_opt);
    if (!listen_port_opt)
    {
      error("Invalid --listen port.");
      return 1;
    }

    // Discovery
    bool discovery_on = true;
    if (auto s = arg_value(args, "--discovery"))
    {
      if (*s == "on")
        discovery_on = true;
      else if (*s == "off")
        discovery_on = false;
      else
      {
        error("Invalid --discovery. Expected on|off.");
        return 1;
      }
    }

    std::uint16_t disc_port = 37020;
    if (auto s = arg_value(args, "--disc-port"))
    {
      auto v = parse_u16(*s);
      if (!v)
      {
        error("Invalid --disc-port.");
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
        error("Invalid --disc-mode. Expected broadcast|multicast.");
        return 1;
      }
    }

    std::uint32_t disc_interval_ms = 2000;
    if (auto s = arg_value(args, "--disc-interval"))
    {
      auto v = parse_u64(*s);
      if (!v || *v == 0)
      {
        error("Invalid --disc-interval (ms).");
        return 1;
      }
      if (*v > 0xFFFFFFFFULL)
      {
        error("--disc-interval too large.");
        return 1;
      }
      disc_interval_ms = static_cast<std::uint32_t>(*v);
    }

    // Bootstrap
    bool bootstrap_on = false;
    if (auto s = arg_value(args, "--bootstrap"))
    {
      if (*s == "on")
        bootstrap_on = true;
      else if (*s == "off")
        bootstrap_on = false;
      else
      {
        error("Invalid --bootstrap. Expected on|off.");
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
        error("Invalid --boot-interval (seconds).");
        return 1;
      }
      boot_interval_sec = *v;
    }

    bool announce_on = true;
    if (auto s = arg_value(args, "--announce"))
    {
      if (*s == "on")
        announce_on = true;
      else if (*s == "off")
        announce_on = false;
      else
      {
        error("Invalid --announce. Expected on|off.");
        return 1;
      }
    }

    // Warn for placeholders (P2.3)
    if (!quiet && (tls_flag || psk_flag.has_value() || handshake_flag.has_value()))
    {
      hint("security flags detected (placeholders): TLS/PSK/handshake not implemented yet.");
    }

    // Build node/runtime
    vix::p2p::NodeConfig cfg;
    cfg.node_id = *id_opt;
    cfg.listen_port = *listen_port_opt;

    auto node = vix::p2p::make_tcp_node(cfg);
    std::weak_ptr<vix::p2p::Node> wnode = node; // P0.3: callbacks safe
    vix::p2p::P2PRuntime p2p(node);

    auto life = g_life;
    ConnectGuard guard(life);

    if (!quiet)
    {
      info("P2P starting");
      hint("node_id=" + cfg.node_id + " listen=" + std::to_string(cfg.listen_port));
    }

    // Bootstrap (HTTP registry)
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
          [life, &guard, wnode, no_connect](const vix::p2p::BootstrapPeer &p)
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

            if (!guard.should_connect(ep, ConnectReason::Bootstrap, /*quiet=*/true))
              return;

            n->connect(ep);
          });

      node->set_bootstrap(boot);

      if (!quiet)
        hint("bootstrap=on registry=" + registry);
    }

    // Discovery (UDP)
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
          [life, &guard, wnode, no_connect](const vix::p2p::DiscoveryAnnouncement &a)
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

            if (!guard.should_connect(ep, ConnectReason::Discovery, /*quiet=*/true))
              return;

            n->connect(ep);
          });

      node->set_discovery(disc);

      if (!quiet)
        hint("discovery=on disc_port=" + std::to_string(disc_port));
    }

    p2p.start();

    // Manual outbound connect (also uses guard)
    if (connect_opt)
    {
      auto ep = parse_endpoint(*connect_opt);
      if (!ep)
      {
        error("Invalid --connect. Expected host:port or tcp://host:port");
        // P0.2: we still stop cleanly
        if (life)
        {
          life->stopping.store(true);
          life->running.store(false);
        }
        p2p.stop();
        return 1;
      }

      if (!quiet)
        hint("connect=" + ep->host + ":" + std::to_string(ep->port) +
             " delay=" + std::to_string(connect_delay_ms) + "ms");

      if (connect_delay_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(connect_delay_ms));

      if (!life->stopping.load())
      {
        if (guard.should_connect(*ep, ConnectReason::Manual, quiet))
          p2p.connect(*ep);
      }
    }

    // Auto-stop timer
    std::thread stopper;
    if (run_seconds > 0)
    {
      stopper = std::thread([life, run_seconds]()
                            {
                              std::this_thread::sleep_for(std::chrono::seconds(run_seconds));
                              if (life)
                                life->running.store(false); });
    }

    // Stats loop
    const auto tick = std::chrono::milliseconds(stats_every_ms);
    while (life->running.load())
    {
      if (!quiet)
      {
        auto st = p2p.stats();
        auto cs = guard.stats();
        std::cout << "[vix p2p] ";
        print_stats_line(st, cs, guard.tracked_endpoints());
      }
      std::this_thread::sleep_for(tick);
    }

    if (stopper.joinable())
      stopper.join();

    if (!quiet)
      hint("stopping...");

    // P0.2: stop order (as far as CLI can enforce without touching core)
    // 1) running=false already
    // 2) mark stopping=true -> callbacks become no-op
    // 3) detach discovery/bootstrap to avoid late callbacks holding refs
    // 4) p2p.stop() handles acceptor/sessions/threads inside core
    life->stopping.store(true);

    // Detach hooks (best-effort: prevents late callback invocations through node)
    node->set_discovery(nullptr);
    node->set_bootstrap(nullptr);

    p2p.stop();

    // Final stats
    auto final_st = p2p.stats();
    auto final_cs = guard.stats();

    std::cout << "[vix p2p] final: ";
    print_stats_line(final_st, final_cs, guard.tracked_endpoints());

    if (!quiet)
      success("bye");

    return 0;
  }

  int help()
  {
    usage(std::cout);
    return 0;
  }

} // namespace vix::commands::P2PCommand
