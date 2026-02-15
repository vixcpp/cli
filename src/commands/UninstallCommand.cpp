/**
 *
 *  @file UninstallCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */

#include <vix/cli/commands/UninstallCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/utils/Env.hpp>

#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <cstdio>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  namespace
  {
    struct Opt
    {
      bool purge = false;
      bool all = false;               // remove everything we can find
      bool system = false;            // include /usr/local/bin (linux) / typical system dir
      std::optional<fs::path> prefix; // explicit prefix (ex: /usr/local)
      std::optional<fs::path> path;   // explicit full path to remove
    };

    fs::path get_install_json_path()
    {
#ifdef _WIN32
      const char *local = vix::utils::vix_getenv("LOCALAPPDATA");
      if (!local || std::string(local).empty())
        throw std::runtime_error("LOCALAPPDATA not set");
      return fs::path(local) / "Vix" / "install.json";
#else
      const char *home = vix::utils::vix_getenv("HOME");
      if (!home || std::string(home).empty())
        throw std::runtime_error("HOME not set");
      return fs::path(home) / ".local" / "share" / "vix" / "install.json";
#endif
    }

    fs::path get_store_path()
    {
#ifdef _WIN32
      const char *local = vix::utils::vix_getenv("LOCALAPPDATA");
      if (!local || std::string(local).empty())
        throw std::runtime_error("LOCALAPPDATA not set");
      return fs::path(local) / "Vix" / "store";
#else
      const char *home = vix::utils::vix_getenv("HOME");
      if (!home || std::string(home).empty())
        throw std::runtime_error("HOME not set");
      return fs::path(home) / ".vix";
#endif
    }

    std::string trim_copy(std::string s)
    {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || std::isspace(static_cast<unsigned char>(s.back()))))
        s.pop_back();
      size_t i = 0;
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
      return s.substr(i);
    }

#ifndef _WIN32
    std::string exec_capture(const std::string &cmd)
    {
      FILE *pipe = popen(cmd.c_str(), "r");
      if (!pipe)
        return {};
      std::string out;
      char buf[4096];
      while (true)
      {
        const size_t n = std::fread(buf, 1, sizeof(buf), pipe);
        if (n > 0)
          out.append(buf, buf + n);
        if (n < sizeof(buf))
          break;
      }
      pclose(pipe);
      return out;
    }
#endif

    std::string bin_name()
    {
#ifdef _WIN32
      return "vix.exe";
#else
      return "vix";
#endif
    }

    std::optional<fs::path> read_install_dir_from_install_json()
    {
      fs::path p = get_install_json_path();
      if (!fs::exists(p))
        return std::nullopt;

      try
      {
        std::ifstream in(p);
        if (!in)
          return std::nullopt;

        json j;
        in >> j;

        if (!j.contains("install_dir") || !j["install_dir"].is_string())
          return std::nullopt;

        const std::string dir = j["install_dir"].get<std::string>();
        if (dir.empty())
          return std::nullopt;

        return fs::path(dir);
      }
      catch (...)
      {
        return std::nullopt;
      }
    }

    std::optional<fs::path> resolve_path_from_env()
    {
      const char *env = vix::utils::vix_getenv("VIX_CLI_PATH");
      if (!env || std::string(env).empty())
        return std::nullopt;
      return fs::absolute(fs::path(env));
    }

    std::optional<fs::path> resolve_path_from_shell()
    {
#ifdef _WIN32
      // Best effort: rely on PATH
      return std::nullopt;
#else
      // Use "command -v vix"
      const std::string out = trim_copy(exec_capture("command -v vix 2>/dev/null"));
      if (out.empty())
        return std::nullopt;
      return fs::path(out);
#endif
    }

    bool remove_file_best_effort(const fs::path &p, std::string &err, std::error_code &outEc)
    {
      outEc.clear();

      if (p.empty())
        return false;

      std::error_code ec;
      if (!fs::exists(p, ec))
        return false;

      if (fs::is_directory(p, ec))
      {
        err = "refusing to remove directory: " + p.string();
        return false;
      }

      fs::remove(p, ec);
      if (ec)
      {
        outEc = ec;
        err = ec.message();
        return false;
      }

      return true;
    }

#ifndef _WIN32
    bool is_system_path(const fs::path &p)
    {
      const std::string s = p.string();
      return (s.rfind("/usr/", 0) == 0) || (s.rfind("/opt/", 0) == 0) || (s.rfind("/bin/", 0) == 0) || (s.rfind("/sbin/", 0) == 0);
    }
#endif

#ifndef _WIN32
    void suggest_sudo_rm(const fs::path &p)
    {
      if (!is_system_path(p))
        return;
      vix::cli::util::warn_line(std::cerr, "Run: sudo rm -f " + p.string());
    }
