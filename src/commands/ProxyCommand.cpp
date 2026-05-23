/**
 *
 *  @file ProxyCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/ProxyCommand.hpp>
#include <vix/cli/commands/proxy/NginxChecker.hpp>
#include <vix/cli/commands/proxy/NginxGenerator.hpp>
#include <vix/cli/commands/proxy/NginxOutput.hpp>
#include <vix/cli/commands/proxy/ProxyConfig.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace vix::commands
{
  namespace
  {
    int nginx_help()
    {
      std::cout
          << "Usage:\n"
          << "  vix proxy nginx <command>\n\n"
          << "Commands:\n"
          << "  init       Generate, install and enable an Nginx config\n"
          << "  check      Validate the installed Nginx proxy config\n"
          << "  reload     Validate and reload Nginx\n\n"
          << "Examples:\n"
          << "  vix proxy nginx init\n"
          << "  vix proxy nginx check\n"
          << "  vix proxy nginx reload\n";

      return 0;
    }

    int run_nginx(const std::vector<std::string> &args)
    {
      if (args.empty() || args[0] == "-h" || args[0] == "--help")
        return nginx_help();

      const auto cfg = proxy::load_nginx_proxy_config();
      const std::string action = args[0];

      if (action == "init")
        return proxy::nginx_generator::init(cfg);

      if (action == "check")
        return proxy::nginx_checker::check(cfg);

      if (action == "reload")
        return proxy::nginx_checker::reload(cfg);

      proxy::nginx_output::error(
          std::cerr,
          "unknown nginx proxy command: " + action);

      proxy::nginx_output::fix(
          std::cerr,
          "vix proxy nginx --help");

      return 1;
    }
  }

  int ProxyCommand::run(const std::vector<std::string> &args)
  {
#ifndef __linux__
    proxy::nginx_output::error(
        std::cerr,
        "vix proxy is currently supported on Linux only.");

    return 1;
#else
    if (args.empty() || args[0] == "-h" || args[0] == "--help")
      return help();

    const std::string provider = args[0];

    if (provider == "nginx")
    {
      std::vector<std::string> rest;

      if (args.size() > 1)
        rest.assign(args.begin() + 1, args.end());

      return run_nginx(rest);
    }

    proxy::nginx_output::error(
        std::cerr,
        "unknown proxy provider: " + provider);

    proxy::nginx_output::fix(
        std::cerr,
        "vix proxy nginx --help");

    return 1;
#endif
  }

  int ProxyCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix proxy <provider> <command>\n\n"
        << "Providers:\n"
        << "  nginx      Manage Nginx reverse proxy config\n\n"
        << "Commands:\n"
        << "  nginx init       Generate, install and enable an Nginx config\n"
        << "  nginx check      Validate the installed Nginx proxy config\n"
        << "  nginx reload     Validate and reload Nginx\n\n"
        << "Examples:\n"
        << "  vix proxy nginx init\n"
        << "  vix proxy nginx check\n"
        << "  vix proxy nginx reload\n";

    return 0;
  }
}
