/**
 *
 *  @file DevCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <cstdlib>
#ifdef _WIN32
#include <stdlib.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::DevCommand
{
  namespace
  {
    bool has_watch_flag(const std::vector<std::string> &args)
    {
      return std::any_of(
          args.begin(), args.end(),
          [](const std::string &s)
          {
            return s == "--watch" || s == "--reload";
          });
    }

  } // namespace

  int run(const std::vector<std::string> &args)
  {
    std::vector<std::string> forwarded;
    forwarded.reserve(args.size() + 2);

    forwarded.insert(forwarded.end(), args.begin(), args.end());

    if (!has_watch_flag(forwarded))
      forwarded.emplace_back("--watch");

    forwarded.emplace_back("--dev-mode");

    return vix::commands::RunCommand::run(forwarded);
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix dev [target] [options] [-- compiler/linker flags] [--run <args...>]\n\n";

    out << "Description:\n";
    out << "  Start a C++ project or script in development mode.\n";
    out << "  vix dev builds the target, runs it when possible, watches file changes,\n";
    out << "  and rebuilds or restarts automatically.\n\n";

    out << "Targets:\n";
    out << "  current project             vix dev\n";
    out << "  project directory/name      vix dev api\n";
    out << "  single C++ file             vix dev server.cpp\n";
    out << "  manifest file               vix dev app.vix\n\n";

    out << "Project behavior:\n";
    out << "  application project         build, run, watch, restart on changes\n";
    out << "  library project             build, watch, rebuild on changes\n";
    out << "  single C++ file             compile, run, watch, rebuild on save\n\n";

    out << "Project options:\n";
    out << "  -d, --dir <path>            Project directory\n";
    out << "  --dir=<path>                Same as --dir <path>\n";
    out << "  --preset <name>             Configure/build preset, default: dev-ninja\n";
    out << "  --preset=<name>             Same as --preset <name>\n";
    out << "  --run-preset <name>         Run preset name\n";
    out << "  --run-preset=<name>         Same as --run-preset <name>\n";
    out << "  -j, --jobs <n>              Number of parallel build jobs\n";
    out << "  --jobs=<n>                  Same as --jobs <n>\n";
    out << "  --clean                     Clean/reconfigure before starting dev mode\n";
    out << "  --replay                    Record runs under .vix/runs/\n\n";

    out << "Runtime arguments and environment:\n";
    out << "  --cwd <path>                Run the program from this working directory\n";
    out << "  --cwd=<path>                Same as --cwd <path>\n";
    out << "  --env <K=V>                 Add or override one environment variable\n";
    out << "  --env=<K=V>                 Same as --env <K=V>\n";
    out << "  --args <value>              Add one runtime argument, repeatable\n";
    out << "  --args=<value>              Same as --args <value>\n";
    out << "  --run <args...>             Runtime args for script mode\n\n";

    out << "Watch mode:\n";
    out << "  --watch                     Enabled by default in vix dev\n";
    out << "  --reload                    Alias for --watch\n";
    out << "  --force-server              Treat the program as a long-running server\n";
    out << "  --force-script              Treat the program as a short-lived script\n\n";

    out << "Script mode:\n";
    out << "  --auto-deps                 Auto-add includes from .vix/deps/*/include\n";
    out << "  --auto-deps=local           Same as --auto-deps\n";
    out << "  --auto-deps=up              Search deps in parent folders too\n";
    out << "  --san                       Enable ASan and UBSan\n";
    out << "  --ubsan                     Enable UBSan only\n";
    out << "  --tsan                      Enable ThreadSanitizer only\n";
    out << "  --with-sqlite               Enable SQLite support\n";
    out << "  --with-mysql                Enable MySQL support\n";
    out << "  --local-cache               Use local .vix-scripts cache\n\n";

    out << "Documentation:\n";
    out << "  --docs                      Enable OpenAPI/docs for this run\n";
    out << "  --no-docs                   Disable OpenAPI/docs for this run\n";
    out << "  --docs=<0|1|true|false>     Explicitly control OpenAPI/docs\n\n";

    out << "Output and logging:\n";
    out << "  --clear <auto|always|never> Clear terminal before runtime output\n";
    out << "  --clear=<auto|always|never> Same as --clear <mode>\n";
    out << "  --no-clear                  Alias for --clear=never\n";
    out << "  --log-level <level>         trace, debug, info, warn, error, critical, off\n";
    out << "  --log-level=<level>         Same as --log-level <level>\n";
    out << "  --verbose                   Alias for --log-level=debug\n";
    out << "  -q, --quiet                 Alias for --log-level=warn\n";
    out << "  --log-format <format>       kv, json, json-pretty\n";
    out << "  --log-format=<format>       Same as --log-format <format>\n";
    out << "  --log-color <mode>          auto, always, never\n";
    out << "  --log-color=<mode>          Same as --log-color <mode>\n";
    out << "  --no-color                  Alias for --log-color=never\n";
    out << "  -h, --help                  Show this help\n\n";

    out << "Compiler/linker flags:\n";
    out << "  -- [flags...]               In script mode, pass flags to the compiler\n";
    out << "                              Example: vix dev server.cpp -- -O2 -lssl\n\n";

    out << "Important:\n";
    out << "  vix dev already enables watch mode.\n";
    out << "  For script runtime args, use --run or --args.\n";
    out << "  Everything after -- is treated as compiler/linker flags.\n\n";

    out << "Examples:\n";
    out << "  vix dev\n";
    out << "  vix dev api\n";
    out << "  vix dev --dir ./examples/blog\n";
    out << "  vix dev --preset dev-ninja\n";
    out << "  vix dev --clean\n";
    out << "  vix dev --reload\n";
    out << "  vix dev --force-server api\n";
    out << "  vix dev --force-script tool.cpp\n";
    out << "  vix dev api --args --port --args 8080\n";
    out << "  vix dev api --cwd ./runtime\n";
    out << "  vix dev api --env PORT=8080\n";
    out << "  vix dev main.cpp\n";
    out << "  vix dev main.cpp --run hello 123\n";
    out << "  vix dev main.cpp --args hello --args 123\n";
    out << "  vix dev main.cpp --with-sqlite\n";
    out << "  vix dev main.cpp --with-mysql\n";
    out << "  vix dev main.cpp --san\n";
    out << "  vix dev main.cpp -- -O2 -DNDEBUG\n";
    out << "  vix dev app.vix\n";
    out << "  vix dev app.vix --args --port --args 8080\n\n";

    out << "Environment variables:\n";
    out << "  VIX_DOCS                    0 or 1\n";
    out << "  VIX_LOG_LEVEL               trace, debug, info, warn, error, critical, off\n";
    out << "  VIX_LOG_FORMAT              kv, json, json-pretty\n";
    out << "  VIX_COLOR                   auto, always, never\n";
    out << "  VIX_STDOUT_MODE             line\n";
    out << "  VIX_CLI_CLEAR               auto, always, never\n";
    out << "  VIX_SHOW_ENV_HINT=1         Show .env hint when .env.example exists\n\n";

    return 0;
  }

} // namespace vix::commands::DevCommand
