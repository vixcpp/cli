/**
 *
 *  @file DeployOutput.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/deploy/DeployOutput.hpp>
#include <vix/cli/util/Ui.hpp>

#include <ostream>
#include <string>

namespace vix::commands::deploy::output
{
  namespace
  {
    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    std::string enabled_disabled(bool value)
    {
      return value ? "enabled" : "disabled";
    }
  }

  void print_summary(
      std::ostream &out,
      const DeployConfig &cfg,
      const DeployOptions &options)
  {
    vix::cli::util::section(out, "Deploy");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Branch", cfg.branch);
    vix::cli::util::kv(out, "Pull", yes_no(cfg.pull));
    vix::cli::util::kv(out, "Build", cfg.buildCommand);
    vix::cli::util::kv(out, "Tests", enabled_disabled(cfg.tests));
    vix::cli::util::kv(out, "Service", cfg.serviceName.empty() ? "(missing)" : cfg.serviceName);
    vix::cli::util::kv(out, "Health local", enabled_disabled(cfg.healthLocal));
    vix::cli::util::kv(out, "Health public", enabled_disabled(cfg.healthPublic));
    vix::cli::util::kv(out, "Logs on failure", enabled_disabled(cfg.logsOnFailure));
    vix::cli::util::kv(out, "Dry run", yes_no(options.dryRun));
    vix::cli::util::kv(out, "Verbose", yes_no(options.verbose));
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
