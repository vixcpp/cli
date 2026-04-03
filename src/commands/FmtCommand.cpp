/**
 *
 *  @file FmtCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/FmtCommand.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vix::commands
{
  namespace
  {
    struct FmtOptions
    {
      bool check = false;
      bool quiet = false;
      std::vector<std::string> ignore;
      std::vector<fs::path> inputs;
    };

    static bool is_cpp_file(const fs::path &p)
    {
      const std::string ext = p.extension().string();

      return ext == ".cpp" ||
             ext == ".hpp" ||
             ext == ".h" ||
             ext == ".cc" ||
             ext == ".cxx" ||
             ext == ".hh" ||
             ext == ".hxx";
    }

    static std::string shell_quote(const std::string &s)
    {
#ifdef _WIN32
      std::string out = "\"";
      for (char c : s)
      {
        if (c == '"')
          out += "\\\"";
        else
          out += c;
      }
      out += "\"";
      return out;
#else
      std::string out = "'";
      for (char c : s)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out += c;
      }
      out += "'";
      return out;
#endif
    }

    static bool has_clang_format()
    {
#ifdef _WIN32
      return std::system("where clang-format >nul 2>&1") == 0;
#else
      return std::system("which clang-format > /dev/null 2>&1") == 0;
#endif
    }

    static bool path_matches_ignore(const fs::path &p, const std::vector<std::string> &ignore)
    {
      const std::string generic = p.generic_string();

      for (const auto &item : ignore)
      {
        if (item.empty())
          continue;

        if (generic == item)
          return true;

        if (generic.find(item) != std::string::npos)
          return true;
      }

      return false;
    }

    static void add_file_if_valid(
        const fs::path &p,
        const std::vector<std::string> &ignore,
        std::vector<fs::path> &files)
    {
      std::error_code ec;
      const bool exists = fs::exists(p, ec);

      if (ec || !exists)
        return;

      if (!fs::is_regular_file(p, ec))
        return;

      if (!is_cpp_file(p))
        return;

      if (path_matches_ignore(p, ignore))
        return;

      files.push_back(p);
    }

    static void collect_from_directory(
        const fs::path &root,
        const std::vector<std::string> &ignore,
        std::vector<fs::path> &files)
    {
      std::error_code ec;
      if (!fs::exists(root, ec) || ec)
        return;

      fs::recursive_directory_iterator it(
          root,
          fs::directory_options::skip_permission_denied,
          ec);

      fs::recursive_directory_iterator end;

      while (!ec && it != end)
      {
        const fs::path current = it->path();

        if (path_matches_ignore(current, ignore))
        {
          if (it->is_directory(ec))
            it.disable_recursion_pending();

          ++it;
          continue;
        }

        if (it->is_regular_file(ec) && is_cpp_file(current))
          files.push_back(current);

        ++it;
      }
    }

    static std::vector<fs::path> collect_files(const FmtOptions &options)
    {
      std::vector<fs::path> files;

      if (!options.inputs.empty())
      {
        for (const auto &input : options.inputs)
        {
          std::error_code ec;

          if (!fs::exists(input, ec) || ec)
            continue;

          if (fs::is_directory(input, ec))
          {
            collect_from_directory(input, options.ignore, files);
          }
          else
          {
            add_file_if_valid(input, options.ignore, files);
          }
        }
      }
      else
      {
        collect_from_directory("src", options.ignore, files);
        collect_from_directory("include", options.ignore, files);
      }

      std::sort(files.begin(), files.end());
      files.erase(std::unique(files.begin(), files.end()), files.end());

      return files;
    }

    static bool parse_args(const std::vector<std::string> &args, FmtOptions &options)
    {
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (arg == "-h" || arg == "--help")
          return false;

        if (arg == "--check")
        {
          options.check = true;
          continue;
        }

        if (arg == "-q" || arg == "--quiet")
        {
          options.quiet = true;
          continue;
        }

        if (arg == "--ignore")
        {
          if (i + 1 >= args.size())
          {
            vix::cli::util::err_line(std::cerr, "missing value for --ignore");
            return false;
          }

          options.ignore.push_back(args[++i]);
          continue;
        }

        if (arg.rfind("--ignore=", 0) == 0)
        {
          options.ignore.push_back(arg.substr(std::string("--ignore=").size()));
          continue;
        }

        if (!arg.empty() && arg[0] == '-')
        {
          vix::cli::util::err_line(std::cerr, "unknown option: " + arg);
          return false;
        }

        options.inputs.push_back(arg);
      }

      return true;
    }

    static int run_check(const std::vector<fs::path> &files, bool quiet)
    {
      std::size_t failed = 0;

      for (const auto &file : files)
      {
        const std::string cmd =
            "clang-format --dry-run --Werror " + shell_quote(file.string()) + " > /dev/null 2>&1";

        const int rc = std::system(cmd.c_str());

        if (rc != 0)
        {
          ++failed;
          if (!quiet)
            vix::cli::util::warn_line(std::cout, file.string());
        }
      }

      if (failed == 0)
      {
        if (!quiet)
          vix::cli::util::ok_line(std::cout, "All files are properly formatted");
        return 0;
      }

      if (!quiet)
      {
        vix::cli::util::one_line_spacer(std::cout);
        vix::cli::util::err_line(
            std::cout,
            std::to_string(failed) + " file(s) need formatting");
      }

      return 1;
    }

    static int run_format(const std::vector<fs::path> &files, bool quiet)
    {
      std::size_t formatted = 0;
      std::size_t failed = 0;

      for (const auto &file : files)
      {
        const std::string cmd =
            "clang-format -i " + shell_quote(file.string());

        const int rc = std::system(cmd.c_str());

        if (rc == 0)
        {
          ++formatted;
          if (!quiet)
            vix::cli::util::info_line(std::cout, file.string());
        }
        else
        {
          ++failed;
          if (!quiet)
            vix::cli::util::err_line(std::cout, "failed: " + file.string());
        }
      }

      if (!quiet)
      {
        vix::cli::util::one_line_spacer(std::cout);

        if (failed == 0)
        {
          vix::cli::util::ok_line(
              std::cout,
              "Formatted " + std::to_string(formatted) + " file(s)");
        }
        else
        {
          vix::cli::util::warn_line(
              std::cout,
              "Formatted " + std::to_string(formatted) +
                  " file(s), failed on " + std::to_string(failed) + " file(s)");
        }
      }

      return failed == 0 ? 0 : 1;
    }
  } // namespace

  int FmtCommand::run(const std::vector<std::string> &args)
  {
    FmtOptions options;

    if (!parse_args(args, options))
      return help();

    if (!has_clang_format())
    {
      vix::cli::util::err_line(std::cerr, "clang-format not found");
      vix::cli::util::warn_line(std::cerr, "Install clang-format to use 'vix fmt'");
      return 1;
    }

    if (!options.quiet)
      vix::cli::util::section(std::cout, "Format");

    const auto files = collect_files(options);

    if (files.empty())
    {
      if (!options.quiet)
        vix::cli::util::warn_line(std::cout, "No C++ files found");
      return 0;
    }

    if (!options.quiet)
    {
      if (options.check)
        vix::cli::util::info_line(std::cout, "checking " + std::to_string(files.size()) + " file(s)");
      else
        vix::cli::util::info_line(std::cout, "formatting " + std::to_string(files.size()) + " file(s)");
    }

    if (options.check)
      return run_check(files, options.quiet);

    return run_format(files, options.quiet);
  }

  int FmtCommand::help()
  {
    std::cout
        << "Usage: vix fmt [OPTIONS] [files]...\n\n"
        << "Format C++ source files using clang-format.\n\n"

        << "Arguments:\n"
        << "  [files]...   Files or directories to format\n\n"

        << "Options:\n"
        << "  --check            Check if files are formatted\n"
        << "  --ignore <path>    Ignore a file or path pattern\n"
        << "  -q, --quiet        Suppress non-essential output\n"
        << "  -h, --help         Show this help message\n\n"

        << "Behavior:\n"
        << "  If no files are provided, vix fmt scans src/ and include/.\n"
        << "  If a .clang-format file exists, clang-format uses it automatically.\n\n"

        << "Examples:\n"
        << "  vix fmt\n"
        << "  vix fmt src include\n"
        << "  vix fmt main.cpp --check\n"
        << "  vix fmt src --ignore=build --ignore=vendor\n";

    return 0;
  }
} // namespace vix::commands
