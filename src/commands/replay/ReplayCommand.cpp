/**
 *
 *  @file ReplayCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/ReplayCommand.hpp>
#include <vix/cli/commands/replay/ReplayId.hpp>
#include <vix/cli/commands/replay/ReplayList.hpp>
#include <vix/cli/commands/replay/ReplayPrinter.hpp>
#include <vix/cli/commands/replay/ReplayRunner.hpp>
#include <vix/cli/commands/replay/ReplayStore.hpp>
#include <vix/cli/Style.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace vix::cli::style;

namespace vix::commands::ReplayCommand
{

  namespace replay = vix::commands::replay;
  namespace fs = std::filesystem;

  namespace
  {

    /**
     * @brief Return true when an argument asks for help.
     *
     * @param arg CLI argument.
     * @return true when help was requested.
     */
    bool is_help_arg(const std::string &arg)
    {
      return arg == "-h" || arg == "--help" || arg == "help";
    }

    /**
     * @brief Return true when an argument is a known option.
     *
     * @param arg CLI argument.
     * @return true when the argument starts with '-'.
     */
    bool is_option(const std::string &arg)
    {
      return !arg.empty() && arg.front() == '-';
    }

    /**
     * @brief Read the value following an option.
     *
     * @param args CLI arguments.
     * @param index Current argument index.
     * @param option Option name.
     * @param value Output value.
     * @return true when a value was read.
     */
    bool take_option_value(
        const std::vector<std::string> &args,
        std::size_t &index,
        const std::string &option,
        std::string &value)
    {
      if (index + 1 >= args.size())
      {
        std::cerr << PAD << RED << "✖" << RESET
                  << " Missing value for " << option << "\n";
        return false;
      }

      value = args[++index];
      return true;
    }

    /**
     * @brief Parse --dir and --dir=<path>.
     *
     * @param args CLI arguments.
     * @param index Current argument index.
     * @param baseDir Output base directory.
     * @return true when parsing succeeded.
     */
    bool parse_dir_option(
        const std::vector<std::string> &args,
        std::size_t &index,
        fs::path &baseDir)
    {
      const std::string &arg = args[index];

      if (arg == "--dir" || arg == "--cwd")
      {
        std::string value;
        if (!take_option_value(args, index, arg, value))
          return false;

        baseDir = value;
        return true;
      }

      constexpr const char dirPrefix[] = "--dir=";
      if (arg.rfind(dirPrefix, 0) == 0)
      {
        baseDir = arg.substr(sizeof(dirPrefix) - 1);
        return true;
      }

      constexpr const char cwdPrefix[] = "--cwd=";
      if (arg.rfind(cwdPrefix, 0) == 0)
      {
        baseDir = arg.substr(sizeof(cwdPrefix) - 1);
        return true;
      }

      return true;
    }

    /**
     * @brief Parse --limit and --limit=<n>.
     *
     * @param args CLI arguments.
     * @param index Current argument index.
     * @param limit Output limit.
     * @return true when parsing succeeded.
     */
    bool parse_limit_option(
        const std::vector<std::string> &args,
        std::size_t &index,
        std::size_t &limit)
    {
      const std::string &arg = args[index];

      std::string value;

      if (arg == "--limit")
      {
        if (!take_option_value(args, index, arg, value))
          return false;
      }
      else
      {
        constexpr const char prefix[] = "--limit=";
        if (arg.rfind(prefix, 0) != 0)
          return true;

        value = arg.substr(sizeof(prefix) - 1);
      }

      try
      {
        limit = static_cast<std::size_t>(std::stoull(value));
        return true;
      }
      catch (...)
      {
        std::cerr << PAD << RED << "✖" << RESET
                  << " Invalid limit: " << value << "\n";
        return false;
      }
    }

    /**
     * @brief Parse an environment variable assignment.
     *
     * @param value Raw KEY=VALUE string.
     * @param env Output environment variable.
     * @return true when parsing succeeded.
     */
    bool parse_env_assignment(
        const std::string &value,
        replay::ReplayEnvVar &env)
    {
      const auto pos = value.find('=');

      if (pos == std::string::npos || pos == 0)
        return false;

      env.name = value.substr(0, pos);
      env.value = value.substr(pos + 1);
      return true;
    }

    /**
     * @brief Parse extra environment options.
     *
     * @param args CLI arguments.
     * @param index Current argument index.
     * @param env Output extra environment variables.
     * @return true when parsing succeeded.
     */
    bool parse_env_option(
        const std::vector<std::string> &args,
        std::size_t &index,
        std::vector<replay::ReplayEnvVar> &env)
    {
      const std::string &arg = args[index];

      std::string value;

      if (arg == "--env")
      {
        if (!take_option_value(args, index, arg, value))
          return false;
      }
      else
      {
        constexpr const char prefix[] = "--env=";
        if (arg.rfind(prefix, 0) != 0)
          return true;

        value = arg.substr(sizeof(prefix) - 1);
      }

      replay::ReplayEnvVar item{};
      if (!parse_env_assignment(value, item))
      {
        std::cerr << PAD << RED << "✖" << RESET
                  << " Invalid environment assignment: " << value << "\n";
        return false;
      }

      env.push_back(item);
      return true;
    }

    /**
     * @brief Parse a replay selector and options.
     *
     * @param args CLI arguments.
     * @param selector Output replay selector.
     * @param baseDir Output base directory.
     * @param dryRun Output dry-run flag.
     * @param printSummary Output summary flag.
     * @param extraArgs Output extra app arguments.
     * @param extraEnv Output extra environment variables.
     * @return true when parsing succeeded.
     */
    bool parse_run_args(
        const std::vector<std::string> &args,
        replay::ReplaySelector &selector,
        fs::path &baseDir,
        bool &dryRun,
        bool &printSummary,
        std::vector<std::string> &extraArgs,
        std::vector<replay::ReplayEnvVar> &extraEnv)
    {
      bool afterDoubleDash = false;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (afterDoubleDash)
        {
          extraArgs.push_back(arg);
          continue;
        }

        if (arg == "--")
        {
          afterDoubleDash = true;
          continue;
        }

        if (arg == "--dry-run")
        {
          dryRun = true;
          continue;
        }

        if (arg == "--no-summary")
        {
          printSummary = false;
          continue;
        }

        if (arg == "--summary")
        {
          printSummary = true;
          continue;
        }

        if (arg == "--dir" || arg == "--cwd" ||
            arg.rfind("--dir=", 0) == 0 ||
            arg.rfind("--cwd=", 0) == 0)
        {
          if (!parse_dir_option(args, i, baseDir))
            return false;
          continue;
        }

        if (arg == "--env" || arg.rfind("--env=", 0) == 0)
        {
          if (!parse_env_option(args, i, extraEnv))
            return false;
          continue;
        }

        if (is_option(arg))
        {
          std::cerr << PAD << RED << "✖" << RESET
                    << " Unknown replay option: " << arg << "\n";
          return false;
        }

        if (selector.value == "last")
        {
          selector.value = arg;
          continue;
        }

        extraArgs.push_back(arg);
      }

      return true;
    }

    /**
     * @brief Run `vix replay list`.
     *
     * @param args Arguments after `list`.
     * @return Process exit code.
     */
    int run_list(const std::vector<std::string> &args)
    {
      fs::path baseDir;
      std::size_t limit = 20;
      replay::ReplayListFilter filter = replay::ReplayListFilter::All;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (is_help_arg(arg))
        {
          std::cout << "Usage:\n";
          std::cout << "  vix replay list [--failed] [--success] [--interrupted] [--limit <n>] [--dir <path>]\n";
          return 0;
        }

        if (arg == "--failed" || arg == "--fail")
        {
          filter = replay::ReplayListFilter::Failed;
          continue;
        }

        if (arg == "--success" || arg == "--ok")
        {
          filter = replay::ReplayListFilter::Success;
          continue;
        }

        if (arg == "--interrupted" || arg == "--interrupt")
        {
          filter = replay::ReplayListFilter::Interrupted;
          continue;
        }

        if (arg == "--all")
        {
          filter = replay::ReplayListFilter::All;
          continue;
        }

        if (arg == "--limit" || arg.rfind("--limit=", 0) == 0)
        {
          if (!parse_limit_option(args, i, limit))
            return 2;
          continue;
        }

        if (arg == "--dir" || arg == "--cwd" ||
            arg.rfind("--dir=", 0) == 0 ||
            arg.rfind("--cwd=", 0) == 0)
        {
          if (!parse_dir_option(args, i, baseDir))
            return 2;
          continue;
        }

        std::cerr << PAD << RED << "✖" << RESET
                  << " Unknown list option: " << arg << "\n";
        return 2;
      }

      replay::ReplayListOptions options{};
      options.base_dir = baseDir;
      options.filter = filter;
      options.limit = limit;
      options.newest_first = true;

      replay::ReplayListResult result{};
      std::string err;

      if (!replay::list_replays(options, result, err))
      {
        replay::print_replay_error(std::cerr, err);
        return 1;
      }

      replay::print_replay_list(std::cout, result.entries);
      return 0;
    }

    /**
     * @brief Run `vix replay show`.
     *
     * @param args Arguments after `show`.
     * @return Process exit code.
     */
    int run_show(const std::vector<std::string> &args)
    {
      fs::path baseDir;
      std::string id = "last";

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (is_help_arg(arg))
        {
          std::cout << "Usage:\n";
          std::cout << "  vix replay show [id|last|failed] [--dir <path>]\n";
          return 0;
        }

        if (arg == "--dir" || arg == "--cwd" ||
            arg.rfind("--dir=", 0) == 0 ||
            arg.rfind("--cwd=", 0) == 0)
        {
          if (!parse_dir_option(args, i, baseDir))
            return 2;
          continue;
        }

        if (is_option(arg))
        {
          std::cerr << PAD << RED << "✖" << RESET
                    << " Unknown show option: " << arg << "\n";
          return 2;
        }

        id = arg;
      }

      replay::ReplayRecord record{};
      replay::ReplaySelector selector{};
      selector.value = id;

      std::string err;
      const fs::path resolvedBase = baseDir.empty() ? replay::default_replay_base_dir() : baseDir;

      if (!replay::resolve_replay_record(resolvedBase, selector, record, err))
      {
        replay::print_replay_error(std::cerr, err);
        return 1;
      }

      replay::print_replay_record(std::cout, record);
      return 0;
    }

    /**
     * @brief Run `vix replay clean`.
     *
     * @param args Arguments after `clean`.
     * @return Process exit code.
     */
    int run_clean(const std::vector<std::string> &args)
    {
      fs::path baseDir;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (is_help_arg(arg))
        {
          std::cout << "Usage:\n";
          std::cout << "  vix replay clean [--dir <path>]\n";
          return 0;
        }

        if (arg == "--dir" || arg == "--cwd" ||
            arg.rfind("--dir=", 0) == 0 ||
            arg.rfind("--cwd=", 0) == 0)
        {
          if (!parse_dir_option(args, i, baseDir))
            return 2;
          continue;
        }

        std::cerr << PAD << RED << "✖" << RESET
                  << " Unknown clean option: " << arg << "\n";
        return 2;
      }

      std::string err;
      const fs::path resolvedBase = baseDir.empty() ? replay::default_replay_base_dir() : baseDir;

      if (!replay::clear_replay_runs(resolvedBase, err))
      {
        replay::print_replay_error(std::cerr, err);
        return 1;
      }

      std::cout << PAD << GREEN << "✔" << RESET << " replay runs cleared\n";
      return 0;
    }

    /**
     * @brief Run the default replay action.
     *
     * @param args Replay command arguments.
     * @return Process exit code.
     */
    int run_default(const std::vector<std::string> &args)
    {
      replay::ReplaySelector selector{};
      fs::path baseDir;
      bool dryRun = false;
      bool printSummary = true;
      std::vector<std::string> extraArgs;
      std::vector<replay::ReplayEnvVar> extraEnv;

      if (!parse_run_args(
              args,
              selector,
              baseDir,
              dryRun,
              printSummary,
              extraArgs,
              extraEnv))
      {
        return 2;
      }

      replay::ReplayRunnerOptions options{};
      options.base_dir = baseDir;
      options.selector = selector;
      options.extra_args = extraArgs;
      options.extra_env = extraEnv;
      options.print_summary = printSummary;
      options.print_command = true;
      options.dry_run = dryRun;

      replay::ReplayRunnerResult result{};
      std::string err;

      if (!replay::run_replay(options, result, err))
      {
        replay::print_replay_error(std::cerr, err);
        return 1;
      }

      return result.process.exit_code;
    }

  } // namespace

  int run(const std::vector<std::string> &args)
  {
    if (args.empty())
      return run_default({});

    if (is_help_arg(args.front()))
      return help();

    const std::string &sub = args.front();
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (sub == "list" || sub == "ls")
      return run_list(rest);

    if (sub == "show" || sub == "inspect")
      return run_show(rest);

    if (sub == "clean" || sub == "clear")
      return run_clean(rest);

    return run_default(args);
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix replay [id|last|failed] [options] [-- app-args...]\n";
    out << "  vix replay list [options]\n";
    out << "  vix replay show [id|last|failed]\n";
    out << "  vix replay clean\n\n";

    out << "Description:\n";
    out << "  Replay a previously recorded Vix execution.\n";
    out << "  Vix records enough execution context to rerun the same command later.\n\n";

    out << "Selectors:\n";
    out << "  last, latest             Replay the latest recorded run\n";
    out << "  failed, fail             Replay the latest failed run\n";
    out << "  <id>                     Replay a specific run id\n\n";

    out << "Options:\n";
    out << "  --dry-run                Print the replay command without executing it\n";
    out << "  --summary                Print record summary before replaying\n";
    out << "  --no-summary             Do not print record summary\n";
    out << "  --dir, --cwd <path>      Use another directory containing .vix/runs\n";
    out << "  --env KEY=VALUE          Add environment variable during replay\n";
    out << "  --                       Append remaining args to the replay command\n\n";

    out << "List options:\n";
    out << "  --all                    Show all replay runs\n";
    out << "  --failed                 Show failed replay runs\n";
    out << "  --success                Show successful replay runs\n";
    out << "  --interrupted            Show interrupted replay runs\n";
    out << "  --limit <n>              Limit number of rows\n\n";

    out << "Examples:\n";
    out << "  vix replay\n";
    out << "  vix replay last\n";
    out << "  vix replay failed\n";
    out << "  vix replay 2026-05-05-18-42-11-a91f\n";
    out << "  vix replay list --failed\n";
    out << "  vix replay show last\n";
    out << "  vix replay last -- --port 8080\n\n";

    return 0;
  }

} // namespace vix::commands::ReplayCommand
