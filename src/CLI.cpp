/**
 *
 *  @file CLI.cpp
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

#include <vix/cli/CLI.hpp>
#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/InitCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/PackCommand.hpp>
#include <vix/cli/commands/VerifyCommand.hpp>
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/ReplCommand.hpp>
#include <vix/cli/commands/Dispatch.hpp>
#include <vix/cli/commands/CacheCommand.hpp>
#include <vix/cli/commands/RegistryCommand.hpp>
#include <vix/cli/commands/AddCommand.hpp>
#include <vix/cli/commands/SearchCommand.hpp>
#include <vix/cli/commands/RemoveCommand.hpp>
#include <vix/cli/commands/ListCommand.hpp>
#include <vix/cli/commands/StoreCommand.hpp>
#include <vix/cli/commands/PublishCommand.hpp>
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/commands/ModulesCommand.hpp>
#include <vix/cli/commands/UpgradeCommand.hpp>
#include <vix/cli/commands/DoctorCommand.hpp>
#include <vix/cli/commands/UninstallCommand.hpp>
#include <vix/cli/commands/UnpublishCommand.hpp>
#include <vix/cli/commands/UpdateCommand.hpp>
#include <vix/cli/commands/OutdatedCommand.hpp>
#include <vix/cli/commands/CompletionCommand.hpp>
#include <vix/cli/commands/InfoCommand.hpp>
#include <vix/cli/commands/FmtCommand.hpp>
#include <vix/cli/commands/CleanCommand.hpp>
#include <vix/cli/commands/ResetCommand.hpp>
#include <vix/env/Legacy.hpp>
#include <vix/cli/Style.hpp>
#include <vix/log/Logger.hpp>
#include <vix/cli/util/Ui.hpp>

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <climits>

namespace vix
{
  using Logger = vix::log::Logger;
  using namespace vix::cli::style;

  namespace
  {
    std::string to_lower_copy(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    std::optional<Logger::Level> parse_log_level(const std::string &raw)
    {
      const std::string s = to_lower_copy(raw);

      if (s == "trace")
        return Logger::Level::Trace;
      if (s == "debug")
        return Logger::Level::Debug;
      if (s == "info")
        return Logger::Level::Info;
      if (s == "warn" || s == "warning")
        return Logger::Level::Warn;
      if (s == "error" || s == "err")
        return Logger::Level::Error;
      if (s == "critical" || s == "fatal")
        return Logger::Level::Critical;

      return std::nullopt;
    }

    static void print_invalid_log_level_error(
        const std::string &source,
        const std::string &value)
    {
      std::cerr << PAD
                << RED
                << BOLD
                << "✖ Invalid log level"
                << RESET
                << "\n\n";

      std::cerr << PAD
                << CYAN
                << "source:"
                << RESET
                << "\n"
                << "    "
                << source
                << "\n\n";

      std::cerr << PAD
                << CYAN
                << "value:"
                << RESET
                << "\n"
                << "    "
                << value
                << "\n\n";

      std::cerr << PAD
                << CYAN
                << "expected:"
                << RESET
                << "\n"
                << "    trace, debug, info, warn, error, critical"
                << "\n\n";

      std::cerr << PAD
                << YELLOW
                << "hint:"
                << RESET
                << "\n";

      if (to_lower_copy(value) == "release")
      {
        std::cerr << "    To build in release mode, use: vix build --preset release\n";
      }
      else
      {
        std::cerr << "    Example: VIX_LOG_LEVEL=debug vix build -v\n";
      }
    }

    static std::size_t levenshtein_distance(const std::string &a, const std::string &b)
    {
      const std::size_t m = a.size();
      const std::size_t n = b.size();

      std::vector<std::size_t> prev(n + 1), curr(n + 1);

      for (std::size_t j = 0; j <= n; ++j)
        prev[j] = j;

      for (std::size_t i = 1; i <= m; ++i)
      {
        curr[0] = i;
        for (std::size_t j = 1; j <= n; ++j)
        {
          const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0u : 1u;

          curr[j] = std::min({prev[j] + 1u,
                              curr[j - 1] + 1u,
                              prev[j - 1] + cost});
        }
        prev = curr;
      }

      return prev[n];
    }

    static std::optional<std::string> find_closest_command(
        const std::string &input,
        const std::unordered_map<std::string, vix::cli::dispatch::Entry> &entries)
    {
      std::size_t bestScore = std::numeric_limits<std::size_t>::max();
      std::string best;

      for (const auto &[name, _] : entries)
      {
        const std::size_t d = levenshtein_distance(input, name);

        if (d < bestScore)
        {
          bestScore = d;
          best = name;
        }
      }

      if (bestScore <= 3)
        return best;

      return std::nullopt;
    }

    void apply_log_level_from_env(Logger &logger)
    {
      if (const char *env = vix::env::legacy::getenv("VIX_LOG_LEVEL"))
      {
        std::string value(env);

        if (auto lvl = parse_log_level(value))
        {
          logger.setLevel(*lvl);
        }
        else
        {
          print_invalid_log_level_error("VIX_LOG_LEVEL", value);
        }
      }
    }

    bool apply_log_level_from_flag(Logger &logger, const std::string &value)
    {
      if (auto lvl = parse_log_level(value))
      {
        logger.setLevel(*lvl);
        return true;
      }

      print_invalid_log_level_error("--log-level", value);
      return false;
    }

  } // namespace

  CLI::CLI()
  {
  }

  int CLI::run(int argc, char **argv)
  {
#ifndef _WIN32
    setenv("VIX_CLI_PATH", argv[0], 1);
#else
    _putenv_s("VIX_CLI_PATH", argv[0]);
#endif

    auto &logger = Logger::getInstance();
    auto &dispatcher = vix::cli::dispatch::global();

    apply_log_level_from_env(logger);

    if (argc < 2)
      return dispatcher.run("repl", {});

    enum class VerbosityMode
    {
      Default,
      Verbose,
      Quiet
    };
    VerbosityMode verbosity = VerbosityMode::Default;
    std::optional<std::string> logLevelFlag;
    bool globalHelp = false;
    bool globalVersion = false;
    int index = 1;

    while (index < argc)
    {
      std::string arg = argv[index];

      if (arg == "-h" || arg == "--help")
      {
        globalHelp = true;
        ++index;
        break;
      }

      if (arg == "-v" || arg == "--version")
      {
        globalVersion = true;
        ++index;
        break;
      }

      if (arg == "--verbose")
      {
        verbosity = VerbosityMode::Verbose;
        ++index;
        continue;
      }

      if (arg == "--quiet" || arg == "-q")
      {
        verbosity = VerbosityMode::Quiet;
        ++index;
        continue;
      }

      if (arg == "--log-level")
      {
        if (index + 1 >= argc)
        {
          std::cerr << "vix: --log-level requires a value (trace|debug|info|warn|error|critical).\n";
          return 1;
        }
        logLevelFlag = argv[index + 1];
        index += 2;
        continue;
      }

      constexpr const char prefix[] = "--log-level=";
      if (arg.rfind(prefix, 0) == 0)
      {
        std::string value = arg.substr(sizeof(prefix) - 1);
        if (value.empty())
        {
          std::cerr << "vix: --log-level=VALUE cannot be empty.\n";
          return 1;
        }
        logLevelFlag = value;
        ++index;
        continue;
      }

      break;
    }

    switch (verbosity)
    {
    case VerbosityMode::Verbose:
      logger.setLevel(Logger::Level::Debug);
      break;
    case VerbosityMode::Quiet:
      logger.setLevel(Logger::Level::Warn);
      break;
    default:
      break;
    }

    if (logLevelFlag.has_value() && !apply_log_level_from_flag(logger, *logLevelFlag))
      return 1;

    if (globalHelp)
      return help({});

    if (globalVersion)
      return version({});

    if (index >= argc)
      return dispatcher.run("repl", {});

    std::string cmd = argv[index];
    std::vector<std::string> args(argv + index + 1, argv + argc);

    {
      std::filesystem::path p{cmd};
      const auto ext = p.extension().string();
      if (ext == ".vix" || ext == ".cpp")
      {
        args.insert(args.begin(), cmd);
        cmd = "run";
      }
    }

    if (cmd == "help")
      return help(args);

    if (cmd == "version")
      return version(args);

    if (!args.empty() && (args[0] == "--help" || args[0] == "-h"))
    {
      if (!dispatcher.has(cmd))
      {
        vix::cli::util::err_line(
            std::cerr,
            "unrecognized subcommand " + vix::cli::util::quote(cmd));

        auto suggestion = find_closest_command(cmd, dispatcher.entries());

        if (suggestion.has_value())
        {
          vix::cli::util::tip_line(
              std::cerr,
              "A similar command exists: " + vix::cli::util::quote(suggestion.value()));
        }

        return 1;
      }
      return dispatcher.help(cmd);
    }

    if (!args.empty() && args[0] == "unpublish")
    {
      // Usage:
      //   vix registry unpublish <namespace/name> [-y|--yes]
      // On forward sans le mot "unpublish"
      std::vector<std::string> rest(args.begin() + 1, args.end());
      return vix::commands::UnpublishCommand{}.run(rest);
    }

    if (!dispatcher.has(cmd))
    {
      vix::cli::util::err_line(
          std::cerr,
          "unrecognized subcommand " + vix::cli::util::quote(cmd));

      auto suggestion = find_closest_command(cmd, dispatcher.entries());

      if (suggestion.has_value())
      {
        vix::cli::util::tip_line(
            std::cerr,
            "A similar command exists: " + vix::cli::util::quote(suggestion.value()));
      }

      return 1;
    }

    try
    {
      return dispatcher.run(cmd, args);
    }
    catch (const std::exception &ex)
    {
      logger.logModule("CLI", Logger::Level::Error,
                       "Command '{}' failed: {}", cmd, ex.what());
      return 1;
    }
  }

  int CLI::help(const std::vector<std::string> &args)
  {
    // Per-command help: vix help <command>
    if (!args.empty())
    {
      const std::string &cmd = args[0];

      if (cmd == "new")
        return commands::NewCommand::help();
      if (cmd == "init")
        return commands::InitCommand::help();
      if (cmd == "build")
        return commands::BuildCommand::help();
      if (cmd == "run")
        return commands::RunCommand::help();
      if (cmd == "dev")
        return commands::DevCommand::help();
      if (cmd == "check")
        return commands::CheckCommand::help();
      if (cmd == "tests" || cmd == "test")
        return commands::TestsCommand::help();
      if (cmd == "repl")
        return commands::ReplCommand::help();

      if (cmd == "fmt")
        return commands::FmtCommand::help();
      if (cmd == "clean")
        return commands::CleanCommand::help();
      if (cmd == "reset")
        return commands::ResetCommand::help();
      if (cmd == "modules")
        return commands::ModulesCommand::help();

      if (cmd == "login" || cmd == "logout")
        return vix::cli::dispatch::global().help(cmd);

      if (cmd == "pack")
        return commands::PackCommand::help();
      if (cmd == "verify")
        return commands::VerifyCommand::help();
      if (cmd == "cache")
        return commands::CacheCommand::help();

      if (cmd == "registry")
        return commands::RegistryCommand::help();
      if (cmd == "add")
        return commands::AddCommand::help();
      if (cmd == "search")
        return commands::SearchCommand::help();
      if (cmd == "remove")
        return commands::RemoveCommand::help();
      if (cmd == "list")
        return commands::ListCommand::help();
      if (cmd == "update" || cmd == "up")
        return commands::UpdateCommand::help();
      if (cmd == "outdated")
        return commands::OutdatedCommand::help();
      if (cmd == "store")
        return commands::StoreCommand::help();
      if (cmd == "publish")
        return commands::PublishCommand::help();
      if (cmd == "unpublish")
        return commands::UnpublishCommand{}.help();
      if (cmd == "install" || cmd == "i")
        return commands::InstallCommand::help();

      if (cmd == "deps")
      {
        vix::cli::util::warn_line(
            std::cout,
            "'vix deps' is deprecated, use 'vix install'");

        return commands::InstallCommand::help();
      }

      if (cmd == "completion")
        return commands::CompletionCommand::help();
      if (cmd == "upgrade")
        return commands::UpgradeCommand::help();
      if (cmd == "info")
        return commands::InfoCommand::help();
      if (cmd == "doctor")
        return commands::DoctorCommand::help();
      if (cmd == "uninstall")
        return commands::UninstallCommand::help();

      if (vix::cli::dispatch::global().has(cmd))
        return vix::cli::dispatch::global().help(cmd);

      vix::cli::util::err_line(
          std::cerr,
          "unknown help topic " + vix::cli::util::quote(cmd));

      return 1;
    }

    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix <command> [options]\n";
    out << "  vix <file.cpp>\n";
    out << "  vix help <command>\n\n";

    out << "Common commands:\n";
    out << "  new <name>                 Create a new Vix project\n";
    out << "  init                       Initialize the current directory\n";
    out << "  add <package>              Add a dependency\n";
    out << "  install                    Install project dependencies\n";
    out << "  run                        Build and run a project or C++ file\n";
    out << "  dev                        Start development mode\n";
    out << "  build                      Configure and build\n";
    out << "  targets                    List configured CMake targets\n";
    out << "  test                       Run project tests\n";

    out << "Project:\n";
    out << "  check                      Validate a project or source file\n";
    out << "  repl                       Start the interactive REPL\n";
    out << "  fmt                        Format C++ source files\n";
    out << "  clean                      Remove local build caches\n";
    out << "  reset                      Reset caches and dependencies\n";
    out << "  modules                    Manage optional project modules\n\n";

    out << "Dependencies and registry:\n";
    out << "  login                      Authenticate with the Vix account service\n";
    out << "  logout                     End the Vix account session\n";
    out << "  registry                   Manage the registry index\n";
    out << "  search <query>             Search registry packages\n";
    out << "  list                       List project dependencies\n";
    out << "  remove <package>           Remove a dependency\n";
    out << "  update                     Update dependencies\n";
    out << "  outdated                   Check outdated dependencies\n";
    out << "  store                      Manage the local package store\n";
    out << "  publish                    Publish a package version\n";
    out << "  unpublish                  Remove a published version\n\n";

    out << "Packaging:\n";
    out << "  pack                       Create a distributable package\n";
    out << "  verify                     Verify package integrity\n";
    out << "  cache                      Cache a package locally\n\n";

    out << "System:\n";
    out << "  doctor                     Check the installation and toolchain\n";
    out << "  info                       Show environment information\n";
    out << "  completion                 Generate shell completions\n";
    out << "  upgrade                    Upgrade the Vix CLI\n";
    out << "  uninstall                  Uninstall the Vix CLI\n\n";

    out << "Global options:\n";
    out << "  --verbose                  Enable debug output\n";
    out << "  -q, --quiet                Only show warnings and errors\n";
    out << "  --log-level <level>        Set trace, debug, info, warn, error or critical\n";
    out << "  -h, --help                 Show command help\n";
    out << "  -v, --version              Show the installed version\n\n";

    out << "Run 'vix help <command>' for detailed command usage.\n";
    out << "Documentation: "
        << link("https://docs.vixcpp.com/cli/")
        << "\n";

    return 0;
  }

  int CLI::version(const std::vector<std::string> &)
  {
    using namespace vix::cli::style;

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

    std::ostream &out = std::cout;

    section_title(out, "Vix.cpp CLI");

    out << "  version : "
        << CYAN << VIX_CLI_VERSION << RESET << "\n";

    out << "  author  : "
        << "Gaspard Kirira\n";

    out << "  source  : "
        << link("https://github.com/vixcpp/vix") << "\n\n";

    return 0;
  }

} // namespace vix
