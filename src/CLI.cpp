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
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/OrmCommand.hpp>
#include <vix/cli/commands/PackCommand.hpp>
#include <vix/cli/commands/VerifyCommand.hpp>
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/ReplCommand.hpp>
#include <vix/cli/commands/Dispatch.hpp>
#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/commands/RegistryCommand.hpp>
#include <vix/cli/commands/AddCommand.hpp>
#include <vix/cli/commands/SearchCommand.hpp>
#include <vix/cli/commands/RemoveCommand.hpp>
#include <vix/cli/commands/ListCommand.hpp>
#include <vix/cli/commands/StoreCommand.hpp>
#include <vix/cli/commands/PublishCommand.hpp>
#include <vix/cli/commands/DepsCommand.hpp>
#include <vix/cli/commands/ModulesCommand.hpp>
#include <vix/cli/commands/P2PCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/utils/Logger.hpp>

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace vix
{
  using Logger = vix::utils::Logger;
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
        return Logger::Level::TRACE;
      if (s == "debug")
        return Logger::Level::DEBUG;
      if (s == "info")
        return Logger::Level::INFO;
      if (s == "warn" || s == "warning")
        return Logger::Level::WARN;
      if (s == "error" || s == "err")
        return Logger::Level::ERROR;
      if (s == "critical" || s == "fatal")
        return Logger::Level::CRITICAL;

      return std::nullopt;
    }

    void apply_log_level_from_env(Logger &logger)
    {
      if (const char *env = std::getenv("VIX_LOG_LEVEL"))
      {
        std::string value(env);
        if (auto lvl = parse_log_level(value))
        {
          logger.setLevel(*lvl);
        }
        else
        {
          std::cerr << "vix: invalid VIX_LOG_LEVEL value '" << value
                    << "'. Expected one of: trace, debug, info, warn, error, critical.\n";
        }
      }
    }

    void apply_log_level_from_flag(Logger &logger, const std::string &value)
    {
      if (auto lvl = parse_log_level(value))
      {
        logger.setLevel(*lvl);
      }
      else
      {
        std::cerr << "vix: invalid --log-level value '" << value
                  << "'. Expected one of: trace, debug, info, warn, error, critical.\n";
      }
    }

  } // namespace

  CLI::CLI()
  {
    // Base commands
    commands_["help"] = [this](auto args)
    { return help(args); };
    commands_["version"] = [this](auto args)
    { return version(args); };

    commands_["new"] = [](auto args)
    { return commands::NewCommand::run(args); };
    commands_["run"] = [](auto args)
    { return commands::RunCommand::run(args); };
    commands_["build"] = [](auto args)
    { return commands::BuildCommand::run(args); };
    commands_["dev"] = [](auto args)
    { return commands::DevCommand::run(args); };
    commands_["orm"] = [](auto args)
    { return commands::OrmCommand::run(args); };
    commands_["pack"] = [](auto args)
    { return commands::PackCommand::run(args); };
    commands_["verify"] = [](auto args)
    { return commands::VerifyCommand::run(args); };
    commands_["check"] = [](auto args)
    { return commands::CheckCommand::run(args); };
    commands_["tests"] = [](auto args)
    { return commands::TestsCommand::run(args); };
    commands_["test"] = [](auto args)
    { return commands::TestsCommand::run(args); };
    commands_["repl"] = [](auto args)
    { return commands::ReplCommand::run(args); };
    commands_["install"] = [](auto args)
    { return commands::InstallCommand::run(args); };

    commands_["-h"] = [this](auto args)
    { return help(args); };
    commands_["--help"] = [this](auto args)
    { return help(args); };
    commands_["-v"] = [this](auto args)
    { return version(args); };
    commands_["--version"] = [this](auto args)
    { return version(args); };

    commands_["hello"] = [](auto)
    {
      auto &logger = Logger::getInstance();
      logger.logModule("CLI", Logger::Level::INFO, "Hello from Vix.cpp 👋");
      return 0;
    };
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
    int index = 1;

    while (index < argc)
    {
      std::string arg = argv[index];

      if (arg == "-h" || arg == "--help")
        return help({});

      if (arg == "-v" || arg == "--version")
        return version({});

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
      logger.setLevel(Logger::Level::DEBUG);
      break;
    case VerbosityMode::Quiet:
      logger.setLevel(Logger::Level::WARN);
      break;
    default:
      break;
    }

    if (logLevelFlag.has_value())
      apply_log_level_from_flag(logger, *logLevelFlag);

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

    if (!args.empty() && (args[0] == "--help" || args[0] == "-h"))
    {
      if (!dispatcher.has(cmd))
      {
        std::cerr << "vix: unknown command '" << cmd << "'\n\n";
        return help({});
      }
      return dispatcher.help(cmd);
    }

    if (!dispatcher.has(cmd))
    {
      std::cerr << "vix: unknown command '" << cmd << "'\n\n";
      help({});
      return 1;
    }

    try
    {
      return dispatcher.run(cmd, args);
    }
    catch (const std::exception &ex)
    {
      logger.logModule("CLI", Logger::Level::ERROR,
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
      if (cmd == "build")
        return commands::BuildCommand::help();
      if (cmd == "run")
        return commands::RunCommand::help();
      if (cmd == "dev")
        return commands::DevCommand::help();
      if (cmd == "orm")
        return commands::OrmCommand::help();
      if (cmd == "pack")
        return commands::PackCommand::help();
      if (cmd == "verify")
        return commands::VerifyCommand::help();
      if (cmd == "check")
        return commands::CheckCommand::help();
      if (cmd == "tests" || cmd == "test")
        return commands::TestsCommand::help();
      if (cmd == "repl")
        return commands::ReplCommand::help();
      if (cmd == "install")
        return commands::InstallCommand::help();
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
      if (cmd == "store")
        return commands::StoreCommand::help();
      if (cmd == "publish")
        return commands::PublishCommand::help();
      if (cmd == "deps")
        return commands::DepsCommand::help();
      if (cmd == "modules")
        return commands::ModulesCommand::help();
      if (cmd == "p2p")
        return commands::P2PCommand::help();
    }

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

    std::ostream &out = std::cout;

    // Global padding helpers (2 spaces per level)
    auto indent = [](int level) -> std::string
    {
      return std::string(static_cast<size_t>(level) * 2, ' ');
    };

    out << "Vix.cpp — Modern C++ backend runtime\n";
    out << "Version: " << VIX_CLI_VERSION << "\n\n";

    out << indent(1) << "Usage:\n";
    out << indent(2) << "vix <command> [options] [args...]\n";
    out << indent(2) << "vix help <command>\n\n";

    out << indent(1) << "Quick start:\n";
    out << indent(2) << "vix new api\n";
    out << indent(2) << "cd api && vix dev\n";
    out << indent(2) << "vix pack --version 1.0.0 && vix verify\n\n";

    out << indent(1) << "Commands:\n";

    out << indent(2) << "Project:\n";
    out << indent(3) << "new <name>               Create a new Vix project in ./<name>\n";
    out << indent(3) << "build [name]             Configure + build (root project or app)\n";
    out << indent(3) << "run   [name] [--args]    Build (if needed) then run\n";
    out << indent(3) << "dev   [name]             Dev mode (watch, rebuild, reload)\n";
    out << indent(3) << "check [path]             Validate a project or compile a single .cpp (no execution)\n";
    out << indent(3) << "tests [path]             Run project tests (alias of check --tests)\n";
    out << indent(3) << "repl                      Start interactive Vix REPL\n\n";

    out << indent(2) << "Project structure:\n";
    out << indent(3) << "modules <subcommand>      Opt-in module system (init/add/check)\n\n";

    out << indent(2) << "Network:\n";
    out << indent(3) << "p2p [options]            Run P2P node (tcp + discovery + bootstrap)\n\n";

    out << indent(2) << "Registry:\n";
    out << indent(3) << "registry <subcommand>     Sync/search registry index (git-based)\n";
    out << indent(3) << "add <pkg>@<version>       Add a dependency from registry (pins commit)\n";
    out << indent(3) << "search <query>            Search packages in local registry index (offline)\n";
    out << indent(3) << "remove <pkg>             Remove a dependency from vix.lock\n";
    out << indent(3) << "list                     List project dependencies from vix.lock\n";
    out << indent(3) << "store <subcommand>       Manage local store cache (gc/path)\n";
    out << indent(3) << "publish <version>         Publish current repo to registry (JSON + PR)\n";
    out << indent(3) << "deps                     Install deps from vix.lock (generate .vix/vix_deps.cmake)\n\n";

    out << indent(2) << "Packaging & security:\n";
    out << indent(3) << "pack   [options]         Create dist/<name>@<version> (+ optional .vixpkg)\n";
    out << indent(3) << "verify [options]         Verify dist/<name>@<version> or a .vixpkg artifact\n";
    out << indent(3) << "install [options]        Install dist/<name>@<version> or a .vixpkg into the local store\n\n";

    out << indent(2) << "Database (ORM):\n";
    out << indent(3) << "orm <subcommand>         Migrations/status/rollback\n\n";

    out << indent(2) << "Info:\n";
    out << indent(3) << "help [command]           Show help for CLI or a specific command\n";
    out << indent(3) << "version                  Show version information\n\n";

    out << indent(1) << "Global options:\n";
    out << indent(2) << "--verbose                Enable debug logs (equivalent to --log-level debug)\n";
    out << indent(2) << "-q, --quiet              Only show warnings and errors\n";
    out << indent(2) << "--log-level <level>      trace|debug|info|warn|error|critical\n";
    out << indent(2) << "-h, --help               Show CLI help (or: vix help)\n";
    out << indent(2) << "-v, --version            Show version info\n\n";

    section_title(out, "Links:");
    out << indent(2) << "GitHub: " << link("https://github.com/vixcpp/vix") << "\n\n";

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
