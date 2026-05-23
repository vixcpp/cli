/**
 *
 *  @file HealthOutput.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/health/HealthOutput.hpp>
#include <vix/cli/commands/health/HealthConfig.hpp>
#include <vix/cli/util/Ui.hpp>

#include <iostream>
#include <ostream>
#include <string>

namespace vix::commands::health::output
{
  namespace
  {
    std::string enabled_disabled(bool value)
    {
      return value ? "enabled" : "disabled";
    }

    std::string status_text(const HealthResult &result)
    {
      if (result.actualStatus <= 0)
        return "connection failed";

      return std::to_string(result.actualStatus);
    }

    std::string time_text(const HealthResult &result)
    {
      return std::to_string(result.responseMs) + " ms";
    }
  }

  void print_summary(
      std::ostream &out,
      const HealthConfig &cfg)
  {
    vix::cli::util::section(out, "Health Config");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Service", cfg.serviceName.value_or("(none)"));
    vix::cli::util::kv(out, "Local", cfg.local.enabled ? cfg.local.url : "disabled");
    vix::cli::util::kv(out, "Public", cfg.publicEndpoint.enabled ? cfg.publicEndpoint.url : "disabled");
    vix::cli::util::kv(out, "WebSocket", cfg.websocket.enabled ? cfg.websocket.url : "disabled");
  }

  void print_result(
      std::ostream &out,
      const HealthResult &result)
  {
    vix::cli::util::section(out, "Health Check");

    vix::cli::util::kv(out, "Target", target_name(result.target));
    vix::cli::util::kv(out, "URL", result.url.empty() ? "(missing)" : result.url);
    vix::cli::util::kv(out, "Expected", std::to_string(result.expectedStatus));
    vix::cli::util::kv(out, "Status", status_text(result));
    vix::cli::util::kv(out, "Time", time_text(result));
    vix::cli::util::kv(out, "Max time", std::to_string(result.maxResponseMs) + " ms");
    vix::cli::util::kv(out, "Healthy", enabled_disabled(result.healthy));

    if (!result.error.empty())
      vix::cli::util::kv(out, "Error", result.error);
  }

  void ok(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::ok_line(out, message);
  }

  void warn(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::warn_line(out, message);
  }

  void error(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::err_line(out, message);
  }

  void fix(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::warn_line(out, "Fix: " + message);
  }
}
