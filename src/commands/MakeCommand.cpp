/**
 *
 *  @file MakeCommand.cpp
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
#include <vix/cli/commands/MakeCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/commands/make/MakeDispatcher.hpp>
#include <vix/cli/commands/make/MakeOptions.hpp>
#include <vix/cli/commands/make/MakeResult.hpp>
#include <vix/cli/commands/make/MakeUtils.hpp>
#include <vix/cli/util/Ui.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace vix::commands
{
  namespace fs = std::filesystem;
  namespace ui = vix::cli::util;
  using namespace vix::cli::style;
  namespace mk = vix::cli::make;

  namespace
  {
    [[nodiscard]] mk::MakeOptions parse_args(const std::vector<std::string> &args)
    {
      mk::MakeOptions opt;

      auto is_opt = [](const std::string &s)
      { return !s.empty() && s.front() == '-'; };

      if (args.empty())
      {
        opt.show_help = true;
        return opt;
      }

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "-h" || a == "--help")
        {
          opt.show_help = true;
        }
        else if (a == "--force")
        {
          opt.force = true;
        }
        else if (a == "--dry-run")
        {
          opt.dry_run = true;
        }
        else if (a == "--print")
        {
          opt.print_only = true;
        }
        else if (a == "--header-only")
        {
          opt.header_only = true;
        }
        else if (a == "-d" || a == "--dir")
        {
          if (i + 1 < args.size() && !is_opt(args[i + 1]))
            opt.dir = args[++i];
        }
        else if (mk::starts_with(a, "--dir="))
        {
          opt.dir = a.substr(std::string("--dir=").size());
        }
        else if (a == "--in")
        {
          if (i + 1 < args.size() && !is_opt(args[i + 1]))
            opt.in = args[++i];
        }
        else if (mk::starts_with(a, "--in="))
        {
          opt.in = a.substr(std::string("--in=").size());
        }
        else if (a == "--namespace")
        {
          if (i + 1 < args.size() && !is_opt(args[i + 1]))
            opt.name_space = args[++i];
        }
        else if (mk::starts_with(a, "--namespace="))
        {
          opt.name_space = a.substr(std::string("--namespace=").size());
        }
        else if (opt.kind.empty() && !is_opt(a))
        {
          opt.kind = a;
        }
        else if (opt.name.empty() && !is_opt(a))
        {
          opt.name = a;
        }
      }

      return opt;
    }

    void print_result_summary(const mk::MakeOptions &opt,
                              const mk::MakeResult &result)
    {
      ui::section(std::cout, "Make");
      ui::kv(std::cout, "kind", opt.kind.empty() ? "(none)" : opt.kind, 12);
      ui::kv(std::cout, "name", opt.name.empty() ? "(none)" : opt.name, 12);

      if (!opt.name_space.empty())
        ui::kv(std::cout, "namespace", opt.name_space, 12);

      if (!opt.in.empty())
        ui::kv(std::cout, "in", opt.in, 12);

      if (opt.dry_run)
        ui::kv(std::cout, "mode", "dry-run", 12);
      else if (opt.print_only)
        ui::kv(std::cout, "mode", "print", 12);
      else
        ui::kv(std::cout, "mode", "write", 12);

      for (const auto &file : result.files)
        ui::kv(std::cout, "file", file.path.string(), 12);

      for (const auto &note : result.notes)
        ui::warn_line(std::cout, note);
    }

    void print_preview_if_needed(const mk::MakeResult &result)
    {
      if (result.preview.empty())
        return;

      std::cout << "\n"
                << BOLD << "Preview" << RESET << "\n\n"
                << result.preview << "\n";
    }

    [[nodiscard]] bool validate_write_targets(const std::vector<mk::MakeFile> &files,
                                              bool force,
                                              std::string &error_message)
    {
      for (const auto &file : files)
      {
        if (mk::exists_file(file.path) && !force)
        {
          error_message =
              "File already exists: " + file.path.string() + " (use --force)";
          return false;
        }
      }

      return true;
    }

    [[nodiscard]] bool write_generated_files(const std::vector<mk::MakeFile> &files,
                                             std::string &error_message)
    {
      for (const auto &file : files)
      {
        const fs::path parent = file.path.parent_path();

        if (!parent.empty() && !mk::ensure_dir(parent))
        {
          error_message = "Failed to create directory: " + parent.string();
          return false;
        }

        if (!mk::write_file_overwrite(file.path, file.content))
        {
          error_message = "Failed to write file: " + file.path.string();
          return false;
        }
      }

      return true;
    }
  } // namespace

  int MakeCommand::run(const std::vector<std::string> &args)
  {
    const mk::MakeOptions opt = parse_args(args);

    if (opt.show_help)
      return help();

    if (opt.kind.empty())
    {
      ui::err_line(std::cout, "Missing make kind.");
      ui::warn_line(std::cout, "Example: vix make class User");
      return 1;
    }

    if (opt.name.empty())
    {
      const mk::MakeKind kind = mk::parse_make_kind(opt.kind);
      if (kind != mk::MakeKind::Unknown)
      {
        ui::err_line(std::cout, "Missing name.");
        ui::warn_line(std::cout, "Example: vix make class User");
        return 1;
      }
    }

    const mk::MakeResult result = mk::dispatch_make(opt);

    if (!result.ok)
    {
      ui::section(std::cout, "Make");
      if (!result.error.empty())
        ui::err_line(std::cout, result.error);
      else
        ui::err_line(std::cout, "Generation failed.");

      for (const auto &note : result.notes)
        ui::warn_line(std::cout, note);

      return 1;
    }

    print_result_summary(opt, result);
    print_preview_if_needed(result);

    if (opt.print_only)
    {
      ui::ok_line(std::cout, "Printed");
      return 0;
    }

    if (opt.dry_run)
    {
      ui::ok_line(std::cout, "Dry-run complete");
      return 0;
    }

    std::string error_message;
    if (!validate_write_targets(result.files, opt.force, error_message))
    {
      ui::err_line(std::cout, error_message);
      return 1;
    }

    if (!write_generated_files(result.files, error_message))
    {
      ui::err_line(std::cout, error_message);
      return 1;
    }

    ui::ok_line(std::cout, "Generated");
    ui::kv(std::cout, "count", std::to_string(result.files.size()), 12);

    return 0;
  }

  int MakeCommand::help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix make <kind> <name> [options]\n\n";

    out << "Goal:\n";
    out << "  Generate C++ code faster with safe and predictable scaffolding.\n";
    out << "  This command supports both snippet-style generation and real project files.\n\n";

    out << "Kinds:\n";
    out << "  class       Generate a class (.hpp + .cpp by default)\n";
    out << "  struct      Generate a plain data struct (.hpp)\n";
    out << "  enum        Generate an enum class with helpers (.hpp)\n";
    out << "  function    Generate a free function (.hpp + .cpp by default)\n";
    out << "  lambda      Generate a modern generic lambda\n";
    out << "  concept     Generate a C++20 concept (.hpp)\n";
    out << "  exception   Generate a std::exception derived type\n";
    out << "  test        Generate a GoogleTest skeleton\n";
    out << "  module      Redirects to the modules workflow\n\n";

    out << "Options:\n";
    out << "  -d, --dir <path>          Project root (default: current directory)\n";
    out << "  --in <path>               Target area inside the project\n";
    out << "  --namespace <ns>          Override namespace\n";
    out << "  --header-only             Generate only header files when supported\n";
    out << "  --print                   Print preview/snippet without writing files\n";
    out << "  --dry-run                 Show what would be generated without writing files\n";
    out << "  --force                   Overwrite existing files\n";
    out << "  -h, --help                Show help\n\n";

    out << "Examples:\n";
    out << "  vix make class User\n";
    out << "  vix make struct Claims --namespace auth\n";
    out << "  vix make enum Status --in modules/auth\n";
    out << "  vix make function parse_token --in modules/auth\n";
    out << "  vix make lambda visit_all --print\n";
    out << "  vix make concept EqualityComparable\n";
    out << "  vix make exception InvalidToken --in modules/auth\n";
    out << "  vix make test AuthService\n\n";

    out << "Behavior:\n";
    out << "  - In a regular project, headers go to include/ and sources to src/.\n";
    out << "  - In a module path like modules/auth, headers go to modules/auth/include/auth/ and sources to modules/auth/src/.\n";
    out << "  - Existing files are never overwritten unless --force is set.\n";
    out << "  - For module creation, use: vix modules add <name>\n\n";

    return 0;
  }

} // namespace vix::commands
