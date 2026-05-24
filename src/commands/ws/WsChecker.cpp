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
#ifdef VIX_CLI_HAS_WEBSOCKET
#include <vix/websocket/client.hpp>
#endif

#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

#else

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#endif

namespace vix::commands::ws::checker
{
  namespace
  {
#ifdef _WIN32
    using NativeSocket = SOCKET;
    constexpr NativeSocket invalid_socket_value = INVALID_SOCKET;
#else
    using NativeSocket = int;
    constexpr NativeSocket invalid_socket_value = -1;
#endif

    struct ParsedWsUrl
    {
      std::string scheme{};
      std::string host{};
      std::string port{};
      std::string target{"/"};
      bool tls{false};
    };

#ifdef VIX_CLI_HAS_WEBSOCKET
    struct CheckState
    {
      std::mutex mutex;
      std::condition_variable cv;
      bool opened{false};
      bool closed{false};
      bool errored{false};
      std::string error{};
    };
#endif

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

#ifdef _WIN32
    struct WinsockRuntime
    {
      bool ready{false};

      WinsockRuntime()
      {
        WSADATA data{};
        ready = (::WSAStartup(MAKEWORD(2, 2), &data) == 0);
      }

      ~WinsockRuntime()
      {
        if (ready)
          ::WSACleanup();
      }

      WinsockRuntime(const WinsockRuntime &) = delete;
      WinsockRuntime &operator=(const WinsockRuntime &) = delete;
    };

    bool ensure_winsock_ready(std::string &error)
    {
      static WinsockRuntime runtime;

      if (!runtime.ready)
      {
        error = "Winsock initialization failed";
        return false;
      }

      return true;
    }
#endif

    void close_native_socket(NativeSocket socket)
    {
      if (socket == invalid_socket_value)
        return;

#ifdef _WIN32
      ::closesocket(socket);
#else
      ::close(socket);
#endif
    }

    std::string native_socket_error_message(int code)
    {
#ifdef _WIN32
      return "socket error " + std::to_string(code);
#else
      return std::strerror(code);
#endif
    }

    int native_last_error()
    {
#ifdef _WIN32
      return ::WSAGetLastError();
#else
      return errno;
#endif
    }

    bool is_connect_in_progress(int errorCode)
    {
#ifdef _WIN32
      return errorCode == WSAEWOULDBLOCK ||
             errorCode == WSAEINPROGRESS ||
             errorCode == WSAEALREADY ||
             errorCode == WSAEINVAL;
#else
      return errorCode == EINPROGRESS;
#endif
    }

    bool set_socket_non_blocking(NativeSocket socket, std::string &error)
    {
#ifdef _WIN32
      u_long mode = 1;

      if (::ioctlsocket(socket, FIONBIO, &mode) == 0)
        return true;

      error = native_socket_error_message(native_last_error());
      return false;
#else
      const int flags = ::fcntl(socket, F_GETFL, 0);

      if (flags < 0)
      {
        error = native_socket_error_message(errno);
        return false;
      }

      if (::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0)
        return true;

      error = native_socket_error_message(errno);
      return false;
#endif
    }

    int clamp_timeout_ms(std::uint64_t timeoutMs)
    {
      if (timeoutMs == 0)
        return 1;

      if (timeoutMs > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();

      return static_cast<int>(timeoutMs);
    }

    bool wait_socket_writable(
        NativeSocket socket,
        std::uint64_t timeoutMs,
        std::string &error)
    {
      fd_set writeSet;
      FD_ZERO(&writeSet);
      FD_SET(socket, &writeSet);

      fd_set errorSet;
      FD_ZERO(&errorSet);
      FD_SET(socket, &errorSet);

      const int timeout = clamp_timeout_ms(timeoutMs);

      timeval tv{};
      tv.tv_sec = timeout / 1000;
      tv.tv_usec = (timeout % 1000) * 1000;

#ifdef _WIN32
      const int rc = ::select(
          0,
          nullptr,
          &writeSet,
          &errorSet,
          &tv);
#else
      const int rc = ::select(
          socket + 1,
          nullptr,
          &writeSet,
          &errorSet,
          &tv);
#endif

      if (rc == 0)
      {
        error = "TCP connection timed out";
        return false;
      }

      if (rc < 0)
      {
        error = native_socket_error_message(native_last_error());
        return false;
      }

      int socketError = 0;
#ifdef _WIN32
      int socketErrorSize = sizeof(socketError);
#else
      socklen_t socketErrorSize = sizeof(socketError);
#endif

      if (::getsockopt(
              socket,
              SOL_SOCKET,
              SO_ERROR,
#ifdef _WIN32
              reinterpret_cast<char *>(&socketError),
#else
              &socketError,
#endif
              &socketErrorSize) != 0)
      {
        error = native_socket_error_message(native_last_error());
        return false;
      }

      if (socketError != 0)
      {
        error = native_socket_error_message(socketError);
        return false;
      }

      return true;
    }

    bool tcp_connect_check(
        const std::string &host,
        const std::string &port,
        std::uint64_t timeoutMs,
        std::string &error)
    {
#ifdef _WIN32
      if (!ensure_winsock_ready(error))
        return false;
#endif

      addrinfo hints{};
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;

      addrinfo *result = nullptr;

      const int gai = ::getaddrinfo(
          host.c_str(),
          port.c_str(),
          &hints,
          &result);

      if (gai != 0)
      {
#ifdef _WIN32
        error = ::gai_strerrorA(gai);
#else
        error = ::gai_strerror(gai);
#endif
        return false;
      }

      std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses(
          result,
          ::freeaddrinfo);

      for (addrinfo *rp = addresses.get(); rp != nullptr; rp = rp->ai_next)
      {
        NativeSocket socket = ::socket(
            rp->ai_family,
            rp->ai_socktype,
            rp->ai_protocol);

        if (socket == invalid_socket_value)
        {
          error = native_socket_error_message(native_last_error());
          continue;
        }

        if (!set_socket_non_blocking(socket, error))
        {
          close_native_socket(socket);
          continue;
        }

        const int rc = ::connect(
            socket,
            rp->ai_addr,
#ifdef _WIN32
            static_cast<int>(rp->ai_addrlen)
#else
            rp->ai_addrlen
#endif
        );

        if (rc == 0)
        {
          close_native_socket(socket);
          return true;
        }

        const int connectError = native_last_error();

        if (!is_connect_in_progress(connectError))
        {
          error = native_socket_error_message(connectError);
          close_native_socket(socket);
          continue;
        }

        const bool writable = wait_socket_writable(
            socket,
            timeoutMs,
            error);

        close_native_socket(socket);

        if (writable)
          return true;
      }

      if (error.empty())
        error = "TCP connection failed";

      return false;
    }

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
          contains_text(message, "name or service not known") ||
          contains_text(message, "nodename nor servname provided"))
      {
        return WsFailureKind::Dns;
      }

