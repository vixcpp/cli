/**
 *
 *  @file EnvOutput.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/env/EnvOutput.hpp>
#include <vix/cli/util/Ui.hpp>

#include <ostream>
#include <string>

namespace vix::commands::env::output
{
  namespace
  {
    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    std::string found_missing(bool value)
    {
      return value ? "found" : "missing";
    }

    std::string loaded_missing(bool value)
    {
      return value ? "loaded" : "missing";
    }

    std::string variable_status(const EnvVariable &variable)
    {
      if (variable.required && !variable.presentInEnv)
        return "required missing";

      if (!variable.presentInEnv && variable.presentInExample)
        return "missing from .env";

      if (variable.presentInEnv && !variable.presentInExample)
        return "missing from .env.example";

      if (variable.presentInEnv)
        return "loaded";

      return "missing";
    }
  }

  std::string display_value(
      const EnvVariable &variable,
      const EnvOptions &options)
  {
    if (!options.showValues)
      return "";

    if (variable.secret)
      return "********";

    if (options.masked)
      return variable.value.empty() ? "" : "********";

    return variable.value;
  }

  void print_summary(
      std::ostream &out,
      const EnvConfig &cfg,
      const EnvOptions &options)
  {
    vix::cli::util::section(out, "Env Check");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Project", cfg.projectDir.string());
    vix::cli::util::kv(out, ".env", found_missing(cfg.env.exists));
    vix::cli::util::kv(out, ".env.example", found_missing(cfg.example.exists));
    vix::cli::util::kv(out, "Production", yes_no(options.production));
    vix::cli::util::kv(out, "Masked", yes_no(options.masked));
    vix::cli::util::kv(out, "Show values", yes_no(options.showValues));

    if (options.production)
    {
      vix::cli::util::kv(
          out,
          "Service",
          cfg.serviceName.empty() ? "(missing)" : cfg.serviceName);

      vix::cli::util::kv(
          out,
          "Required vars",
          std::to_string(cfg.requiredProductionKeys.size()));

      vix::cli::util::kv(
          out,
          "Systemd vars",
          std::to_string(cfg.systemdEnvironment.size()));
    }
  }

  void print_variable(
      std::ostream &out,
      const EnvVariable &variable,
      const EnvOptions &options)
  {
    std::string message = variable.name + " : " + variable_status(variable);

    const std::string value = display_value(variable, options);

    if (!value.empty())
      message += " = " + value;

    if (variable.secret)
      message += " [secret]";

    if (variable.required)
      message += " [required]";

    if (variable.presentInSystemd)
      message += " [systemd]";

    if (variable.required && !variable.presentInEnv)
    {
      vix::cli::util::err_line(out, message);
      return;
    }

    if (!variable.presentInEnv ||
        !variable.presentInExample)
    {
      vix::cli::util::warn_line(out, message);
      return;
    }

    vix::cli::util::ok_line(out, message);
  }

  void section(
      std::ostream &out,
      const std::string &label)
  {
    vix::cli::util::section(out, label);
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
