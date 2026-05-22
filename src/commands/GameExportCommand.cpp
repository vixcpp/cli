/**
 *
 *  @file GameExportCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  vix game export command.
 *
 */

#include <vix/cli/commands/GameExportCommand.hpp>
#include <vix/cli/util/Ui.hpp>

#ifdef VIX_CLI_HAS_GAME
#include <vix/game/GameExportConfig.hpp>
#include <vix/game/GameExporter.hpp>
#endif

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace vix::commands::GameCommand
{
  namespace
  {
    bool has_flag(
        const std::vector<std::string> &args,
        const std::string &flag)
    {
      for (const auto &arg : args)
      {
        if (arg == flag)
          return true;
      }

      return false;
    }

    std::string option_value(
        const std::vector<std::string> &args,
        const std::string &name)
    {
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        if (args[i] == name && i + 1 < args.size())
          return args[i + 1];

        const std::string prefix = name + "=";

        if (args[i].rfind(prefix, 0) == 0)
          return args[i].substr(prefix.size());
      }

      return {};
    }

    bool wants_help(const std::vector<std::string> &args)
    {
      return has_flag(args, "-h") || has_flag(args, "--help");
    }
  } // namespace

  ExportOptions parse_export_options(
      const std::vector<std::string> &args)
  {
    ExportOptions options;

    const auto project_root = option_value(args, "--project-root");
    if (!project_root.empty())
      options.project_root = project_root;

    const auto output = option_value(args, "--output");
    if (!output.empty())
      options.output_directory = output;

    const auto name = option_value(args, "--name");
    if (!name.empty())
      options.name = name;

    options.overwrite = !has_flag(args, "--no-overwrite");
    options.copy_assets = !has_flag(args, "--no-assets");

    return options;
  }

  int export_game(
      const std::vector<std::string> &args)
  {
    using namespace vix::cli::util;

    if (wants_help(args))
      return help();

#ifndef VIX_CLI_HAS_GAME
    (void)args;

    err_line(std::cerr, "Game support is not enabled in this Vix build.");
    warn_line(std::cerr, "Rebuild Vix with -DVIX_ENABLE_GAME=ON.");
    return 1;
#else
    const auto options = parse_export_options(args);

    vix::game::GameExportConfig config;
    config.project_root = options.project_root;
    config.overwrite = options.overwrite;
    config.copy_assets = options.copy_assets;

    if (!options.output_directory.empty())
      config.output_directory = options.output_directory;

    if (!options.name.empty())
      config.name = options.name;

    vix::game::GameExporter exporter;

    auto result = exporter.export_project(config);
    if (!result)
    {
      err_line(std::cerr, "Game export failed.");
      warn_line(std::cerr, result.error().message());
      return 1;
    }

    ok_line(std::cout, "Game exported.");
    kv(std::cout, "Output", result.value().output_path.string());
    kv(std::cout, "Name", result.value().name);
    kv(std::cout, "Version", result.value().version);
    kv(std::cout, "Asset root", result.value().asset_root);
    kv(std::cout, "Copied files", std::to_string(result.value().copied_files));
    kv(std::cout, "Copied directories", std::to_string(result.value().copied_directories));

    return 0;
#endif
  }

  int run(
      const std::vector<std::string> &args)
  {
    if (args.empty())
      return help();

    if (args[0] == "-h" || args[0] == "--help")
      return help();

    if (args[0] == "export")
    {
      std::vector<std::string> rest;
      rest.reserve(args.size());

      for (std::size_t i = 1; i < args.size(); ++i)
        rest.push_back(args[i]);

      return export_game(rest);
    }

    vix::cli::util::err_line(std::cerr, "Unknown game command.");
    vix::cli::util::warn_line(std::cerr, "Usage: vix game export [options]");
    return 1;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out
        << "vix game\n"
        << "Game project tools.\n\n"

        << "Usage\n"
        << "  vix game export [options]\n\n"

        << "Options\n"
        << "  --project-root <path>       Project root, default: .\n"
        << "  --project-root=<path>       Same as --project-root <path>\n"
        << "  --output <path>             Override output directory\n"
        << "  --output=<path>             Same as --output <path>\n"
        << "  --name <name>               Override export name\n"
        << "  --name=<name>               Same as --name <name>\n"
        << "  --no-overwrite              Fail if output already exists\n"
        << "  --no-assets                 Do not copy assets\n"
        << "  -h, --help                  Show help\n\n"

        << "Examples\n"
        << "  vix game export\n"
        << "  vix game export --project-root .\n"
        << "  vix game export --output dist\n"
        << "  vix game export --name mario-release\n"
        << "  vix game export --no-overwrite\n\n";

    return 0;
  }

} // namespace vix::commands::GameCommand
