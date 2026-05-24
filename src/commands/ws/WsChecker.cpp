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
#include <cctype>

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

    enum class WsFailureKind
    {
      Unknown,
      Dns,
      ConnectionRefused,
      Timeout,
      TlsUnsupported,
      BadHandshake,
      MissingUpgrade,
      BadPath,
      ProxyHttpResponse
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

    bool is_valid_port_number(const std::string &value)
    {
      if (!is_digit_string(value))
        return false;

      try
      {
        const unsigned long parsed = std::stoul(value);
        return parsed >= 1 && parsed <= 65535;
      }
      catch (...)
      {
        return false;
      }
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

    std::string lower_copy(std::string value)
    {
      for (char &ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

      return value;
    }

    bool contains_text(
        const std::string &value,
        const std::string &needle)
    {
      return lower_copy(value).find(lower_copy(needle)) != std::string::npos;
    }

    WsFailureKind classify_failure(const std::string &message)
    {
      if (message.empty())
        return WsFailureKind::Unknown;

      if (contains_text(message, "resolve") ||
          contains_text(message, "dns") ||
          contains_text(message, "host not found") ||
          contains_text(message, "name or service not known"))
      {
        return WsFailureKind::Dns;
      }

      if (contains_text(message, "connection refused") ||
          contains_text(message, "refused"))
      {
        return WsFailureKind::ConnectionRefused;
      }

      if (contains_text(message, "timeout") ||
          contains_text(message, "timed out"))
      {
        return WsFailureKind::Timeout;
      }

      if (contains_text(message, "upgrade") ||
          contains_text(message, "sec-websocket-accept"))
      {
        return WsFailureKind::MissingUpgrade;
      }

      if (contains_text(message, "404") ||
          contains_text(message, "not found"))
      {
        return WsFailureKind::BadPath;
      }

      if (contains_text(message, "301") ||
          contains_text(message, "302") ||
          contains_text(message, "400") ||
          contains_text(message, "403") ||
          contains_text(message, "502") ||
          contains_text(message, "503") ||
          contains_text(message, "504") ||
          contains_text(message, "http"))
      {
        return WsFailureKind::ProxyHttpResponse;
      }

      if (contains_text(message, "handshake"))
        return WsFailureKind::BadHandshake;

      return WsFailureKind::Unknown;
    }

    std::string failure_title(WsFailureKind kind)
    {
      switch (kind)
      {
      case WsFailureKind::Dns:
        return "DNS resolution failed.";
      case WsFailureKind::ConnectionRefused:
        return "WebSocket TCP connection was refused.";
      case WsFailureKind::Timeout:
        return "WebSocket connection timed out.";
      case WsFailureKind::MissingUpgrade:
        return "WebSocket upgrade headers are missing or invalid.";
      case WsFailureKind::BadPath:
        return "WebSocket path does not exist on the server.";
      case WsFailureKind::ProxyHttpResponse:
        return "Server returned an HTTP response instead of a WebSocket upgrade.";
      case WsFailureKind::BadHandshake:
        return "WebSocket handshake failed.";
      case WsFailureKind::TlsUnsupported:
        return "TLS WebSocket checks are not supported yet.";
      case WsFailureKind::Unknown:
        return "WebSocket check failed.";
      }

      return "WebSocket check failed.";
    }

    std::string failure_fix(WsFailureKind kind)
    {
      switch (kind)
      {
      case WsFailureKind::Dns:
        return "check the domain name, DNS record and network resolver";
      case WsFailureKind::ConnectionRefused:
        return "check that the WebSocket service is running and listening on the selected port";
      case WsFailureKind::Timeout:
        return "check firewall rules, service availability, Nginx upstream and network reachability";
      case WsFailureKind::MissingUpgrade:
        return "check Nginx proxy_set_header Upgrade and Connection headers";
      case WsFailureKind::BadPath:
        return "check the WebSocket route/path configured by the application";
      case WsFailureKind::ProxyHttpResponse:
        return "check that the endpoint is a WebSocket endpoint and not a normal HTTP route";
      case WsFailureKind::BadHandshake:
        return "check WebSocket port, route/path and proxy upgrade configuration";
      case WsFailureKind::TlsUnsupported:
        return "use ws:// for local checks until native wss:// support is added";
      case WsFailureKind::Unknown:
        return "run with --verbose and check the WebSocket server logs";
      }

      return "run with --verbose and check the WebSocket server logs";
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

        if (!is_valid_port_number(parsed.port))
        {
          error = "WebSocket URL port must be between 1 and 65535";
          return std::nullopt;
        }
      }

      if (parsed.target.front() != '/')
        parsed.target.insert(parsed.target.begin(), '/');

      if (parsed.target.find('#') != std::string::npos)
      {
        error = "WebSocket URL fragments are not supported";
        return std::nullopt;
      }

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
        const std::string errorMessage = state.error;
        const WsFailureKind failureKind = classify_failure(errorMessage);

        client->close();

        output::error(std::cerr, failure_title(failureKind));

        if (!errorMessage.empty())
          output::warn(std::cerr, errorMessage);

        output::fix(std::cerr, failure_fix(failureKind));
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
      output::command(std::cout, "check ping capability");

      output::warn(
          std::cout,
          "ping diagnostic disabled");

      output::fix(
          std::cout,
          "use --no-ping or check heartbeat logs");
    }

    client->close();

    output::ok(std::cout, "WebSocket endpoint is reachable");
    return 0;
  }
}
