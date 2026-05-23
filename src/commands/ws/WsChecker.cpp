/**
 *
 *  @file WsChecker.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/ws/WsChecker.hpp>
#include <vix/cli/commands/ws/WsOutput.hpp>
#include <vix/websocket/client.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace vix::commands::ws::checker
{
  namespace
  {
    struct ParsedWsUrl
    {
      std::string scheme{};
      std::string host{};
      std::string port{};
      std::string target{"/"};
      bool tls{false};
    };

    struct CheckState
    {
      std::mutex mutex;
      std::condition_variable cv;
      bool opened{false};
      bool closed{false};
      bool errored{false};
      std::string error{};
    };

    bool starts_with(
        const std::string &value,
        const std::string &prefix)
    {
      return value.rfind(prefix, 0) == 0;
    }

    bool is_digit_string(const std::string &value)
    {
      if (value.empty())
        return false;

      for (char ch : value)
      {
        if (ch < '0' || ch > '9')
          return false;
      }

      return true;
    }

    std::string selected_url(
        const WsConfig &cfg,
        const WsOptions &options)
    {
      if (!options.url.empty())
        return options.url;

      if (!cfg.publicUrl.empty())
        return cfg.publicUrl;

      return cfg.localUrl;
    }

    std::optional<ParsedWsUrl> parse_ws_url(
        const std::string &url,
        std::string &error)
    {
      ParsedWsUrl parsed;

      std::string rest;

      if (starts_with(url, "ws://"))
      {
        parsed.scheme = "ws";
        parsed.tls = false;
        rest = url.substr(5);
      }
      else if (starts_with(url, "wss://"))
      {
        parsed.scheme = "wss";
        parsed.tls = true;
        rest = url.substr(6);
      }
      else
      {
        error = "WebSocket URL must start with ws:// or wss://";
        return std::nullopt;
      }

      if (rest.empty())
      {
        error = "WebSocket URL host is missing";
        return std::nullopt;
      }

      const std::size_t slash = rest.find('/');
      std::string authority;

      if (slash == std::string::npos)
      {
        authority = rest;
        parsed.target = "/";
      }
      else
      {
        authority = rest.substr(0, slash);
        parsed.target = rest.substr(slash);

        if (parsed.target.empty())
          parsed.target = "/";
      }

      if (authority.empty())
      {
        error = "WebSocket URL host is missing";
        return std::nullopt;
      }

      const std::size_t colon = authority.rfind(':');

      if (colon == std::string::npos)
      {
        parsed.host = authority;
        parsed.port = parsed.tls ? "443" : "80";
      }
      else
      {
        parsed.host = authority.substr(0, colon);
        parsed.port = authority.substr(colon + 1);

        if (parsed.host.empty())
        {
          error = "WebSocket URL host is missing";
          return std::nullopt;
        }

        if (!is_digit_string(parsed.port))
        {
          error = "WebSocket URL port must be numeric";
          return std::nullopt;
        }
      }

      if (parsed.target.front() != '/')
        parsed.target.insert(parsed.target.begin(), '/');

      return parsed;
    }

    bool wait_for_open_or_error(
        CheckState &state,
        std::uint64_t timeoutMs)
    {
      std::unique_lock<std::mutex> lock(state.mutex);

      return state.cv.wait_for(
          lock,
          std::chrono::milliseconds(timeoutMs),
          [&state]()
          {
            return state.opened || state.errored || state.closed;
          });
    }

    void print_parsed_url(
        const ParsedWsUrl &url)
    {
      output::step(std::cout, "WebSocket Target");
      output::command(
          std::cout,
          url.scheme + "://" + url.host + ":" + url.port + url.target);

      output::ok(std::cout, "scheme: " + url.scheme);
      output::ok(std::cout, "host: " + url.host);
      output::ok(std::cout, "port: " + url.port);
      output::ok(std::cout, "path: " + url.target);
    }
  }

  int check(
      const WsConfig &cfg,
      const WsOptions &options)
  {
    output::print_summary(std::cout, cfg, options);

    const std::string url = selected_url(cfg, options);

    if (url.empty())
    {
      output::error(std::cerr, "Missing WebSocket URL.");
      output::fix(std::cerr, "run `vix ws check ws://127.0.0.1:9090/ws`");
      return 1;
    }

    std::string parseError;
    const auto parsed = parse_ws_url(url, parseError);

    if (!parsed)
    {
      output::error(std::cerr, parseError);
      output::fix(std::cerr, "use a URL like ws://127.0.0.1:9090/ws");
      return 1;
    }

    print_parsed_url(*parsed);

    if (parsed->tls)
    {
      output::error(std::cerr, "wss:// checks are not supported by the native checker yet.");
      output::fix(std::cerr, "use ws:// for local checks, or test WSS through the proxy layer later");
      return 1;
    }

    CheckState state;

    auto client = vix::websocket::Client::create(
        parsed->host,
        parsed->port,
        parsed->target);

    client->on_open(
        [&state]()
        {
          {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.opened = true;
          }

          state.cv.notify_all();
        });

    client->on_error(
        [&state](const std::string &message)
        {
          {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.errored = true;
            state.error = message;
          }

          state.cv.notify_all();
        });

    client->on_close(
        [&state]()
        {
          {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.closed = true;
          }

          state.cv.notify_all();
        });

    output::step(std::cout, "Handshake");
    output::command(std::cout, "connect " + url);

    try
    {
      client->connect();
    }
    catch (const std::exception &e)
    {
      output::error(std::cerr, "WebSocket client failed to start.");
      output::warn(std::cerr, e.what());
      return 1;
    }

    const bool completed =
        wait_for_open_or_error(state, options.timeoutMs);

    if (!completed)
    {
      client->close();

      output::error(std::cerr, "WebSocket check timed out.");
      output::fix(std::cerr, "check host, port, firewall, service status and Nginx upstream");
      return 1;
    }

    {
      std::lock_guard<std::mutex> lock(state.mutex);

      if (state.errored)
      {
        client->close();

        output::error(std::cerr, "WebSocket handshake failed.");

        if (!state.error.empty())
          output::warn(std::cerr, state.error);

        output::fix(std::cerr, "check WebSocket port, path and proxy upgrade headers");
        return 1;
      }

      if (!state.opened)
      {
        client->close();

        output::error(std::cerr, "WebSocket connection closed before opening.");
        output::fix(std::cerr, "check whether the endpoint accepts WebSocket upgrades");
        return 1;
      }
    }

    output::ok(std::cout, "WebSocket handshake succeeded");

    if (options.ping)
    {
      output::step(std::cout, "Ping");
      output::command(std::cout, "send websocket ping frame");

      try
      {
        client->send_ping();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        output::ok(std::cout, "ping frame sent");
      }
      catch (const std::exception &e)
      {
        client->close();

        output::error(std::cerr, "failed to send WebSocket ping");
        output::warn(std::cerr, e.what());
        return 1;
      }
    }

    client->close();

    output::ok(std::cout, "WebSocket endpoint is reachable");
    return 0;
  }
}
