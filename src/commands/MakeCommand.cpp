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
  namespace mk = vix::cli::make;

  namespace
  {
    [[nodiscard]] bool is_option_token(const std::string &s)
    {
      return !s.empty() && s.front() == '-';
    }

    [[nodiscard]] bool is_make_colon_kind(const std::string &s)
    {
      return mk::starts_with(s, "make:") && s.size() > std::string("make:").size();
    }

    [[nodiscard]] std::string extract_make_colon_kind(const std::string &s)
    {
      if (!is_make_colon_kind(s))
        return {};

      return s.substr(std::string("make:").size());
    }

    [[nodiscard]] std::string prompt_line(const std::string &label)
    {
      std::string value;
      std::cout << label;
      std::getline(std::cin, value);
      return mk::trim(value);
    }

    [[nodiscard]] bool prompt_yes_no(const std::string &label,
                                     bool default_value)
    {
      const std::string suffix = default_value ? " [Y/n]: " : " [y/N]: ";

      while (true)
      {
        const std::string value = mk::to_lower(prompt_line(label + suffix));

        if (value.empty())
          return default_value;

        if (value == "y" || value == "yes")
          return true;

        if (value == "n" || value == "no")
          return false;

        ui::warn_line(std::cout, "Please answer y or n.");
      }
    }

    [[nodiscard]] mk::MakeOptions parse_args(const std::vector<std::string> &args)
    {
      mk::MakeOptions opt;

      if (args.empty())
      {
        opt.show_help = true;
        return opt;
      }

      std::size_t i = 0;

      if (is_make_colon_kind(args[0]))
      {
        opt.kind = extract_make_colon_kind(args[0]);
        i = 1;
      }

      for (; i < args.size(); ++i)
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
        else if (a == "--server")
        {
          opt.config_options.with_server = true;
        }
        else if (a == "--no-server")
        {
          opt.config_options.with_server = false;
        }
        else if (a == "--logging")
        {
          opt.config_options.with_logging = true;
        }
        else if (a == "--no-logging")
        {
          opt.config_options.with_logging = false;
        }
        else if (a == "--waf")
        {
          opt.config_options.with_waf = true;
        }
        else if (a == "--no-waf")
        {
          opt.config_options.with_waf = false;
        }
        else if (a == "--websocket")
        {
          opt.config_options.with_websocket = true;
        }
        else if (a == "--no-websocket")
        {
          opt.config_options.with_websocket = false;
        }
        else if (a == "--database")
        {
          opt.config_options.with_database = true;
        }
        else if (a == "--no-database")
        {
          opt.config_options.with_database = false;
        }
        else if (a == "-d" || a == "--dir")
        {
          if (i + 1 < args.size() && !is_option_token(args[i + 1]))
            opt.dir = args[++i];
        }
        else if (mk::starts_with(a, "--dir="))
        {
          opt.dir = a.substr(std::string("--dir=").size());
        }
        else if (a == "--in")
        {
          if (i + 1 < args.size() && !is_option_token(args[i + 1]))
            opt.in = args[++i];
        }
        else if (mk::starts_with(a, "--in="))
        {
          opt.in = a.substr(std::string("--in=").size());
        }
        else if (a == "--namespace")
        {
          if (i + 1 < args.size() && !is_option_token(args[i + 1]))
            opt.name_space = args[++i];
        }
        else if (mk::starts_with(a, "--namespace="))
        {
          opt.name_space = a.substr(std::string("--namespace=").size());
        }
        else if (opt.kind.empty() && !is_option_token(a))
        {
          opt.kind = a;
        }
        else if (opt.name.empty() && !is_option_token(a))
        {
          opt.name = a;
        }
      }

      return opt;
    }

    void print_files_list(const std::vector<mk::MakeFile> &files)
    {
      for (const auto &file : files)
        ui::kv(std::cout, "file", file.path.string(), 10);
    }

    void print_preview_output(const mk::MakeResult &result)
    {
      if (!result.preview.empty())
      {
        std::cout << result.preview;
        if (result.preview.back() != '\n')
          std::cout << "\n";
        return;
      }

      for (const auto &file : result.files)
      {
        std::cout << "// " << file.path.string() << "\n";
        std::cout << file.content;

        if (!file.content.empty() && file.content.back() != '\n')
          std::cout << "\n";

        std::cout << "\n";
      }
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

    int run_make_result(const mk::MakeOptions &opt,
                        const mk::MakeResult &result)
    {
      if (!result.ok)
      {
        if (!result.error.empty())
          ui::err_line(std::cout, result.error);
        else
          ui::err_line(std::cout, "Generation failed.");

        return 1;
      }

      if (opt.print_only)
      {
        print_preview_output(result);
        return 0;
      }

      if (opt.dry_run)
      {
        ui::ok_line(std::cout, "Ready");
        ui::kv(std::cout, "kind", opt.kind.empty() ? "(none)" : opt.kind, 10);
        ui::kv(std::cout, "name", opt.name.empty() ? "(none)" : opt.name, 10);

        if (!opt.name_space.empty())
          ui::kv(std::cout, "namespace", opt.name_space, 10);

        ui::kv(std::cout, "files", std::to_string(result.files.size()), 10);
        print_files_list(result.files);
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
      ui::kv(std::cout, "kind", opt.kind.empty() ? "(none)" : opt.kind, 10);
      ui::kv(std::cout, "name", opt.name.empty() ? "(none)" : opt.name, 10);

      if (!opt.name_space.empty())
        ui::kv(std::cout, "namespace", opt.name_space, 10);

      ui::kv(std::cout, "files", std::to_string(result.files.size()), 10);
      print_files_list(result.files);

      return 0;
    }

    int run_direct(const mk::MakeOptions &opt)
    {
      const mk::MakeResult result = mk::dispatch_make(opt);
      return run_make_result(opt, result);
    }

    int run_class_prompt(mk::MakeOptions opt)
    {
      if (opt.name.empty())
      {
        while (true)
        {
          opt.name = prompt_line("Class name: ");

          if (opt.name.empty())
          {
            ui::warn_line(std::cout, "Class name is required.");
            continue;
          }

          if (!mk::is_valid_cpp_identifier(opt.name))
          {
            ui::warn_line(std::cout, "Invalid C++ identifier.");
            continue;
          }

          if (mk::is_reserved_cpp_keyword(opt.name))
          {
            ui::warn_line(std::cout, "Reserved C++ keyword is not allowed.");
            continue;
          }

          break;
        }
      }

      if (opt.name_space.empty())
      {
        opt.name_space = prompt_line("Namespace (optional): ");
      }

      int field_count = 0;

      while (true)
      {
        const std::string input = prompt_line("How many fields? ");

        if (input.empty())
        {
          field_count = 0;
          break;
        }

        try
        {
          field_count = std::stoi(input);
          if (field_count < 0)
            throw std::invalid_argument("negative");

          break;
        }
        catch (...)
        {
          ui::warn_line(std::cout, "Please enter a valid number.");
        }
      }

      for (int i = 0; i < field_count; ++i)
      {
        std::string fname;
        std::string ftype;

        while (true)
        {
          const std::string input =
              prompt_line("Field " + std::to_string(i + 1) + " (name:type): ");

          const auto pos = input.find(':');

          if (pos != std::string::npos)
          {
            fname = mk::trim(input.substr(0, pos));
            ftype = mk::trim(input.substr(pos + 1));
          }
          else
          {
            fname = mk::trim(input);
            ftype.clear();
          }

          if (fname.empty())
          {
            ui::warn_line(std::cout, "Field name cannot be empty.");
            continue;
          }

          if (!mk::is_valid_cpp_identifier(fname))
          {
            ui::warn_line(std::cout, "Invalid field name.");
            continue;
          }

          if (mk::is_reserved_cpp_keyword(fname))
          {
            ui::warn_line(std::cout, "Reserved C++ keyword not allowed.");
            continue;
          }

          bool duplicate = false;
          for (const auto &field : opt.class_options.fields)
          {
            if (field.name == fname)
            {
              duplicate = true;
              break;
            }
          }

          if (duplicate)
          {
            ui::warn_line(std::cout, "Field name already exists.");
            continue;
          }

          if (ftype.empty())
          {
            while (true)
            {
              ftype = mk::trim(prompt_line("Type for '" + fname + "': "));

              if (!ftype.empty())
                break;

              ui::warn_line(std::cout, "Field type cannot be empty.");
            }
          }

          if (ftype == "string")
            ftype = "std::string";

          break;
        }

        opt.class_options.fields.push_back({fname, ftype});
      }

      opt.class_options.interactive = true;
      opt.class_options.with_default_ctor =
          prompt_yes_no("Generate default constructor?", true);
      opt.class_options.with_value_ctor =
          prompt_yes_no("Generate value constructor?", true);
      opt.class_options.with_getters_setters =
          prompt_yes_no("Generate getters/setters?", true);
      opt.class_options.with_copy_move =
          prompt_yes_no("Generate copy/move?", true);
      opt.class_options.with_virtual_destructor =
          prompt_yes_no("Use virtual destructor?", true);

      opt.header_only = prompt_yes_no("Header only?", false);

      if (opt.in.empty())
      {
        opt.in = prompt_line("Target folder (optional): ");
      }

      return run_direct(opt);
    }

    int run_config_prompt(mk::MakeOptions opt)
    {
      if (opt.name.empty())
      {
        while (true)
        {
          opt.name = prompt_line("Config name: ");

          if (opt.name.empty())
          {
            ui::warn_line(std::cout, "Config name is required.");
            continue;
          }

          if (!mk::is_valid_cpp_identifier(opt.name))
          {
            ui::warn_line(std::cout, "Invalid config name.");
            continue;
          }

          if (mk::is_reserved_cpp_keyword(opt.name))
          {
            ui::warn_line(std::cout, "Reserved C++ keyword is not allowed.");
            continue;
          }

          break;
        }
      }

      opt.config_options.with_server =
          prompt_yes_no("Include server section?", opt.config_options.with_server);

      opt.config_options.with_logging =
          prompt_yes_no("Include logging section?", opt.config_options.with_logging);

      opt.config_options.with_waf =
          prompt_yes_no("Include waf section?", opt.config_options.with_waf);

      opt.config_options.with_websocket =
          prompt_yes_no("Include websocket section?", opt.config_options.with_websocket);

      opt.config_options.with_database =
          prompt_yes_no("Include database section?", opt.config_options.with_database);

      if (opt.in.empty())
      {
        opt.in = prompt_line("Target folder (optional): ");
      }

      return run_direct(opt);
    }

    int run_interactive(mk::MakeOptions opt)
    {
      const mk::MakeKind kind = mk::parse_make_kind(opt.kind);

      switch (kind)
      {
      case mk::MakeKind::Class:
        return run_class_prompt(opt);

      case mk::MakeKind::Config:
        return run_config_prompt(opt);

      case mk::MakeKind::Struct:
      case mk::MakeKind::Enum:
      case mk::MakeKind::Function:
      case mk::MakeKind::Lambda:
      case mk::MakeKind::Concept:
      case mk::MakeKind::Exception:
      case mk::MakeKind::Test:
      case mk::MakeKind::Module:
      case mk::MakeKind::Unknown:
      default:
        ui::err_line(std::cout,
                     "Interactive mode is not implemented yet for: " + opt.kind);
        return 1;
      }
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
      ui::warn_line(std::cout, "Examples: vix make class User");
      ui::warn_line(std::cout, "          vix make:class");
      return 1;
    }

    const mk::MakeKind kind = mk::parse_make_kind(opt.kind);
    if (kind == mk::MakeKind::Unknown)
    {
      ui::err_line(std::cout, "Unknown make kind: " + opt.kind);
      return 1;
    }

    if (opt.name.empty())
      return run_interactive(opt);

    return run_direct(opt);
  }

  int MakeCommand::help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix make <kind> <name> [options]\n";
    out << "  vix make:<kind> [name] [options]\n\n";

    out << "Goal:\n";
    out << "  Generate C++ files quickly from the folder you are working in.\n";
    out << "  vix make writes files into the current directory by default,\n";
    out << "  or into a custom folder when --in is provided.\n\n";

    out << "Kinds:\n";
    out << "  class       Generate a class (.hpp + .cpp by default)\n";
    out << "  struct      Generate a plain data struct (.hpp)\n";
    out << "  enum        Generate an enum class with helpers (.hpp)\n";
    out << "  function    Generate a free function (.hpp + .cpp by default)\n";
    out << "  lambda      Generate a modern generic lambda\n";
    out << "  concept     Generate a C++20 concept (.hpp)\n";
    out << "  exception   Generate a std::exception derived type\n";
    out << "  test        Generate a GoogleTest skeleton\n";
    out << "  module      Redirects to the modules workflow\n";
    out << "  config      Generate a JSON runtime configuration file\n\n";

    out << "Options:\n";
    out << "  -d, --dir <path>          Project root (default: current directory)\n";
    out << "  --in <path>               Folder where files should be generated\n";
    out << "  --namespace <ns>          Override namespace\n";
    out << "  --header-only             Generate only header files when supported\n";
    out << "  --print                   Print preview/snippet without writing files\n";
    out << "  --dry-run                 Show what would be generated without writing files\n";
    out << "  --force                   Overwrite existing files\n";
    out << "  --server                  Enable server section for config generation\n";
    out << "  --no-server               Disable server section for config generation\n";
    out << "  --logging                 Enable logging section for config generation\n";
    out << "  --no-logging              Disable logging section for config generation\n";
    out << "  --waf                     Enable waf section for config generation\n";
    out << "  --no-waf                  Disable waf section for config generation\n";
    out << "  --websocket               Enable websocket section for config generation\n";
    out << "  --no-websocket            Disable websocket section for config generation\n";
    out << "  --database                Enable database section for config generation\n";
    out << "  --no-database             Disable database section for config generation\n";
    out << "  -h, --help                Show help\n\n";

    out << "Examples:\n";
    out << "  vix make class User\n";
    out << "  vix make:class\n";
    out << "  vix make:class User\n";
    out << "  vix make struct Claims --namespace auth\n";
    out << "  vix make enum Status --in src/domain\n";
    out << "  vix make function parse_token --in src/auth\n";
    out << "  vix make lambda visit_all --print\n";
    out << "  vix make concept EqualityComparable\n";
    out << "  vix make exception InvalidToken --in src/auth\n";
    out << "  vix make test AuthService\n";
    out << "  vix make config app\n";
    out << "  vix make config app --websocket --database\n";
    out << "  vix make:config\n\n";

    out << "Behavior:\n";
    out << "  - By default, files are generated in the current directory.\n";
    out << "  - Use --in <path> to generate files inside a specific folder.\n";
    out << "  - If the name is missing, Vix starts an interactive prompt.\n";
    out << "  - vix make:<kind> is the interactive generator form.\n";
    out << "  - Existing files are never overwritten unless --force is set.\n";
    out << "  - vix make is a file generator, not a project layout generator.\n";
    out << "  - For structured Vix service or module scaffolding, use: vix modules add <name>\n\n";

    return 0;
  }

} // namespace vix::commands
