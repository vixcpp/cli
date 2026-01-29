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

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <vix/p2p/Bootstrap.hpp>
#include <vix/p2p/Node.hpp>
#include <vix/p2p/P2P.hpp>
#include <vix/p2p/Peer.hpp>

using namespace vix::cli::style;

namespace
{
  static std::atomic<bool> g_running{true};

  static void on_sigint(int)
  {
    g_running.store(false);
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

    // also allow --key=value
    const std::string prefix = k + "=";
    for (const auto &x : a)
      if (x.rfind(prefix, 0) == 0)
        return x.substr(prefix.size());

    return std::nullopt;
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

  static void print_stats_line(const vix::p2p::NodeStats &st)
  {
    std::cout
        << "peers_total=" << st.peers_total
        << " peers_connected=" << st.peers_connected
        << " handshakes_started=" << st.handshakes_started
        << " handshakes_completed=" << st.handshakes_completed
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
    std::signal(SIGINT, on_sigint);
    g_running.store(true);

    std::vector<std::string> args = argsIn;

    if (args.empty() || has_flag(args, "--help") || has_flag(args, "-h"))
      return help();

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

    // Build node/runtime
    vix::p2p::NodeConfig cfg;
    cfg.node_id = *id_opt;
    cfg.listen_port = *listen_port_opt;

    auto node = vix::p2p::make_tcp_node(cfg);
    vix::p2p::P2PRuntime p2p(node);

    if (!quiet)
    {
      info("P2P starting");
      hint("node_id=" + cfg.node_id + " listen=" + std::to_string(cfg.listen_port));
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
          [node, no_connect](const vix::p2p::BootstrapPeer &p)
          {
            if (no_connect)
              return;

            vix::p2p::PeerEndpoint ep;
            ep.host = p.host;
            ep.port = p.tcp_port;
            ep.scheme = "tcp";

            node->connect(ep);
          });

      node->set_bootstrap(boot);

      if (!quiet)
        hint("bootstrap=on registry=" + registry);
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
          [node, no_connect](const vix::p2p::DiscoveryAnnouncement &a)
          {
            if (no_connect)
              return;

            vix::p2p::PeerEndpoint ep;
            ep.host = a.host;
            ep.port = a.port;
            ep.scheme = "tcp";

            node->connect(ep);
          });

      node->set_discovery(disc);

      if (!quiet)
        hint("discovery=on disc_port=" + std::to_string(disc_port));
    }

    p2p.start();

    // Optional outbound connect
    if (connect_opt)
    {
      auto ep = parse_endpoint(*connect_opt);
      if (!ep)
      {
        error("Invalid --connect. Expected host:port or tcp://host:port");
        p2p.stop();
        return 1;
      }

      if (!quiet)
        hint("connect=" + ep->host + ":" + std::to_string(ep->port) + " delay=" + std::to_string(connect_delay_ms) + "ms");

      if (connect_delay_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(connect_delay_ms));

      p2p.connect(*ep);
    }

    // Auto-stop timer
    std::thread stopper;
    if (run_seconds > 0)
    {
      stopper = std::thread([run_seconds]()
                            {
                              std::this_thread::sleep_for(std::chrono::seconds(run_seconds));
                              g_running.store(false); });
    }

    // Stats loop
    const auto tick = std::chrono::milliseconds(stats_every_ms);
    while (g_running.load())
    {
      if (!quiet)
      {
        auto st = p2p.stats();
        std::cout << "[vix p2p] ";
        print_stats_line(st);
      }
      std::this_thread::sleep_for(tick);
    }

    if (stopper.joinable())
      stopper.join();

    if (!quiet)
      hint("stopping...");

    p2p.stop();

    auto final_st = p2p.stats();
    std::cout << "[vix p2p] final: ";
    print_stats_line(final_st);

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
