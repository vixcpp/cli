/**
 *
 *  @file ProductionValidator.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/production/ProductionValidator.hpp>
#include <vix/cli/commands/production/ProductionOutput.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace vix::commands::production::validator
{
  namespace
  {
    bool run_cmd(const std::string &cmd)
    {
      output::command(std::cout, cmd);
      return std::system(cmd.c_str()) == 0;
    }

    bool run_section(
        const std::string &label,
        const std::string &cmd)
    {
      output::section(std::cout, label);

      if (run_cmd(cmd))
      {
        output::ok(std::cout, label + " passed");
        return true;
      }

      output::error(std::cerr, label + " failed");
      return false;
    }
  }

  int validate()
  {
#ifndef __linux__
    output::error(
        std::cerr,
        "vix production validate is currently supported on Linux only.");

    return 1;
#else
    output::section(std::cout, "Production Config Validate");

    bool ok = true;

    if (!run_section("Deploy Config", "vix deploy --dry-run"))
      ok = false;

    if (!run_section("Proxy Config", "vix proxy nginx check"))
      ok = false;

    if (!run_section("Health Config", "vix health"))
      ok = false;

    if (!ok)
    {
      output::error(
          std::cerr,
          "production config validation failed");

      output::fix(
          std::cerr,
          "update production config in vix.json");

      return 1;
    }

    output::ok(
        std::cout,
        "production config is valid");

    return 0;
#endif
  }

  int status()
  {
#ifndef __linux__
    output::error(
        std::cerr,
        "vix production status is currently supported on Linux only.");

    return 1;
#else
    output::section(std::cout, "Production Status");

    bool ok = true;

    if (!run_section("Service", "vix service status"))
      ok = false;

    if (!run_section("Health", "vix health"))
      ok = false;

    if (!run_section("Proxy", "vix proxy nginx check"))
      ok = false;

    if (!run_section("Logs", "vix logs errors --repeated -n 80"))
      ok = false;

    if (!ok)
    {
      output::error(
          std::cerr,
          "production status check failed");

      output::fix(
          std::cerr,
          "inspect service, health, proxy, and logs output above");

      return 1;
    }

    output::ok(
        std::cout,
        "production looks healthy");

    return 0;
#endif
  }
}
