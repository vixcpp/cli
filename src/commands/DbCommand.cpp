/**
 *
 *  @file DbCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/DbCommand.hpp>
#include <vix/cli/commands/db/DbBackup.hpp>
#include <vix/cli/commands/db/DbChecker.hpp>
#include <vix/cli/commands/db/DbConfig.hpp>
#include <vix/cli/commands/db/DbMigrator.hpp>
#include <vix/cli/commands/db/DbOutput.hpp>
#include <vix/cli/commands/db/DbTypes.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace vix::commands
{
  namespace
  {
    /**
     * @brief Consume a boolean command-line flag.
     *
     * @param args Mutable argument list.
     * @param flag Flag to consume.
     * @return true if the flag was found and removed.
     */
    bool consume_flag(
        std::vector<std::string> &args,
        const std::string &flag)
    {
      for (auto it = args.begin(); it != args.end(); ++it)
      {
        if (*it == flag)
        {
          args.erase(it);
          return true;
        }
      }

      return false;
    }

    /**
     * @brief Parse db command options.
     *
     * @param args Mutable command arguments.
     * @param ok Output parsing status.
     * @param errorMessage Output error message when parsing fails.
     * @return Parsed database options.
     */
    db::DbOptions parse_options(
        std::vector<std::string> &args,
        bool &ok,
        std::string &errorMessage)
    {
      db::DbOptions options;

      ok = true;
      errorMessage.clear();

      options.json = consume_flag(args, "--json");

      options.verbose =
          consume_flag(args, "--verbose") ||
          consume_flag(args, "-v");

      return options;
    }

    /**
     * @brief Parse the db subcommand.
     *
     * @param args Mutable command arguments.
     * @return Parsed subcommand name.
     */
    std::string parse_action(
        std::vector<std::string> &args)
    {
      if (args.empty())
        return "status";

      const std::string action = args.front();
      args.erase(args.begin());

      return action;
    }

    /**
     * @brief Convert database status to a process exit code.
     *
     * @param status Database inspection status.
     * @return Process exit code.
     */
    int exit_code_for_status(db::DbStatus status)
    {
      switch (status)
      {
      case db::DbStatus::Ok:
        return 0;

      case db::DbStatus::Warning:
        return 0;

      case db::DbStatus::Error:
        return 1;
      }

      return 1;
    }
  }

  int DbCommand::run(const std::vector<std::string> &argsIn)
  {
    std::vector<std::string> args = argsIn;

    if (!args.empty() &&
        (args[0] == "-h" || args[0] == "--help"))
    {
      return help();
    }

    const std::string action =
        parse_action(args);

    bool ok = true;
    std::string errorMessage;

    db::DbOptions options =
        parse_options(args, ok, errorMessage);

    if (!ok)
    {
      db::output::error(std::cerr, errorMessage);
      db::output::fix(std::cerr, "vix db --help");
      return 1;
    }

    if (!args.empty())
    {
      db::output::error(
          std::cerr,
          "unknown db argument: " + args.front());

      db::output::fix(
          std::cerr,
          "vix db --help");

      return 1;
    }

    const db::DbConfig cfg =
        db::apply_db_options(
            db::load_db_config(),
            options);

    if (action == "status")
    {
      const db::DbCheckResult result =
          db::checker::check_status(cfg);

      db::output::print_status(
          std::cout,
          result,
          options);

      return exit_code_for_status(result.status);
    }

    if (action == "migrate")
    {
      return db::migrator::migrate(
          cfg,
          options);
    }

    if (action == "backup")
    {
      return db::backup::create_backup(
          cfg,
          options);
    }

    db::output::error(
        std::cerr,
        "unknown db action: " + action);

    db::output::fix(
        std::cerr,
        "vix db --help");

    return 1;
  }

  int DbCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix db [action] [options]\n\n"
        << "Actions:\n"
        << "  status      Inspect SQLite database and storage status\n"
        << "  migrate     Apply pending file-based SQL migrations\n"
        << "  backup      Create a SQLite database backup\n\n"
        << "Options:\n"
        << "  --json      Print supported output as JSON\n"
        << "  --verbose   Show verbose diagnostic output\n"
        << "  -v          Alias for --verbose\n"
        << "  -h, --help  Show this help message\n\n"
        << "Examples:\n"
        << "  vix db\n"
        << "  vix db status\n"
        << "  vix db status --json\n"
        << "  vix db migrate\n"
        << "  vix db backup\n\n"
        << "Config:\n"
        << "  database.engine\n"
        << "  database.sqlite.path\n"
        << "  database.storage\n"
        << "  database.migrations\n";

    return 0;
  }
}
