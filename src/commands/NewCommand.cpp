/**
 * @file NewCommand.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Entry-point for `vix new`.  Orchestrates argument parsing, interactive
 * prompts, and project scaffolding by delegating to:
 *   - vix::commands::new_cmd::tui       (interactive menus)
 *   - vix::commands::new_cmd::generator (file-system scaffolding)
 */

#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/new/NewGenerator.hpp>
#include <vix/cli/commands/new/NewTui.hpp>
#include <vix/cli/commands/new/NewTypes.hpp>
#include <vix/cli/Utils.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace gen = vix::commands::new_cmd::generator;
namespace tui = vix::commands::new_cmd::tui;
using vix::commands::new_cmd::FeaturesSelection;
using vix::commands::new_cmd::InPlaceDirChoice;
using vix::commands::new_cmd::OverwriteChoice;
using vix::commands::new_cmd::TemplateKind;
using namespace vix::cli::util;

namespace
{
  // ------------------------------------------------------------------
  // Argv helpers (local to this TU)
  // ------------------------------------------------------------------

  bool consume_flag(std::vector<std::string> &a, const std::string &flag)
  {
    auto it = std::find(a.begin(), a.end(), flag);
    if (it == a.end())
      return false;
    a.erase(it);
    return true;
  }

  bool has_any(const std::vector<std::string> &a, const std::vector<std::string> &candidates)
  {
    for (const auto &x : candidates)
      if (std::find(a.begin(), a.end(), x) != a.end())
        return true;
    return false;
  }

  std::optional<std::string> take_option_value(
      std::vector<std::string> &a,
      const std::vector<std::string> &names)
  {
    for (const auto &n : names)
    {
      for (std::size_t i = 0; i < a.size(); ++i)
      {
        if (a[i] == n)
        {
          if (i + 1 >= a.size())
            return std::nullopt;
          std::string v = a[i + 1];
          a.erase(a.begin() + (long)i, a.begin() + (long)i + 2);
          return v;
        }
        const std::string prefix = n + "=";
        if (a[i].rfind(prefix, 0) == 0)
        {
          std::string v = a[i].substr(prefix.size());
          a.erase(a.begin() + (long)i);
          return v;
        }
      }
    }
    return std::nullopt;
  }

} // anonymous namespace

namespace vix::commands::NewCommand
{