#endif

    Opt parse_args(const std::vector<std::string> &args)
    {
      Opt o;

      for (size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "--purge")
        {
          o.purge = true;
          continue;
        }
        if (a == "--all")
        {
          o.all = true;
          continue;
        }
        if (a == "--system")
        {
          o.system = true;
          continue;
        }
        if (a == "--prefix")
        {
          if (i + 1 >= args.size())
            throw std::runtime_error("missing value for --prefix");
          o.prefix = fs::path(args[++i]);
          continue;
        }
        if (a == "--path")
        {
          if (i + 1 >= args.size())
            throw std::runtime_error("missing value for --path");
          o.path = fs::path(args[++i]);
          continue;
        }
        if (a == "-h" || a == "--help")
        {
          // handled by caller
          continue;
        }

        throw std::runtime_error("unknown argument: " + a);
      }

      return o;
    }

    std::vector<fs::path> build_candidate_paths(const Opt &opt)
    {
      std::vector<fs::path> out;

      // 1) Explicit path wins
      if (opt.path.has_value())
        out.push_back(fs::absolute(*opt.path));

      // 2) install.json install_dir
      if (auto dir = read_install_dir_from_install_json())
        out.push_back(fs::path(*dir) / bin_name());

      // 3) VIX_CLI_PATH (can be wrong, but still useful)
      if (auto p = resolve_path_from_env())
        out.push_back(*p);

      // 4) shell resolved (command -v)
      if (auto p = resolve_path_from_shell())
        out.push_back(*p);

      // 5) explicit prefix
      if (opt.prefix.has_value())
        out.push_back(fs::path(*opt.prefix) / "bin" / bin_name());

#ifndef _WIN32
      // 6) system candidates (only if asked)
      if (opt.system || opt.all)
      {
        out.push_back(fs::path("/usr/local/bin") / bin_name());
        out.push_back(fs::path("/usr/bin") / bin_name());
      }
#endif

      // Dedup
      std::vector<fs::path> dedup;
      dedup.reserve(out.size());

      for (const auto &p : out)
      {
        if (p.empty())
          continue;

        fs::path ap = p;
        std::error_code ec;
        ap = fs::weakly_canonical(ap, ec);
        if (ec)
          ap = fs::absolute(p);

        bool seen = false;
        for (const auto &q : dedup)
        {
          if (q == ap)
          {
            seen = true;
            break;
          }
        }
        if (!seen)
          dedup.push_back(ap);
      }

      return dedup;
    }

    void print_post_check()
    {
#ifndef _WIN32
      const std::string out = trim_copy(exec_capture("command -v vix 2>/dev/null"));
      if (!out.empty())
        vix::cli::util::warn_line(std::cerr, "Still found in PATH: " + out);
#endif
    }
  }

  int UninstallCommand::run(const std::vector<std::string> &args)
  {
    try
    {
      // Parse
      bool wantHelp = false;
      for (const auto &a : args)
      {
        if (a == "-h" || a == "--help")
          wantHelp = true;
      }
      if (wantHelp)
        return help();

      const Opt opt = parse_args(args);

      vix::cli::util::section(std::cout, "Uninstall");

      // Candidates
      const auto candidates = build_candidate_paths(opt);
      if (candidates.empty())
        vix::cli::util::warn_line(std::cout, "No candidate paths found to uninstall.");

      bool removedAny = false;

      for (const auto &p : candidates)
      {
        std::string err;
        std::error_code rmEc;

        const bool ok = remove_file_best_effort(p, err, rmEc);
        if (ok)
        {
          removedAny = true;
          vix::cli::util::ok_line(std::cout, "Removed binary: " + p.string());
          if (!opt.all)
            break;
        }
        else
        {
          std::error_code ec;
          if (opt.all && fs::exists(p, ec))
          {
            if (!err.empty())
              vix::cli::util::warn_line(std::cerr, "Could not remove: " + p.string() + " (" + err + ")");

#ifndef _WIN32
            // Permission denied -> tell user the exact command
            if (rmEc == std::errc::permission_denied)
              suggest_sudo_rm(p);
#endif
          }
        }
      }

      // install.json
      fs::path installJson = get_install_json_path();
      std::error_code ec;
      if (fs::exists(installJson, ec))
      {
        fs::remove(installJson, ec);
        if (!ec)
          vix::cli::util::ok_line(std::cout, "Removed install.json");
      }

      if (opt.purge)
      {
        fs::path store = get_store_path();
        if (fs::exists(store, ec))
        {
          fs::remove_all(store, ec);
          if (!ec)
            vix::cli::util::ok_line(std::cout, "Purged local store/cache");
        }
      }

      vix::cli::util::info_line(std::cout, "You may need to run: hash -r (bash/zsh)");
      vix::cli::util::ok_line(std::cout, removedAny ? "Uninstall complete." : "Uninstall finished (nothing removed).");

      // Helpful: show if another vix still exists in PATH
      print_post_check();

      vix::cli::util::warn_line(std::cerr, "Tip: run: hash -r (bash/zsh) or restart your terminal.");
      return 0;
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, ex.what());
      return 1;
    }
  }

  int UninstallCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix uninstall [options]\n\n"
        << "Description:\n"
        << "  Remove the Vix CLI binary and install metadata.\n\n"
        << "Options:\n"
        << "  --purge           Remove local store/cache as well\n"
        << "  --all             Try to remove every detected vix in common locations\n"
        << "  --system          Include system locations (/usr/local/bin, /usr/bin)\n"
        << "  --prefix <dir>    Remove <dir>/bin/vix (example: /usr/local)\n"
        << "  --path <file>     Remove the binary at an explicit path\n\n"
        << "Notes:\n"
        << "  - Default behavior removes the best detected match, then stops.\n"
        << "  - If another vix exists earlier in PATH, it will still be found.\n";
    return 0;
  }
}
