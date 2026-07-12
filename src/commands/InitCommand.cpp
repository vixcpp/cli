/**
 * @file InitCommand.cpp
 */
#include <vix/cli/commands/InitCommand.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vix::commands
{
  namespace fs = std::filesystem;

  namespace
  {
    struct Options
    {
      std::string name;
      std::string standard{"c++20"};
      bool library{false};
      bool force{false};
      bool help{false};
    };

    std::string take_value(const std::vector<std::string> &args, std::size_t &i, const std::string &flag)
    {
      if (i + 1 >= args.size())
        throw std::runtime_error("missing value for " + flag);
      return args[++i];
    }

    std::string normalize_name(std::string raw)
    {
      std::string out;
      bool lastDash = false;
      for (char c : raw)
      {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (std::isalnum(ch))
        {
          out.push_back(static_cast<char>(std::tolower(ch)));
          lastDash = false;
        }
        else if (!lastDash && !out.empty())
        {
          out.push_back('-');
          lastDash = true;
        }
      }
      while (!out.empty() && out.back() == '-')
        out.pop_back();
      if (out.empty() || !std::isalpha(static_cast<unsigned char>(out.front())))
        out = "vix-project";
      return out;
    }

    std::string quote_string(const std::string &value)
    {
      std::string out = "\"";
      for (char c : value)
      {
        if (c == '\\')
          out += "\\\\";
        else if (c == '"')
          out += "\\\"";
        else
          out.push_back(c);
      }
      out += "\"";
      return out;
    }

    std::vector<std::string> cpp_sources(const fs::path &dir)
    {
      std::vector<std::string> out;
      std::error_code ec;
      for (const auto &entry : fs::directory_iterator(dir, ec))
      {
        if (ec || !entry.is_regular_file())
          continue;
        const fs::path p = entry.path();
        if (p.extension() == ".cpp" || p.extension() == ".cc" || p.extension() == ".cxx")
          out.push_back(p.filename().string());
      }
      std::sort(out.begin(), out.end());
      return out;
    }

    Options parse(const std::vector<std::string> &args)
    {
      Options o;
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];
        if (a == "-h" || a == "--help")
          o.help = true;
        else if (a == "--name")
          o.name = take_value(args, i, a);
        else if (a.rfind("--name=", 0) == 0)
          o.name = a.substr(7);
        else if (a == "--standard")
          o.standard = take_value(args, i, a);
        else if (a.rfind("--standard=", 0) == 0)
          o.standard = a.substr(11);
        else if (a == "--lib" || a == "--library")
          o.library = true;
        else if (a == "--force")
          o.force = true;
        else
          throw std::runtime_error("unknown option: " + a);
      }
      return o;
    }
  }

  int InitCommand::run(const std::vector<std::string> &args)
  {
    Options opt;
    try
    {
      opt = parse(args);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, ex.what());
      return 2;
    }

    if (opt.help)
      return help();

    const fs::path cwd = fs::current_path();
    const fs::path app = cwd / "vix.app";
    if (fs::exists(app) && !opt.force)
    {
      vix::cli::util::err_line(std::cerr, "vix.app already exists");
      vix::cli::util::warn_line(std::cerr, "Use --force to replace it.");
      return 1;
    }

    const std::string name = normalize_name(opt.name.empty() ? cwd.filename().string() : opt.name);
    const auto sources = cpp_sources(cwd);

    std::ofstream out(app, std::ios::binary | std::ios::trunc);
    if (!out)
    {
      vix::cli::util::err_line(std::cerr, "cannot write vix.app");
      return 1;
    }

    out << "name = " << quote_string(name) << "\n";
    out << "type = " << quote_string(opt.library ? "library" : "executable") << "\n";
    out << "standard = " << quote_string(opt.standard) << "\n";
    if (!sources.empty())
    {
      out << "sources = [";
      for (std::size_t i = 0; i < sources.size(); ++i)
      {
        if (i)
          out << ", ";
        out << quote_string(sources[i]);
      }
      out << "]\n";
    }

    vix::cli::util::ok_line(std::cout, "Initialized Vix project");
    std::cout << "\n  Created\n  vix.app\n\n  Next\n  vix install <package-or-url>\n  vix run main.cpp\n";
    return 0;
  }

  int InitCommand::help()
  {
    std::cout << "vix init\n"
              << "Initialize the current directory as a minimal Vix project.\n\n"
              << "Usage\n"
              << "  vix init [options]\n\n"
              << "Options\n"
              << "  --name <name>        Project name\n"
              << "  --lib                Create a library manifest\n"
              << "  --standard <std>     C++ standard, default: c++20\n"
              << "  --force              Replace an existing vix.app\n"
              << "  -h, --help           Show this help\n";
    return 0;
  }
}
