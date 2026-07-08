/**
 * @file ModulesCommand.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Entry-point for `vix modules`. Parses arguments and dispatches to:
 *   - vix::commands::modules_cmd::commands  (subcommand handlers)
 *   - vix::commands::modules_cmd::utils     (project resolution)
 */

#include <vix/cli/commands/ModulesCommand.hpp>
#include <vix/cli/commands/modules/ModulesCommands.hpp>
#include <vix/cli/commands/modules/ModulesTypes.hpp>
#include <vix/cli/commands/modules/ModulesUtils.hpp>
#include <vix/cli/util/Ui.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace vix::commands::ModulesCommand
{

  namespace cmds = vix::commands::modules_cmd::commands;
  namespace utils = vix::commands::modules_cmd::utils;
  namespace ui = vix::cli::util;

  using vix::commands::modules_cmd::AddModuleOptions;
  using vix::commands::modules_cmd::ModuleTemplate;
  using vix::commands::modules_cmd::Options;
  using vix::commands::modules_cmd::WebSocketWorkflow;

  static WebSocketWorkflow parse_websocket_workflow(
      const std::string &value,
      bool &ok)
  {
    ok = true;

    if (value == "attached")
      return WebSocketWorkflow::Attached;

    if (value == "standalone")
      return WebSocketWorkflow::Standalone;

    if (value == "bridge")
      return WebSocketWorkflow::Bridge;

    if (value == "client")
      return WebSocketWorkflow::Client;

    ok = false;
    return WebSocketWorkflow::None;
  }

  // ------------------------------------------------------------------
  // Argument parsing
  // ------------------------------------------------------------------

  static Options parse_args(const std::vector<std::string> &args)
  {
    Options o;

    if (!args.empty())
      o.subcmd = args[0];

    auto is_opt = [](const std::string &s)
    {
      return !s.empty() && s[0] == '-';
    };

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const auto &a = args[i];

      if (a == "-h" || a == "--help")
      {
        o.showHelp = true;
      }
      else if (a == "-d" || a == "--dir")
      {
        if (i + 1 < args.size() && !is_opt(args[i + 1]))
          o.dir = args[++i];
      }
      else if (utils::starts_with(a, "--dir="))
      {
        o.dir = a.substr(std::string("--dir=").size());
      }
      else if (a == "--project")
      {
        if (i + 1 < args.size() && !is_opt(args[i + 1]))
          o.project = args[++i];
      }
      else if (utils::starts_with(a, "--project="))
      {
        o.project = a.substr(std::string("--project=").size());
      }
      else if (a == "--name")
      {
        if (i + 1 < args.size() && !is_opt(args[i + 1]))
          o.module = args[++i];
      }
      else if (utils::starts_with(a, "--name="))
      {
        o.module = a.substr(std::string("--name=").size());
      }
      else if (a == "--no-patch")
      {
        o.patchRoot = false;
      }
      else if (a == "--patch")
      {
        o.patchRoot = true;
      }
      else if (a == "--no-link")
      {
        o.patchLink = false;
      }
      else if (a == "--link")
      {
        o.patchLink = true;
      }
      else if (a == "--websocket")
      {
        o.websocket = true;

        if (o.websocketWorkflow == WebSocketWorkflow::None)
          o.websocketWorkflow = WebSocketWorkflow::Attached;
      }
      else if (a == "--workflow")
      {
        o.websocket = true;

        if (i + 1 < args.size() && !is_opt(args[i + 1]))
        {
          bool ok = false;
          const WebSocketWorkflow workflow =
              parse_websocket_workflow(args[++i], ok);

          o.websocketWorkflow = ok ? workflow : WebSocketWorkflow::None;
        }
        else
        {
          o.websocketWorkflow = WebSocketWorkflow::None;
        }
      }
      else if (utils::starts_with(a, "--workflow="))
      {
        bool ok = false;
        const std::string value =
            a.substr(std::string("--workflow=").size());

        const WebSocketWorkflow workflow =
            parse_websocket_workflow(value, ok);

        o.websocket = true;
        o.websocketWorkflow = ok ? workflow : WebSocketWorkflow::None;
      }
    }

    if (o.subcmd == "add" ||
        o.subcmd == "enable" ||
        o.subcmd == "disable")
    {
      if (o.module.empty() && args.size() >= 2 && !is_opt(args[1]))
        o.module = args[1];
    }

    return o;
  }

  // ------------------------------------------------------------------
  // run
  // ------------------------------------------------------------------

  int run(const std::vector<std::string> &args)
  {
    Options opt = parse_args(args);

    if (opt.showHelp || opt.subcmd.empty() || opt.subcmd == "help")
      return help();

    const auto root = utils::resolve_root(opt.dir);

    const std::string project = opt.project.empty()
                                    ? utils::detect_project_name(root)
                                    : opt.project;

    if (opt.subcmd == "init")
    {
      return cmds::cmd_init(root, opt.patchRoot) ? 0 : 1;
    }

    if (opt.subcmd == "add")
    {
      if (opt.module.empty())
      {
        ui::err_line(std::cout, "Missing module name.");
        ui::warn_line(std::cout, "Usage: vix modules add <name>");
        return 1;
      }

      AddModuleOptions addOptions{};
      addOptions.patchRootLink = opt.patchLink;

      if (opt.websocket)
      {
        if (opt.websocketWorkflow == WebSocketWorkflow::None)
        {
          ui::err_line(std::cout, "Invalid WebSocket workflow.");
          ui::warn_line(
              std::cout,
              "Supported workflows: attached, standalone, bridge, client.");
          return 1;
        }

        addOptions.moduleTemplate = ModuleTemplate::WebSocket;
        addOptions.websocketWorkflow = opt.websocketWorkflow;
      }

      return cmds::cmd_add(root, project, opt.module, addOptions) ? 0 : 1;
    }

    if (opt.subcmd == "enable")
    {
      if (opt.module.empty())
      {
        ui::err_line(std::cout, "Missing module name.");
        ui::warn_line(std::cout, "Usage: vix modules enable <name>");
        return 1;
      }

      return cmds::cmd_enable(root, opt.module) ? 0 : 1;
    }

    if (opt.subcmd == "disable")
    {
      if (opt.module.empty())
      {
        ui::err_line(std::cout, "Missing module name.");
        ui::warn_line(std::cout, "Usage: vix modules disable <name>");
        return 1;
      }

      return cmds::cmd_disable(root, opt.module) ? 0 : 1;
    }

    if (opt.subcmd == "check")
    {
      return cmds::cmd_check(root, project) ? 0 : 1;
    }

    if (opt.subcmd == "list")
    {
      return cmds::cmd_list(root) ? 0 : 1;
    }

    ui::err_line(std::cout, "Unknown subcommand: " + opt.subcmd);
    ui::warn_line(std::cout, "Run: vix modules --help");
    return 1;
  }

  // ------------------------------------------------------------------
  // help
  // ------------------------------------------------------------------

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix modules <subcommand> [options]\n\n";

    out << "Goal:\n";
    out << "  Enable an app-first, Go-like modules organization layer for any existing CMake project.\n";
    out << "  Modules are enforced through strict PUBLIC/PRIVATE boundaries, explicit deps, and safety checks.\n\n";

    out << "Subcommands:\n";
    out << "  init                 Initialize modules mode (creates modules/ + cmake/vix_modules.cmake)\n";
    out << "  add <name>           Create a module skeleton and a target <project>::<name>\n";
    out << "  list                 List modules declared in vix.app\n";
    out << "  enable <name>        Enable a module in vix.app\n";
    out << "  disable <name>       Disable a module in vix.app\n";
    out << "  check                Validate module safety rules (public/private + explicit deps)\n\n";

    out << "Options:\n";
    out << "  -d, --dir <path>         Project root (default: current)\n";
    out << "  --project <name>         Override project name (default: parsed from root CMakeLists.txt)\n";
    out << "  --name <name>            Module name for add/enable/disable\n";
    out << "  --no-patch               Do not patch root CMakeLists.txt (init only)\n";
    out << "  --patch                  Patch root CMakeLists.txt (default)\n";
    out << "  --no-link                Do not auto-link module target into main project target\n";
    out << "  --link                   Auto-link module target into main project target (default)\n";
    out << "  --websocket              Generate a WebSocket application module\n";
    out << "  --workflow <name>        WebSocket workflow: attached, standalone, bridge, client\n";
    out << "  -h, --help               Show help\n\n";

    out << "Contract (Go-like):\n";
    out << "  modules/<m>/include/<m>/...  (public headers)\n";
    out << "  modules/<m>/src/...          (private impl)\n";
    out << "  Public include style: #include <m/api.hpp>\n";
    out << "  Cross-module usage must be explicit: target_link_libraries(<proj>_<m> PUBLIC <proj>::<other>)\n\n";

    out << "Examples:\n";
    out << "  vix modules init\n";
    out << "  vix modules add auth\n";
    out << "  vix modules add products\n";
    out << "  vix modules add realtime --websocket\n";
    out << "  vix modules add notifications --websocket --workflow attached\n";
    out << "  vix modules add --websocket --name live_chat\n";
    out << "  vix modules list\n";
    out << "  vix modules enable auth\n";
    out << "  vix modules disable products\n";
    out << "  vix modules check\n\n";

    return 0;
  }

} // namespace vix::commands::ModulesCommand
