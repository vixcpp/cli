/**
 *
 *  @file HealthChecker.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/health/HealthChecker.hpp>
#include <vix/cli/commands/health/HealthConfig.hpp>
#include <vix/cli/commands/health/HealthOutput.hpp>

#include <vix/requests/Client.hpp>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace vix::commands::health::checker
{
  namespace
  {
    bool run_cmd(const std::string &cmd)
    {
      return std::system(cmd.c_str()) == 0;
    }

    std::string websocket_http_url(std::string url)
    {
      if (url.rfind("wss://", 0) == 0)
        return "https://" + url.substr(6);

      if (url.rfind("ws://", 0) == 0)
        return "http://" + url.substr(5);

      return url;
    }

    bool service_is_running(const HealthConfig &cfg)
    {
      if (!cfg.serviceName.has_value())
        return true;

      return run_cmd(
          "systemctl is-active --quiet " +
          cfg.serviceName.value());
    }

    HealthResult disabled_result(
        HealthTarget target,
        int expectedStatus)
    {
      HealthResult result;
      result.target = target;
      result.expectedStatus = expectedStatus;
      result.healthy = false;
      result.error = "health endpoint is not configured";
      return result;
    }

    HealthResult check_http_endpoint(
        HealthTarget target,
        const HealthEndpointConfig &endpoint)
    {
      if (!endpoint.enabled || endpoint.url.empty())
        return disabled_result(target, endpoint.expectedStatus);

      HealthResult result;
      result.target = target;
      result.url = endpoint.url;
      result.expectedStatus = endpoint.expectedStatus;
      result.maxResponseMs = endpoint.maxResponseMs;

      const auto start = std::chrono::steady_clock::now();
      try
      {
        vix::requests::RequestOptions options;
        options.follow_redirects = false;

        if (endpoint.timeoutMs != 0)
        {
          const std::uint64_t timeoutSeconds = endpoint.timeoutMs / 1000;
          const std::uint64_t effectiveSeconds =
              timeoutSeconds == 0 ? 1 : timeoutSeconds;
          options.timeout.set_total(vix::requests::Timeout::Duration{
              static_cast<vix::requests::Timeout::Duration::rep>(
                  effectiveSeconds * 1000)});
        }

        vix::requests::Client client;
        const auto response = client.head(
            websocket_http_url(endpoint.url), options);
        const auto end = std::chrono::steady_clock::now();

        result.responseMs =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start)
                    .count());
        result.actualStatus = response.status_code();
      }
      catch (const std::exception &ex)
      {
        const auto end = std::chrono::steady_clock::now();

        result.responseMs =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start)
                    .count());
        result.actualStatus = 0;
        result.healthy = false;
        result.error = ex.what();
        return result;
      }

      if (result.actualStatus != result.expectedStatus)
      {
        result.healthy = false;
        result.error = "unexpected HTTP status";
        return result;
      }

      if (result.responseMs > result.maxResponseMs)
      {
        result.healthy = false;
        result.error = "response time exceeded";
        return result;
      }

      result.healthy = true;
      return result;
    }

    HealthResult check_websocket_endpoint(
        const HealthEndpointConfig &endpoint)
    {
      if (!endpoint.enabled || endpoint.url.empty())
        return disabled_result(HealthTarget::WebSocket, endpoint.expectedStatus);

      HealthResult result;
      result.target = HealthTarget::WebSocket;
      result.url = endpoint.url;
      result.expectedStatus = endpoint.expectedStatus;
      result.maxResponseMs = endpoint.maxResponseMs;

      const auto start = std::chrono::steady_clock::now();
      try
      {
        vix::requests::RequestOptions options;
        options.follow_redirects = false;
        options.headers.set("Connection", "Upgrade");
        options.headers.set("Upgrade", "websocket");
        options.headers.set("Sec-WebSocket-Key", "SGVsbG8sIHdvcmxkIQ==");
        options.headers.set("Sec-WebSocket-Version", "13");

        if (endpoint.timeoutMs != 0)
        {
          const std::uint64_t timeoutSeconds = endpoint.timeoutMs / 1000;
          const std::uint64_t effectiveSeconds =
              timeoutSeconds == 0 ? 1 : timeoutSeconds;
          options.timeout.set_total(vix::requests::Timeout::Duration{
              static_cast<vix::requests::Timeout::Duration::rep>(
                  effectiveSeconds * 1000)});
        }

        vix::requests::Client client;
        const auto response = client.get(
            websocket_http_url(endpoint.url), options);
        const auto end = std::chrono::steady_clock::now();

        result.responseMs =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start)
                    .count());
        result.actualStatus = response.status_code();
      }
      catch (const std::exception &ex)
      {
        const auto end = std::chrono::steady_clock::now();

        result.responseMs =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start)
                    .count());
        result.actualStatus = 0;
        result.healthy = false;
        result.error = ex.what();
        return result;
      }

      if (result.actualStatus != result.expectedStatus)
      {
        result.healthy = false;
        result.error = "unexpected WebSocket status";
        return result;
      }

      if (result.responseMs > result.maxResponseMs)
      {
        result.healthy = false;
        result.error = "response time exceeded";
        return result;
      }

      result.healthy = true;
      return result;
    }

    int print_and_exit(const HealthResult &result)
    {
      output::print_result(std::cout, result);

      if (!result.healthy)
      {
        output::error(
            std::cerr,
            std::string(target_name(result.target)) + " endpoint is unhealthy");

        if (result.target == HealthTarget::Local)
          output::fix(std::cerr, "run `vix service status`");

        if (result.target == HealthTarget::Public)
          output::fix(std::cerr, "run `vix proxy nginx check`");

        if (result.target == HealthTarget::WebSocket)
          output::fix(std::cerr, "check WebSocket proxy and upstream service");

        return 1;
      }

      output::ok(
          std::cout,
          std::string(target_name(result.target)) + " endpoint is healthy");

      return 0;
    }
  }

  int check_all(const HealthConfig &cfg)
  {
    output::print_summary(std::cout, cfg);

    if (!service_is_running(cfg))
    {
      output::error(std::cerr, "configured service is not running");
      output::fix(std::cerr, "run `vix service status`");
      return 1;
    }

    bool ok = true;

    if (cfg.local.enabled)
      ok = check_local(cfg) == 0 && ok;

    if (cfg.publicEndpoint.enabled)
      ok = check_public(cfg) == 0 && ok;

    if (cfg.websocket.enabled)
      ok = check_websocket(cfg) == 0 && ok;

    if (!cfg.local.enabled &&
        !cfg.publicEndpoint.enabled &&
        !cfg.websocket.enabled)
    {
      output::error(std::cerr, "no health endpoints configured");
      output::fix(std::cerr, "add production.health to vix.json");
      return 1;
    }

    return ok ? 0 : 1;
  }

  int check_local(const HealthConfig &cfg)
  {
    if (!service_is_running(cfg))
    {
      output::error(std::cerr, "configured service is not running");
      output::fix(std::cerr, "run `vix service status`");
      return 1;
    }

    return print_and_exit(
        check_http_endpoint(
            HealthTarget::Local,
            cfg.local));
  }

  int check_public(const HealthConfig &cfg)
  {
    return print_and_exit(
        check_http_endpoint(
            HealthTarget::Public,
            cfg.publicEndpoint));
  }

  int check_websocket(const HealthConfig &cfg)
  {
    return print_and_exit(
        check_websocket_endpoint(cfg.websocket));
  }
}