  int run(const std::vector<std::string> &argsIn)
  {
    // Return codes: 0 = success  |  2 = user cancelled  |  1 = error
    if (argsIn.empty())
    {
      error("Missing project name.");
      hint("Usage: vix new <name|path> [-d|--dir <base_dir>] [--app|--lib|--game] [--force]");
      return 1;
    }

    std::vector<std::string> args = argsIn;

    const bool force = consume_flag(args, "--force");
    const std::optional<std::string> baseOpt = take_option_value(args, {"-d", "--dir"});
    const std::optional<std::string> templateOpt =
        take_option_value(args, {"--template"});

    const bool wantsLib = has_any(args, {"--lib", "--library", "--type=lib", "--type=library"});
    const bool wantsApp = has_any(args, {"--app", "--application", "--type=app", "--type=application"});
    const bool wantsGame = has_any(args, {"--game", "--type=game"});

    for (const auto &f : {"--lib", "--library", "--type=lib", "--type=library",
                          "--app", "--application", "--type=app", "--type=application",
                          "--game", "--type=game"})
      consume_flag(args, f);

    if (args.empty())
    {
      error("Missing project name.");
      hint("Usage: vix new <name|path> [--template vue|game] [--app|--lib|--game] [--force]");
      return 1;
    }

    if ((wantsLib && wantsApp) || (wantsLib && wantsGame) || (wantsApp && wantsGame))
    {
      error("Conflicting options: choose only one project type.");
      return 1;
    }

    if (!args[0].empty() && args[0][0] == '-' && !gen::is_dot_path(args[0]))
    {
      error("Missing project name.");
      hint("Options must come after a project name or be followed by one.");
      step("vix new my-app --template vue");
      return 1;
    }

    if (templateOpt.has_value())
    {
      const std::string tpl = *templateOpt;

      if (tpl != "vue" && tpl != "game")
      {
        error("Unknown template: " + tpl);
        hint("Supported templates: vue, game");
        return 1;
      }

      if (wantsLib)
      {
        error("Conflicting options: --template cannot be used with --lib.");
        hint("Templates currently supported: vue, game.");
        return 1;
      }

      if (wantsApp && *templateOpt == "game")
      {
        error("Conflicting options: --template game cannot be used with --app.");
        hint("Use --game or --template game.");
        return 1;
      }

      if (wantsGame && *templateOpt == "vue")
      {
        error("Conflicting options: --template vue cannot be used with --game.");
        hint("Use --app, --template vue, or --template game.");
        return 1;
      }
    }

    const std::string nameOrPath = args[0];
    const bool inPlace = gen::is_dot_path(nameOrPath);

    try
    {
      tui::SignalGuard sig;
      if (tui::g_cancelled.load())
        return 2;

      // ------------------------------------------------------------------
      // Step 0 – Resolve destination path
      // ------------------------------------------------------------------
      fs::path dest;
      fs::path np = fs::path(nameOrPath);

      if (baseOpt.has_value())
      {
        fs::path base = fs::path(*baseOpt);
        std::error_code ec;
        if (!fs::exists(base, ec) || !fs::is_directory(base, ec))
        {
          error("Base directory '" + base.string() + "' is not a valid folder.");
          hint("Make sure it exists and is a directory, or omit --dir.");
          return 1;
        }
        dest = np.is_absolute() ? np : fs::weakly_canonical(base / np, ec);
        if (ec)
          dest = base / np;
      }
      else
      {
        std::error_code ec;
        dest = np.is_absolute() ? np : fs::weakly_canonical(fs::current_path() / np, ec);
        if (ec)
          dest = fs::current_path() / np;
      }

      fs::path projectDir = inPlace ? fs::current_path() : dest;
      std::string projName = inPlace ? gen::current_dir_name() : projectDir.filename().string();

      if (tui::g_cancelled.load())
        return 2;

      // ------------------------------------------------------------------
      // Step 1 – Existing-directory handling
      // ------------------------------------------------------------------
      if (fs::exists(projectDir))
      {
        if (!gen::dir_exists(projectDir))
        {
          error("Path exists but is not a directory: '" + projectDir.string() + "'");
          return 1;
        }

        if (!gen::dir_is_empty(projectDir))
        {
          if (inPlace)
          {
            if (!force)
            {
              if (tui::can_interact())
              {
                const auto choice = tui::confirm_inplace_dir_interactive(projectDir);
                if (choice.cancelled || choice.value == InPlaceDirChoice::Cancel)
                  return 2;
              }
              else
              {
                error("Cannot create project in current directory: directory is not empty.");
                hint("Use --force to overwrite template files.");
                return 1;
              }
            }
            // force=true → proceed (overwrite files, do NOT delete the dir)
          }
          else
          {
            if (force)
            {
              std::error_code ec;
              fs::remove_all(projectDir, ec);
              if (ec)
              {
                error("Failed to remove existing directory.");
                hint(ec.message());
                return 1;
              }
            }
            else if (tui::can_interact())
            {
              const auto choice = tui::confirm_overwrite_interactive(projectDir);
              if (choice.cancelled || choice.value == OverwriteChoice::Cancel)
                return 2;
              std::error_code ec;
              fs::remove_all(projectDir, ec);
              if (ec)
              {
                error("Failed to remove existing directory.");
                hint(ec.message());
                return 1;
              }
            }
            else
            {
              error("Cannot create project in '" + projectDir.string() + "': directory is not empty.");
              hint("Use --force to overwrite.");
              return 1;
            }
          }

          if (tui::g_cancelled.load())
            return 2;
        }
      }

      // ------------------------------------------------------------------
      // Step 2 – Choose template
      // ------------------------------------------------------------------
      TemplateKind kind = TemplateKind::App;

      if (templateOpt.has_value() && *templateOpt == "game")
      {
        kind = TemplateKind::Game;
      }
      else if (templateOpt.has_value() && *templateOpt == "vue")
      {
        kind = TemplateKind::Vue;
      }
      else if (wantsGame)
      {
        kind = TemplateKind::Game;
      }
      else if (wantsLib)
      {
        kind = TemplateKind::Lib;
      }
      else if (wantsApp)
      {
        kind = TemplateKind::App;
      }
      else if (tui::can_interact())
      {
        const auto sel = tui::choose_template_interactive();
        if (sel.cancelled)
          return 2;
        kind = sel.value;
      }

      if (tui::g_cancelled.load())
        return 2;

      // ------------------------------------------------------------------
      // Step 3 – Choose features (App only)
      // ------------------------------------------------------------------
      FeaturesSelection features{};
      if ((kind == TemplateKind::App || kind == TemplateKind::Vue) && tui::can_interact())
      {
        bool cancelled = false;
        features = tui::choose_features_interactive(cancelled);
        if (cancelled)
          return 2;
      }

      if (features.full_static)
        features.static_rt = true;
      if (tui::g_cancelled.load())
        return 2;

      // ------------------------------------------------------------------
      // Step 4 – Create project root
      // ------------------------------------------------------------------
      {
        std::string err;
        if (!gen::ensure_dir(projectDir, err))
        {
          error("Failed to create project directory.");
          hint(err);
          return 1;
        }
      }

      if (tui::g_cancelled.load())
        return 2;

      // ------------------------------------------------------------------
      // Step 5 – Generate files
      // ------------------------------------------------------------------
      std::string genErr;

      if (kind == TemplateKind::Vue)
      {
        if (!gen::generate_vue_project(projectDir, projName, features, genErr))
        {
          vix::cli::util::err_line(std::cerr, "Failed to create project files.");
          vix::cli::util::warn_line(std::cerr, genErr);
          return 1;
        }

        vix::cli::util::ok_line(std::cout, "Project created.");
        vix::cli::util::kv(std::cout, "Location", projectDir.string());
        gen::print_next_steps_vue(projectDir, projName);
        return 0;
      }

      if (kind == TemplateKind::Game)
      {
        if (!gen::generate_game_project(projectDir, projName, genErr))
        {
          vix::cli::util::err_line(std::cerr, "Failed to create project files.");
          vix::cli::util::warn_line(std::cerr, genErr);
          return 1;
        }

        vix::cli::util::ok_line(std::cout, "Project created.");
        vix::cli::util::kv(std::cout, "Location", projectDir.string());
        gen::print_next_steps_game(projectDir, projName);
        return 0;
      }

      if (kind == TemplateKind::App)
      {
        if (!gen::generate_app_project(projectDir, projName, features, genErr))
        {
          vix::cli::util::err_line(std::cerr, "Failed to create project files.");
          vix::cli::util::warn_line(std::cerr, genErr);
          return 1;
        }
        vix::cli::util::ok_line(std::cout, "Project created.");
        vix::cli::util::kv(std::cout, "Location", projectDir.string());
        gen::print_next_steps_app(projectDir, projName);
        return 0;
      }

      if (!gen::generate_lib_project(projectDir, projName, genErr))
      {
        vix::cli::util::err_line(std::cerr, "Failed to create project files.");
        vix::cli::util::warn_line(std::cerr, genErr);
        return 1;
      }
      vix::cli::util::ok_line(std::cout, "Project created.");
      vix::cli::util::kv(std::cout, "Location", projectDir.string());
      gen::print_next_steps_lib(projectDir, projName);
      return 0;
    }
    catch (const std::exception &ex)
    {
      error("Failed to create project.");
      hint(ex.what());
      return 1;
    }
  }

