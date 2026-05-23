/**
 *
 *  @file LogsOutput.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/logs/LogsOutput.hpp>
#include <vix/cli/util/Ui.hpp>

#include <ostream>
#include <string>

namespace vix::commands::logs::output
{
  namespace
  {
    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    std::string target_name(LogsTarget target)
    {
      switch (target)
      {
      case LogsTarget::All:
        return "all";
      case LogsTarget::App:
        return "app";
      case LogsTarget::Proxy:
        return "proxy";
      case LogsTarget::Errors:
        return "errors";
      }

      return "all";
    }
  }

  void print_summary(
      std::ostream &out,
      const LogsConfig &cfg,
      const LogsOptions &options)
  {
    vix::cli::util::section(out, "Logs");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Target", target_name(options.target));
    vix::cli::util::kv(out, "Service", cfg.serviceName.empty() ? "(missing)" : cfg.serviceName);
    vix::cli::util::kv(out, "Nginx access", cfg.nginxAccessLog.string());
    vix::cli::util::kv(out, "Nginx error", cfg.nginxErrorLog.string());
    vix::cli::util::kv(out, "Lines", std::to_string(cfg.lines));
    vix::cli::util::kv(out, "Follow", yes_no(options.follow));
    vix::cli::util::kv(out, "Errors only", yes_no(options.errorsOnly));

    if (!options.since.empty())
      vix::cli::util::kv(out, "Since", options.since);
  }

  void step(
      std::ostream &out,
      const std::string &label)
  {
    vix::cli::util::section(out, label);
  }

  void command(
      std::ostream &out,
      const std::string &command)
  {
    vix::cli::util::kv(out, "Command", command);
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