      if (contains_text(message, "connection refused") ||
          contains_text(message, "refused") ||
          contains_text(message, "socket error 10061"))
      {
        return WsFailureKind::ConnectionRefused;
      }

      if (contains_text(message, "timeout") ||
          contains_text(message, "timed out"))
      {
        return WsFailureKind::Timeout;
      }

      if (contains_text(message, "upgrade") ||
          contains_text(message, "sec-websocket-accept") ||
          contains_text(message, "bad response") ||
          contains_text(message, "invalid response") ||
          contains_text(message, "expected 101") ||
          contains_text(message, "did not switch protocols") ||
          contains_text(message, "missing connection") ||
          contains_text(message, "missing upgrade"))
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
        return "WebSocket upgrade failed.";
      case WsFailureKind::BadPath:
        return "WebSocket path does not exist on the server.";
      case WsFailureKind::ProxyHttpResponse:
        return "Server returned HTTP instead of WebSocket upgrade.";
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
        return "check domain and DNS resolver";
      case WsFailureKind::ConnectionRefused:
        return "check service port and listener";
      case WsFailureKind::Timeout:
        return "check firewall, upstream and service";
      case WsFailureKind::MissingUpgrade:
        return "check Upgrade and Connection headers";
      case WsFailureKind::BadPath:
        return "check WebSocket route/path";
      case WsFailureKind::ProxyHttpResponse:
        return "check endpoint and proxy route";
      case WsFailureKind::BadHandshake:
        return "check port, path and proxy upgrade";
      case WsFailureKind::TlsUnsupported:
        return "use ws:// until wss:// support is added";
      case WsFailureKind::Unknown:
        return "run with --verbose and check logs";
      }

      return "run with --verbose and check logs";
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

#ifdef VIX_CLI_HAS_WEBSOCKET
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
#endif

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

    bool run_tcp_probe(
        const ParsedWsUrl &parsed,
        std::uint64_t timeoutMs)
    {
      output::step(std::cout, "TCP");
      output::command(
          std::cout,
          "connect " + parsed.host + ":" + parsed.port);

      std::string tcpError;

      if (tcp_connect_check(
              parsed.host,
              parsed.port,
              timeoutMs,
              tcpError))
      {
        output::ok(std::cout, "TCP endpoint is reachable");
        return true;
      }

      const WsFailureKind failureKind = classify_failure(tcpError);

      output::error(std::cerr, failure_title(failureKind));

      if (!tcpError.empty())
        output::warn(std::cerr, tcpError);

      output::fix(std::cerr, failure_fix(failureKind));
      return false;
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
      output::error(std::cerr, "wss:// checks are not supported yet.");
      output::fix(std::cerr, "use ws:// until TLS support is added");
      return 1;
    }

    if (!run_tcp_probe(*parsed, options.timeoutMs))
      return 1;

#ifndef VIX_CLI_HAS_WEBSOCKET
    output::warn(
        std::cout,
        "WebSocket client module is not available in this build");

    output::fix(
        std::cout,
        "TCP check passed; rebuild the Vix CLI with the websocket module enabled for handshake checks");

    return 0;
#else
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
      output::fix(std::cerr, "check service, firewall and proxy");
      return 1;
    }

    bool opened = false;
    bool errored = false;
    std::string errorMessage;

    {
      std::lock_guard<std::mutex> lock(state.mutex);
      opened = state.opened;
      errored = state.errored;
      errorMessage = state.error;
    }

    if (errored)
    {
      const WsFailureKind failureKind = classify_failure(errorMessage);

      client->close();

      output::error(std::cerr, failure_title(failureKind));

      if (!errorMessage.empty())
        output::warn(std::cerr, errorMessage);

      output::fix(std::cerr, failure_fix(failureKind));
      return 1;
    }

    if (!opened)
    {
      client->close();

      output::error(std::cerr, "WebSocket closed before opening.");
      output::fix(std::cerr, "check upgrade support");
      return 1;
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
#endif
  }
}
