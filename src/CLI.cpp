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
#include <vix/cli/commands/ReplayCommand.hpp>
#include <vix/cli/commands/OrmCommand.hpp>
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
#include <vix/cli/commands/P2PCommand.hpp>
#include <vix/cli/commands/UpgradeCommand.hpp>
#include <vix/cli/commands/DoctorCommand.hpp>
#include <vix/cli/commands/UninstallCommand.hpp>
#include <vix/cli/commands/UnpublishCommand.hpp>
#include <vix/cli/commands/UpdateCommand.hpp>
#include <vix/cli/commands/OutdatedCommand.hpp>
#include <vix/cli/commands/MakeCommand.hpp>
#include <vix/cli/commands/CompletionCommand.hpp>
#include <vix/cli/commands/InfoCommand.hpp>
#include <vix/cli/commands/FmtCommand.hpp>
#include <vix/cli/commands/CleanCommand.hpp>
#include <vix/cli/commands/ResetCommand.hpp>
#include <vix/cli/commands/TaskCommand.hpp>
#include <vix/cli/commands/ServiceCommand.hpp>
#include <vix/cli/commands/ProxyCommand.hpp>
#include <vix/cli/commands/HealthCommand.hpp>
#include <vix/cli/commands/DeployCommand.hpp>
#include <vix/cli/commands/AgentCommand.hpp>
#include <vix/cli/commands/GameExportCommand.hpp>
#include <vix/utils/Env.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Logger.hpp>
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
      if (const char *env = vix::utils::vix_getenv("VIX_LOG_LEVEL"))
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

    void apply_log_level_from_flag(Logger &logger, const std::string &value)
    {
      if (auto lvl = parse_log_level(value))
      {
        logger.setLevel(*lvl);
      }
      else
      {
        print_invalid_log_level_error("--log-level", value);
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
    commands_["make"] = [](auto args)
    { return commands::MakeCommand::run(args); };
    commands_["dev"] = [](auto args)
    { return commands::DevCommand::run(args); };
    commands_["replay"] = [](auto args)
    { return commands::ReplayCommand::run(args); };
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
    commands_["cache"] = [](auto args)
    { return commands::CacheCommand::run(args); };
    commands_["info"] = [](auto args)
    { return commands::InfoCommand::run(args); };
    commands_["fmt"] = [](auto args)
    { return commands::FmtCommand::run(args); };
    commands_["clean"] = [](auto args)
    { return commands::CleanCommand::run(args); };
    commands_["reset"] = [](auto args)
    { return commands::ResetCommand::run(args); };
    commands_["task"] = [](auto args)
    { return commands::TaskCommand::run(args); };
    commands_["service"] = [](auto args)
    { return commands::ServiceCommand::run(args); };
    commands_["proxy"] = [](auto args)
    { return commands::ProxyCommand::run(args); };
    commands_["health"] = [](auto args)
    { return commands::HealthCommand::run(args); };
    commands_["deploy"] = [](auto args)
    { return commands::DeployCommand::run(args); };
    commands_["agent"] = [](auto args)
    { return commands::AgentCommand::run(args); };
    commands_["game"] = [](auto args)
    { return commands::GameCommand::run(args); };

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
      logger.logModule("CLI", Logger::Level::Info, "Hello from Vix.cpp 👋");
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
      logger.setLevel(Logger::Level::Debug);
      break;
    case VerbosityMode::Quiet:
      logger.setLevel(Logger::Level::Warn);
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
      if (cmd == "make")
        return commands::MakeCommand::help();
      if (cmd == "build")
        return commands::BuildCommand::help();
      if (cmd == "run")
        return commands::RunCommand::help();
      if (cmd == "dev")
        return commands::DevCommand::help();
      if (cmd == "replay")
        return commands::ReplayCommand::help();
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
      if (cmd == "task")
        return commands::TaskCommand::help();
      if (cmd == "service")
        return commands::ServiceCommand::help();
      if (cmd == "proxy")
        return commands::ProxyCommand::help();
      if (cmd == "health")
        return commands::HealthCommand::help();
      if (cmd == "deploy")
        return commands::DeployCommand::help();
      if (cmd == "modules")
        return commands::ModulesCommand::help();
      if (cmd == "game")
        return commands::GameCommand::help();

      if (cmd.size() > 5 && cmd.rfind("make:", 0) == 0)
        return commands::MakeCommand::help();

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
        vix::cli::util::warn_line(std::cout, "'vix deps' is deprecated, use 'vix install'");
        return commands::InstallCommand::help();
      }

      if (cmd == "p2p")
        return commands::P2PCommand::help();
      if (cmd == "orm")
        return commands::OrmCommand::help();

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

      vix::cli::util::err_line(
          std::cerr,
          "unknown help topic " + vix::cli::util::quote(cmd));

      return 1;
    }

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

    std::ostream &out = std::cout;

    auto indent = [](int level) -> std::string
    {
      return std::string(static_cast<size_t>(level) * 2, ' ');
    };

    auto docs = [&](const char *path)
    {
      out << indent(2)
          << "Docs: "
          << link(std::string("https://docs.vixcpp.com") + path)
          << "\n";
    };

    out << "Vix.cpp\n";
    out << "Fast. Simple. Built for real apps.\n";
    out << "Version: " << VIX_CLI_VERSION << "\n\n";

    out << indent(1) << "Usage:\n";
    out << indent(2) << "vix <command> [options]\n";
    out << indent(2) << "vix help [command]\n";
    out << indent(2) << "vix <file.cpp>\n";
    out << indent(2) << "vix make:<type> <name>\n\n";

    out << indent(1) << "Core workflow:\n";
    out << indent(2) << "new       Create a new project\n";
    out << indent(2) << "add       Add a dependency\n";
    out << indent(2) << "install   Install dependencies\n";
    out << indent(2) << "run       Build and run\n";
    out << indent(2) << "dev       Start development mode\n";
    out << indent(2) << "build     Build project or file\n\n";

    out << indent(1) << "Commands:\n\n";

    out << indent(2) << "Project:\n";
    docs("/cli/");
    out << indent(3) << "new <name>         Create a new Vix project\n";
    out << indent(3) << "make               Generate C++ scaffolding\n";
    out << indent(3) << "make:<type>        Shortcut for make subcommands\n";
    out << indent(3) << "build              Configure and build project\n";
    out << indent(3) << "run                Build if needed, then run\n";
    out << indent(3) << "dev                Hot reload development mode\n";
    out << indent(3) << "replay             Replay a recorded execution\n";
    out << indent(3) << "check              Validate build or script\n";
    out << indent(3) << "tests              Run tests\n";
    out << indent(3) << "test               Alias for tests\n";
    out << indent(3) << "repl               Start interactive REPL\n";
    out << indent(3) << "fmt                Format C++ source files\n";
    out << indent(3) << "clean              Remove local cache directories\n";
    out << indent(3) << "reset              Clean cache and reinstall dependencies\n";
    out << indent(3) << "task               Run reusable project tasks\n";
    out << indent(3) << "service            Install and manage a production systemd service\n";
    out << indent(3) << "proxy              Generate and validate reverse proxy configs\n";
    out << indent(3) << "health             Check local, public and WebSocket app health\n";
    out << indent(3) << "deploy             Run the production deployment workflow\n";
    out << indent(3) << "modules            Manage optional project modules\n\n";

    out << indent(2) << "Registry and dependencies:\n";
    docs("/cli/registry");
    out << indent(3) << "registry           Sync/search the registry index\n";
    out << indent(3) << "add <pkg>          Add dependency from registry\n";
    out << indent(3) << "search <query>     Search packages offline\n";
    out << indent(3) << "remove <pkg>       Remove dependency from vix.lock\n";
    out << indent(3) << "list               List project dependencies\n";
    out << indent(3) << "install            Install dependencies from vix.lock\n";
    out << indent(3) << "i                  Alias for install\n";
    out << indent(3) << "deps               Deprecated alias for install\n";
    out << indent(3) << "update             Update dependencies\n";
    out << indent(3) << "up                 Alias for update\n";
    out << indent(3) << "outdated           Check outdated dependencies\n";
    out << indent(3) << "store              Manage local store cache\n";
    out << indent(3) << "publish            Publish a package version\n";
    out << indent(3) << "unpublish          Remove a published package\n\n";

    out << indent(2) << "Packaging:\n";
    docs("/cli/pack");
    out << indent(3) << "pack               Create distributable package\n";
    out << indent(3) << "verify             Verify package integrity\n";
    out << indent(3) << "cache              Cache package into local store\n\n";

    out << indent(2) << "Runtime and advanced:\n";
    docs("/cli/commands");
    out << indent(3) << "agent              Run local AI agent commands\n";
    out << indent(3) << "game               Export and manage Vix game projects\n";
    out << indent(3) << "p2p                Run P2P node/tools\n";
    out << indent(3) << "orm                Database migrations/status/rollback\n\n";

    out << indent(2) << "System:\n";
    docs("/cli/info");
    out << indent(3) << "completion         Generate shell completion script\n";
    out << indent(3) << "upgrade            Upgrade the Vix CLI binary\n";
    out << indent(3) << "info               Show environment and cache locations\n";
    out << indent(3) << "doctor             Check toolchain and install health\n";
    out << indent(3) << "uninstall          Remove Vix CLI from the system\n\n";

    out << indent(2) << "Help:\n";
    out << indent(3) << "help [command]     Show command help\n";
    out << indent(3) << "version            Show version\n";
    out << indent(3) << "-h, --help         Show help\n";
    out << indent(3) << "-v, --version      Show version\n\n";

    out << indent(1) << "Global options:\n";
    out << indent(2) << "--verbose          Enable debug logs\n";
    out << indent(2) << "-q, --quiet        Only warnings/errors\n";
    out << indent(2) << "--log-level        trace|debug|info|warn|error|critical\n\n";

    out << indent(1) << "Examples:\n";
    out << indent(2) << "vix new hello\n";
    out << indent(2) << "vix run main.cpp\n";
    out << indent(2) << "vix build main.cpp --out app\n";
    out << indent(2) << "vix make:class User\n";
    out << indent(2) << "vix add @cnerium/app\n";
    out << indent(2) << "vix install\n";
    out << indent(2) << "vix game export\n";
    out << indent(2) << "vix agent ask \"Explain Vix.cpp\" --model qwen2.5-coder:1.5b --timeout 120000\n";
    out << indent(2) << "vix help run\n\n";

    out << indent(1) << "Links:\n";
    out << indent(2) << "Docs:     " << link("https://docs.vixcpp.com") << "\n";
    out << indent(2) << "CLI:      " << link("https://docs.vixcpp.com/cli/") << "\n";
    out << indent(2) << "Registry: " << link("https://registry.vixcpp.com") << "\n";
    out << indent(2) << "GitHub:   " << link("https://github.com/vixcpp/vix") << "\n\n";

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