  int help()
  {
    std::ostream &out = std::cout;

    out
        << "vix new\n"
        << "Create a new Vix project.\n\n"

        << "Usage\n"
        << "  vix new <name|path> [options]\n\n"

        << "Examples\n"
        << "  vix new api\n"
        << "  vix new .\n"
        << "  vix new tree --lib\n"
        << "  vix new mario --game\n"
        << "  vix new shop --template vue\n"
        << "  vix new platformer --template game\n"
        << "  vix new blog -d ./projects\n"
        << "  vix new api --force\n\n"

        << "What happens\n"
        << "  • Generates a ready-to-run Vix project\n"
        << "  • Sets up CMake, source structure, and config files\n"
        << "  • Creates a vix.json manifest\n"
        << "  • For apps, creates an executable target matching the project name\n"
        << "  • For libraries, creates a header-only CMake interface target\n"
        << "  • Applies the selected template (app, game, library, or Vue)\n\n"

        << "  --app       Generate an application (default)\n"
        << "  --game      Generate a Vix game project\n"
        << "  --lib       Generate a header-only library\n"
        << "  --template  Project template, currently: vue, game\n"
        << "  -d, --dir   Base directory for project creation\n"
        << "  --force     Overwrite existing directory\n\n"

        << "Application workflow\n"
        << "  cd api/\n"
        << "  vix build\n"
        << "  vix run\n\n"

        << "Game workflow\n"
        << "  cd mario/\n"
        << "  vix build\n"
        << "  vix run\n\n"

        << "Library workflow\n"
        << "  cd tree/\n"
        << "  vix build --build-target all\n"
        << "  vix build --build-target all -- -Dtree_BUILD_TESTS=ON\n"
        << "  vix tests\n\n"

        << "Environment\n"
        << "  VIX_NONINTERACTIVE=1   Disable prompts\n"
        << "  CI=1                   Disable prompts\n\n"

        << "Notes\n"
        << "  • Use '.' to initialize in the current directory\n"
        << "  • Use 'vix add <pkg>' to add dependencies\n"
        << "  • Use 'vix install' to install from vix.lock\n"
        << "  • Use 'vix task <name>' to run generated tasks\n"
        << "  • For header-only libraries, use '--build-target all' to build the generated project\n"
        << "  • Tests for header-only libraries are disabled by default and must be enabled with CMake args\n"
        << "  • Designed for fast start with zero setup\n";

    return 0;
  }

} // namespace vix::commands::NewCommand
