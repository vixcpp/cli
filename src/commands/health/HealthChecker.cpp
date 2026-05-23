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

#include <vix/net/http/ClientRequest.hpp>
#include <vix/net/http/CurlClient.hpp>
#include <vix/net/http/Method.hpp>

#include <chrono>
#include <cstdlib>
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

      vix::net::http::CurlClient client;

      vix::net::http::ClientRequest request;
      request.set_method(vix::net::http::Method::Head)
          .set_url(endpoint.url)
          .set_timeout_ms(endpoint.timeoutMs);

      const auto start = std::chrono::steady_clock::now();
      auto response = client.send(request);
      const auto end = std::chrono::steady_clock::now();

      result.responseMs =
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  end - start)
                  .count());

      if (!response)
      {
        result.actualStatus = 0;
        result.healthy = false;
        result.error = response.error().message();
        return result;
      }

      result.actualStatus = response.value().status_code;

      if (response.value().has_error())
        result.error = response.value().error;

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

      vix::net::http::CurlClient client;

      vix::net::http::ClientRequest request;
      request.set_method(vix::net::http::Method::Get)
          .set_url(endpoint.url)
          .set_timeout_ms(endpoint.timeoutMs)
          .set_header("Connection", "Upgrade")
          .set_header("Upgrade", "websocket")
          .set_header("Sec-WebSocket-Key", "SGVsbG8sIHdvcmxkIQ==")
          .set_header("Sec-WebSocket-Version", "13");

      const auto start = std::chrono::steady_clock::now();
      auto response = client.send(request);
      const auto end = std::chrono::steady_clock::now();

      result.responseMs =
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  end - start)
                  .count());

      if (!response)
      {
        result.actualStatus = 0;
        result.healthy = false;
        result.error = response.error().message();
        return result;
      }

      result.actualStatus = response.value().status_code;

      if (response.value().has_error())
        result.error = response.value().error;

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
