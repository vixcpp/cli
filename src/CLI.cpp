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
#include <vix/cli/commands/Dispatch.hpp>
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
      if (ext == ".cpp")
      {
        args.insert(args.begin(), cmd);
        cmd = "run";
      }
    }

    if (cmd == "help")
      return help(args);

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

    out << "Project:\n";
    out << "  new <name>                 Create a minimal native Vix project\n";
    out << "  init                       Adopt the current directory as a Vix project\n";
    out << "  build                      Build the project\n";
    out << "  run                        Build and run the project\n";
    out << "  test                       Run registered project tests\n";
    out << "  dev                        Run while watching for changes\n";
    out << "  repl                       Start the C++ REPL\n\n";

    out << "Packages:\n";
    out << "  add <package-or-url>       Add a package dependency\n";
    out << "  remove <package-or-url>    Remove a root package dependency\n";
    out << "  update                     Update all package dependencies\n";
    out << "  install                    Install locked package dependencies\n";
    out << "  publish                    Publish the current Registry package\n\n";

    out << "Global options:\n";
    out << "  --verbose                  Enable debug output\n";
    out << "  -q, --quiet                Only show warnings and errors\n";
    out << "  --log-level <level>        Set trace, debug, info, warn, error or critical\n";
    out << "  -h, --help                 Show command help\n";
    out << "  -v, --version              Show the installed version\n\n";

    out << "Run 'vix help <command>' for detailed command usage.\n";
    out << "Documentation: "
        << link("https://vixcpp.com/docs")
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
