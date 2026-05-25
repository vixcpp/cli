/**
 *
 *  @file DoctorCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/DoctorCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <cctype>
#include <algorithm>
#include <stdexcept>
#include <cstdio>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    std::string trim_copy(std::string s)
    {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || std::isspace(static_cast<unsigned char>(s.back()))))
        s.pop_back();
      size_t i = 0;
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
      return s.substr(i);
    }

    bool starts_with(const std::string &s, const std::string &p)
    {
      return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
    }

    fs::path stats_file()
    {
#ifdef _WIN32
      if (const char *p = vix::utils::vix_getenv("LOCALAPPDATA"))
        if (*p)
          return fs::path(p) / "Vix" / "install.json";
      return fs::current_path() / "install.json";
#else
      if (const char *home = vix::utils::vix_getenv("HOME"))
        if (*home)
          return fs::path(home) / ".local" / "share" / "vix" / "install.json";
      return fs::current_path() / "install.json";
#endif
    }

    fs::path current_exe_path()
    {
      if (const char *p = vix::utils::vix_getenv("VIX_CLI_PATH"))
        if (*p)
          return fs::path(p);
#ifdef _WIN32
      return fs::path("vix.exe");
#else
      return fs::path("vix");
#endif
    }

    std::string detect_os()
    {
#ifdef _WIN32
      return "windows";
#elif __APPLE__
      return "macos";
#else
      return "linux";
#endif
    }

    std::string detect_arch()
    {
#if defined(__x86_64__) || defined(_M_X64)
      return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
      return "aarch64";
#else
      return "unknown";
#endif
    }

    std::optional<std::string> extract_version_token(const std::string &text)
    {
      // Find vX.Y.Z in text
      std::string t = text;
      t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());

      auto looks_like = [](const std::string &x) -> bool
      {
        if (x.size() < 6 || x[0] != 'v')
          return false;
        int dots = 0;
        for (size_t i = 1; i < x.size(); ++i)
        {
          const char c = x[i];
          if (c == '.')
          {
            dots++;
            continue;
          }
          if (std::isdigit(static_cast<unsigned char>(c)))
            continue;
          if (c == '-' || c == '+' || std::isalpha(static_cast<unsigned char>(c)))
            continue;
          return false;
        }
        return dots >= 2;
      };

      std::string cur;
      std::vector<std::string> parts;
      for (char ch : t)
      {
        if (std::isspace(static_cast<unsigned char>(ch)))
        {
          if (!cur.empty())
          {
            parts.push_back(cur);
            cur.clear();
          }
          continue;
        }
        cur.push_back(ch);
      }
      if (!cur.empty())
        parts.push_back(cur);

      for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i)
      {
        if (looks_like(parts[static_cast<size_t>(i)]))
          return parts[static_cast<size_t>(i)];
      }
      return std::nullopt;
    }

    bool have_cmd(const std::string &name)
    {
#ifdef _WIN32
      std::string cmd = "where " + name + " >nul 2>&1";
      return std::system(cmd.c_str()) == 0;
#else
      std::string cmd = "command -v " + name + " >/dev/null 2>&1";
      return std::system(cmd.c_str()) == 0;
#endif
    }

    std::optional<std::string> run_capture(const std::string &cmd)
    {
#ifdef _WIN32
      FILE *pipe = _popen(cmd.c_str(), "r");
#else
      FILE *pipe = popen(cmd.c_str(), "r");
#endif
      if (!pipe)
        return std::nullopt;

      std::string out;
      char buf[2048];
      while (std::fgets(buf, sizeof(buf), pipe))
        out += buf;

#ifdef _WIN32
      _pclose(pipe);
#else
      pclose(pipe);
#endif

      out = trim_copy(out);
      if (out.empty())
        return std::nullopt;
      return out;
    }

    std::optional<fs::path> which_vix();

    std::optional<std::string> vix_version_from_self()
    {
#ifdef _WIN32
      if (auto w = which_vix())
        return extract_version_token(run_capture("\"" + w->string() + "\" --version 2>nul").value_or(""));
      return extract_version_token(run_capture("vix --version 2>nul").value_or(""));
#else
      return extract_version_token(run_capture("vix --version 2>/dev/null").value_or(""));
#endif
    }

    bool is_writable_dir(const fs::path &dir)
    {
      std::error_code ec;
      fs::create_directories(dir, ec);
      if (ec)
        return false;

      fs::path probe = dir / ".vix_doctor_write.tmp";
      std::ofstream out(probe.string(), std::ios::binary);
      if (!out)
        return false;
      out << "x";
      out.close();
      fs::remove(probe, ec);
      return true;
    }

    std::vector<std::string> split_path_list(const std::string &pathEnv)
    {
      std::vector<std::string> out;
      std::string cur;
#ifdef _WIN32
      const char sep = ';';
#else
      const char sep = ':';
#endif
      for (char c : pathEnv)
      {
        if (c == sep)
        {
          if (!cur.empty())
            out.push_back(cur);
          cur.clear();
        }
        else
        {
          cur.push_back(c);
        }
      }
      if (!cur.empty())
        out.push_back(cur);
      return out;
    }

    std::string normalize_dir(std::string s)
    {
#ifdef _WIN32
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      while (!s.empty() && (s.back() == '\\' || s.back() == '/'))
        s.pop_back();
#else
      while (!s.empty() && s.back() == '/')
        s.pop_back();
#endif
      return s;
    }

    bool path_contains_dir(const std::string &dir)
    {
      const char *p = vix::utils::vix_getenv("PATH");
      if (!p)
        return false;

      const std::string want = normalize_dir(dir);
      const auto segs = split_path_list(std::string(p));
      for (auto s : segs)
      {
        if (normalize_dir(trim_copy(s)) == want)
          return true;
      }
      return false;
    }

    json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());
      json j;
      in >> j;
      return j;
    }

    void print_dep_status(const std::string &name, bool ok, const std::string &hintIfMissing)
    {
      if (ok)
        vix::cli::util::ok_line(std::cout, name + ": ok");
      else
      {
        vix::cli::util::err_line(std::cerr, name + ": missing");
        if (!hintIfMissing.empty())
          vix::cli::util::warn_line(std::cerr, hintIfMissing);
      }
    }

    std::optional<std::string> github_latest_tag(const std::string &repo)
    {
      // Uses best available tool:
      // - Linux/macOS: curl or wget
      // - Windows: PowerShell Invoke-RestMethod
#ifdef _WIN32
      // Note: keep it single-line friendly for _popen.
      const std::string ps =
          "powershell -NoProfile -Command \""
          "$r=Invoke-RestMethod -Headers @{ 'User-Agent'='vix-doctor' } "
          "-Uri 'https://api.github.com/repos/" +
          repo +
          "/releases/latest'; "
          "if($r.tag_name){Write-Output $r.tag_name}\"";
      auto out = run_capture(ps + " 2>nul");
      if (!out)
        return std::nullopt;
      auto tag = trim_copy(*out);
      if (tag.empty())
        return std::nullopt;
      return tag;
#else
      if (have_cmd("curl"))
      {
        auto out = run_capture(
            "curl -fSsL -H 'User-Agent: vix-doctor' "
            "'https://api.github.com/repos/" +
            repo +
            "/releases/latest' 2>/dev/null");
        if (!out)
          return std::nullopt;

        // Extract "tag_name":"vX.Y.Z" without jq
        const std::string &body = *out;
        const std::string key = "\"tag_name\"";
        const auto pos = body.find(key);
        if (pos == std::string::npos)
          return std::nullopt;

        const auto colon = body.find(':', pos);
        if (colon == std::string::npos)
          return std::nullopt;

        const auto q1 = body.find('"', colon);
        if (q1 == std::string::npos)
          return std::nullopt;

        const auto q2 = body.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 <= q1 + 1)
          return std::nullopt;

        const std::string tag = body.substr(q1 + 1, q2 - (q1 + 1));
        if (tag.empty())
          return std::nullopt;
        return tag;
      }

      if (have_cmd("wget"))
      {
        auto out = run_capture(
            "wget -qO- "
            "'https://api.github.com/repos/" +
            repo +
            "/releases/latest' 2>/dev/null");
        if (!out)
          return std::nullopt;

        const std::string &body = *out;
        const std::string key = "\"tag_name\"";
        const auto pos = body.find(key);
        if (pos == std::string::npos)
          return std::nullopt;

        const auto colon = body.find(':', pos);
        if (colon == std::string::npos)
          return std::nullopt;

        const auto q1 = body.find('"', colon);
        if (q1 == std::string::npos)
          return std::nullopt;

        const auto q2 = body.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 <= q1 + 1)
          return std::nullopt;

        const std::string tag = body.substr(q1 + 1, q2 - (q1 + 1));
        if (tag.empty())
          return std::nullopt;
        return tag;
      }

      return std::nullopt;
#endif
    }

    struct Options
    {
      bool jsonOut = false;
      bool online = false;
      bool production = false;
      bool toolchain = false;
      std::string repo = "vixcpp/vix";
    };

    struct ToolchainInfo
    {
      fs::path cliPath{};
      std::string cliVersion{};
      std::optional<fs::path> vixDir{};
      std::optional<fs::path> cmakePrefixPath{};
    };

    std::optional<fs::path> env_path(const char *name)
    {
      if (const char *value = vix::utils::vix_getenv(name))
      {
        if (*value)
          return fs::path(value);
      }

      return std::nullopt;
    }

    std::optional<fs::path> first_cmake_prefix_path()
    {
      if (const char *value = vix::utils::vix_getenv("CMAKE_PREFIX_PATH"))
      {
        if (!*value)
          return std::nullopt;

        std::string raw(value);

#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif

        const auto pos = raw.find(sep);

        if (pos != std::string::npos)
          raw = raw.substr(0, pos);

        raw = trim_copy(raw);

        if (!raw.empty())
          return fs::path(raw);
      }

      return std::nullopt;
    }

    fs::path canonical_or_absolute(const fs::path &path)
    {
      std::error_code ec;

      fs::path resolved = fs::weakly_canonical(path, ec);

      if (!ec && !resolved.empty())
        return resolved;

      resolved = fs::absolute(path, ec);

      if (!ec && !resolved.empty())
        return resolved;

      return path;
    }

    bool same_installation_root(
        const fs::path &a,
        const fs::path &b)
    {
      if (a.empty() || b.empty())
        return false;

      const fs::path left = canonical_or_absolute(a);
      const fs::path right = canonical_or_absolute(b);

      if (left == right)
        return true;

      const std::string ls = left.string();
      const std::string rs = right.string();

      return ls.find(rs) == 0 || rs.find(ls) == 0;
    }

    int doctor_toolchain()
    {
      vix::cli::util::section(std::cout, "Toolchain Consistency");

      ToolchainInfo info;

      if (auto path = which_vix())
        info.cliPath = *path;
      else
        info.cliPath = current_exe_path();

      info.cliVersion = vix_version_from_self().value_or("unknown");
      info.vixDir = env_path("Vix_DIR");
      info.cmakePrefixPath = first_cmake_prefix_path();

      vix::cli::util::section(std::cout, "CLI");
      vix::cli::util::kv(std::cout, "Path", info.cliPath.string());
      vix::cli::util::kv(std::cout, "Version", info.cliVersion);

      vix::cli::util::section(std::cout, "CMake Package");
      vix::cli::util::kv(
          std::cout,
          "Vix_DIR",
          info.vixDir ? info.vixDir->string() : "(not set)");

      vix::cli::util::kv(
          std::cout,
          "CMAKE_PREFIX_PATH",
          info.cmakePrefixPath ? info.cmakePrefixPath->string() : "(not set)");

      bool ok = true;

      if (info.vixDir)
      {
        if (!same_installation_root(info.cliPath.parent_path(), *info.vixDir))
          ok = false;
      }

      if (info.cmakePrefixPath)
      {
        if (!same_installation_root(info.cliPath.parent_path(), *info.cmakePrefixPath))
          ok = false;
      }

      vix::cli::util::section(std::cout, "Result");

      if (ok)
      {
        vix::cli::util::ok_line(
            std::cout,
            "Vix CLI and CMake package appear to come from the same installation");

        return 0;
      }

      vix::cli::util::warn_line(
          std::cerr,
          "Vix CLI and Vix libraries come from different installations");

      vix::cli::util::kv(std::cerr, "CLI", info.cliPath.string());

      if (info.vixDir)
        vix::cli::util::kv(std::cerr, "Vix_DIR", info.vixDir->string());

      if (info.cmakePrefixPath)
        vix::cli::util::kv(std::cerr, "CMAKE_PREFIX_PATH", info.cmakePrefixPath->string());

      vix::cli::util::warn_line(
          std::cerr,
          "This can cause confusing builds.");

      vix::cli::util::warn_line(
          std::cerr,
          "Fix: use the same Vix installation for the CLI and CMake package.");

      vix::cli::util::warn_line(
          std::cerr,
          "Example:");

      vix::cli::util::warn_line(
          std::cerr,
          "  export Vix_DIR=" + info.cliPath.parent_path().string());

      vix::cli::util::warn_line(
          std::cerr,
          "  export CMAKE_PREFIX_PATH=" + info.cliPath.parent_path().string());

      return 1;
    }

    Options parse_args(const std::vector<std::string> &args)
    {
      Options o;

      // repo override: --repo owner/name
      for (size_t i = 0; i < args.size(); ++i)
      {
        const auto &a = args[i];
        if (a == "production")
        {
          o.production = true;
          continue;
        }
        if (a == "toolchain")
        {
          o.toolchain = true;
          continue;
        }
        if (a == "--json")
        {
          o.jsonOut = true;
          continue;
        }
        if (a == "--online")
        {
          o.online = true;
          continue;
        }
        if (a == "--repo")
        {
          if (i + 1 >= args.size())
            throw std::runtime_error("--repo requires a value (owner/name)");
          o.repo = args[i + 1];
          ++i;
          continue;
        }
        if (starts_with(a, "--repo="))
        {
          o.repo = a.substr(std::string("--repo=").size());
          if (o.repo.empty())
            throw std::runtime_error("--repo=VALUE cannot be empty");
          continue;
        }
        if (a == "-h" || a == "--help")
          continue;

        throw std::runtime_error("unknown argument: " + a);
      }
      return o;
    }

    std::optional<fs::path> which_vix()
    {
#ifdef _WIN32
      // Take first result from `where vix`
      auto out = run_capture("where vix 2>nul");
      if (!out)
        return std::nullopt;

      std::string s = *out;
      s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());

      // first line
      const auto nl = s.find('\n');
      const std::string first = trim_copy(nl == std::string::npos ? s : s.substr(0, nl));
      if (first.empty())
        return std::nullopt;

      return fs::path(first);
#else
      auto out = run_capture("command -v vix 2>/dev/null");
      if (!out)
        return std::nullopt;
      return fs::path(*out);
#endif
    }

    std::string shell_quote(const std::string &s)
    {
#ifdef _WIN32
      return "\"" + s + "\"";
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

    std::string lower_copy(std::string s)
    {
      std::transform(
          s.begin(),
          s.end(),
          s.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return s;
    }

    std::optional<std::string> read_project_name()
    {
      const fs::path vixJson = fs::current_path() / "vix.json";

      if (fs::exists(vixJson))
      {
        try
        {
          const auto j = read_json_or_throw(vixJson);

          if (j.is_object() && j.contains("name") && j["name"].is_string())
          {
            const std::string name = trim_copy(j["name"].get<std::string>());

            if (!name.empty())
              return name;
          }
        }
        catch (...)
        {
        }
      }

      for (const auto &entry : fs::directory_iterator(fs::current_path()))
      {
        if (!entry.is_regular_file())
          continue;

        const auto p = entry.path();

        if (p.extension() == ".vix")
        {
          return p.stem().string();
        }
      }

      const auto current = fs::current_path().filename().string();

      if (!current.empty())
        return current;

      return std::nullopt;
    }

    std::optional<fs::path> detect_build_dir()
    {
      const std::vector<fs::path> candidates = {
          fs::current_path() / "build-ninja",
          fs::current_path() / "build-release",
          fs::current_path() / "build",
          fs::current_path() / "cmake-build-debug",
          fs::current_path() / "cmake-build-release"};

      for (const auto &candidate : candidates)
      {
        if (fs::exists(candidate) && fs::is_directory(candidate))
          return candidate;
      }

      return std::nullopt;
    }

    std::optional<fs::path> detect_binary_path(const std::string &projectName)
    {
      const auto buildDir = detect_build_dir();

      if (!buildDir)
        return std::nullopt;

#ifdef _WIN32
      const auto exe = *buildDir / (projectName + ".exe");
#else
      const auto exe = *buildDir / projectName;
#endif

      if (fs::exists(exe))
        return exe;

      for (const auto &entry : fs::recursive_directory_iterator(*buildDir))
      {
        if (!entry.is_regular_file())
          continue;

#ifdef _WIN32
        if (entry.path().filename() == projectName + ".exe")
          return entry.path();
#else
        if (entry.path().filename() == projectName)
          return entry.path();
#endif
      }

      return exe;
    }

#ifndef _WIN32
    bool process_running_for_binary(const fs::path &binary)
    {
      if (binary.empty())
        return false;

      const std::string cmd =
          "pgrep -f " + shell_quote(binary.string()) + " >/dev/null 2>&1";

      return std::system(cmd.c_str()) == 0;
    }

    std::vector<std::string> list_systemd_services()
    {
      std::vector<std::string> services;

      auto out = run_capture(
          "systemctl list-unit-files --type=service --no-legend 2>/dev/null | "
          "awk '{print $1}'");

      if (!out)
        return services;

      std::string current;

      for (char c : *out)
      {
        if (c == '\n' || c == '\r')
        {
          current = trim_copy(current);

          if (!current.empty())
            services.push_back(current);

          current.clear();
          continue;
        }

        current.push_back(c);
      }

      current = trim_copy(current);

      if (!current.empty())
        services.push_back(current);

      return services;
    }

    std::optional<std::string> systemctl_property(
        const std::string &service,
        const std::string &property)
    {
      auto out = run_capture(
          "systemctl show " +
          shell_quote(service) +
          " -p " +
          shell_quote(property) +
          " --value 2>/dev/null");

      if (!out)
        return std::nullopt;

      const auto value = trim_copy(*out);

      if (value.empty())
        return std::nullopt;

      return value;
    }

    bool service_points_to_project(
        const std::string &service,
        const fs::path &projectDir,
        const std::optional<fs::path> &binary)
    {
      const auto workingDir = systemctl_property(service, "WorkingDirectory");
      const auto execStart = systemctl_property(service, "ExecStart");

      std::error_code ec;
      const fs::path canonicalProject =
          fs::weakly_canonical(projectDir, ec);

      if (workingDir && !workingDir->empty())
      {
        std::error_code wdEc;
        const fs::path canonicalWorkingDir =
            fs::weakly_canonical(fs::path(*workingDir), wdEc);

        if (!wdEc && !ec && canonicalWorkingDir == canonicalProject)
          return true;

        if (trim_copy(*workingDir) == projectDir.string())
          return true;
      }

      if (binary && execStart && !execStart->empty())
      {
        const std::string exec = *execStart;
        const std::string bin = binary->string();

        if (!bin.empty() && exec.find(bin) != std::string::npos)
          return true;

        if (binary->has_filename())
        {
          const std::string filename = binary->filename().string();

          if (!filename.empty() && exec.find(filename) != std::string::npos)
          {
            if (workingDir && trim_copy(*workingDir) == projectDir.string())
              return true;
          }
        }
      }

      return false;
    }

    bool systemd_service_exists(const std::string &service)
    {
      const std::string cmd =
          "systemctl status " + shell_quote(service) + " >/dev/null 2>&1";

      return std::system(cmd.c_str()) == 0;
    }

    std::optional<std::string> detect_systemd_service(
        const std::string &projectName,
        const fs::path &projectDir,
        const std::optional<fs::path> &binary)
    {
      const std::string lower = lower_copy(projectName);
      const std::string exactService = lower + ".service";

      if (systemd_service_exists(exactService) &&
          service_points_to_project(exactService, projectDir, binary))
      {
        return exactService;
      }

      const auto services = list_systemd_services();

      for (const auto &service : services)
      {
        if (service_points_to_project(service, projectDir, binary))
          return service;
      }

      return std::nullopt;
    }

    std::optional<std::string> detect_listening_port_for_binary(const fs::path &binary)
    {
      if (binary.empty() || !have_cmd("ss"))
        return std::nullopt;

      auto out = run_capture(
          "ss -tulpn 2>/dev/null | grep " +
          shell_quote(binary.filename().string()) +
          " | head -1");

      if (!out)
        return std::nullopt;

      const std::string line = *out;
      const auto colon = line.find(':');

      if (colon == std::string::npos)
        return std::nullopt;

      std::size_t start = colon + 1;
      std::size_t end = start;

      while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])))
        ++end;

      if (end == start)
        return std::nullopt;

      return line.substr(start, end - start);
    }

    std::optional<std::string> detect_nginx_domain(const std::string &projectName)
    {
      const std::string lower = lower_copy(projectName);

      auto out = run_capture(
          "grep -R \"server_name\" /etc/nginx/sites-available /etc/nginx/sites-enabled "
          "2>/dev/null | grep -i " +
          shell_quote(lower) +
          " | head -1");

      if (!out)
        return std::nullopt;

      const std::string line = *out;
      const std::string key = "server_name";
      const auto pos = line.find(key);

      if (pos == std::string::npos)
        return std::nullopt;

      std::string value = trim_copy(line.substr(pos + key.size()));

      if (!value.empty() && value.back() == ';')
        value.pop_back();

      return trim_copy(value);
    }

    bool nginx_config_exists_for_project(const std::string &projectName)
    {
      const std::string lower = lower_copy(projectName);

      const std::string cmd =
          "grep -R " +
          shell_quote(lower) +
          " /etc/nginx/sites-available /etc/nginx/sites-enabled >/dev/null 2>&1";

      return std::system(cmd.c_str()) == 0;
    }

    bool tls_config_exists_for_domain(const std::string &domain)
    {
      if (domain.empty())
        return false;

      const fs::path cert =
          fs::path("/etc/letsencrypt/live") / domain / "fullchain.pem";

      return fs::exists(cert);
    }

    bool local_health_ok(const std::string &port)
    {
      if (port.empty())
        return false;

      if (!have_cmd("curl"))
        return false;

      const std::string cmd =
          "curl -fsS --max-time 3 http://127.0.0.1:" + port + "/ >/dev/null 2>&1";

      return std::system(cmd.c_str()) == 0;
    }

    bool public_health_ok(const std::string &domain)
    {
      if (domain.empty())
        return false;

      if (!have_cmd("curl"))
        return false;

      const std::string cmd =
          "curl -fsS --max-time 5 https://" + domain + "/ >/dev/null 2>&1";

      return std::system(cmd.c_str()) == 0;
    }

    enum class ReadinessStatus
    {
      Ok,
      Warn,
      Fail
    };

    struct ReadinessItem
    {
      ReadinessStatus status{ReadinessStatus::Warn};
      std::string label;
      int points{0};
      int maxPoints{0};
      std::string fix;
    };

    std::string readiness_status_text(ReadinessStatus status)
    {
      switch (status)
      {
      case ReadinessStatus::Ok:
        return "OK";

      case ReadinessStatus::Warn:
        return "WARN";

      case ReadinessStatus::Fail:
        return "FAIL";
      }

      return "WARN";
    }

    std::optional<bool> read_bool_from_object(
        const json &object,
        const std::string &key)
    {
      if (!object.is_object() ||
          !object.contains(key) ||
          !object[key].is_boolean())
      {
        return std::nullopt;
      }

      return object[key].get<bool>();
    }

    json read_production_child_object(const std::string &key)
    {
      const fs::path vixJson = fs::current_path() / "vix.json";

      if (!fs::exists(vixJson))
        return json::object();

      try
      {
        const json root = read_json_or_throw(vixJson);

        if (!root.is_object() ||
            !root.contains("production") ||
            !root["production"].is_object())
        {
          return json::object();
        }

        const json &production = root["production"];

        if (!production.contains(key) ||
            !production[key].is_object())
        {
          return json::object();
        }

        return production[key];
      }
      catch (...)
      {
        return json::object();
      }
    }

    bool production_health_websocket_configured()
    {
      const json health = read_production_child_object("health");

      return health.is_object() &&
             health.contains("websocket") &&
             health["websocket"].is_string() &&
             !trim_copy(health["websocket"].get<std::string>()).empty();
    }

    bool production_deploy_rollback_configured()
    {
      const json deploy = read_production_child_object("deploy");

      if (const auto rollback = read_bool_from_object(deploy, "rollback"))
        return *rollback;

      return false;
    }

    void print_readiness_item(
        const ReadinessItem &item)
    {
      const std::string prefix = readiness_status_text(item.status);

      vix::cli::util::kv(
          std::cout,
          prefix,
          item.label + " (" +
              std::to_string(item.points) + "/" +
              std::to_string(item.maxPoints) + ")");

      if (item.status != ReadinessStatus::Ok && !item.fix.empty())
      {
        vix::cli::util::warn_line(
            std::cerr,
            "Fix: " + item.fix);
      }
    }

    int run_production_doctor(bool jsonOut)
    {
      vix::cli::util::section(std::cout, "Production Doctor");

      const auto projectName = read_project_name().value_or("unknown");
      const auto buildDir = detect_build_dir();
      const auto binary = detect_binary_path(projectName);

      const auto service = detect_systemd_service(
          projectName,
          fs::current_path(),
          binary);

      const bool serviceExists = service.has_value();

      const auto serviceStatus =
          serviceExists ? systemctl_property(*service, "ActiveState") : std::nullopt;

      const auto restartPolicy =
          serviceExists ? systemctl_property(*service, "Restart") : std::nullopt;

      const auto workingDir =
          serviceExists ? systemctl_property(*service, "WorkingDirectory") : std::nullopt;

      const bool binaryExists = binary && fs::exists(*binary);
      const bool running = binaryExists && process_running_for_binary(*binary);

      const auto port =
          binaryExists ? detect_listening_port_for_binary(*binary) : std::nullopt;

      const bool nginxExists = nginx_config_exists_for_project(projectName);
      const auto domain = detect_nginx_domain(projectName);
      const bool tls = domain && tls_config_exists_for_domain(*domain);

      const bool localHealth = port && local_health_ok(*port);
      const bool publicHealth = domain && public_health_ok(*domain);

      const bool websocketHealthConfigured =
          production_health_websocket_configured();

      const bool deployRollbackConfigured =
          production_deploy_rollback_configured();

      std::vector<ReadinessItem> readiness;

      auto add_item =
          [&](ReadinessStatus status,
              std::string label,
              int maxPoints,
              std::string fix)
      {
        readiness.push_back(
            ReadinessItem{
                status,
                label,
                status == ReadinessStatus::Ok ? maxPoints : 0,
                maxPoints,
                fix});
      };

      add_item(
          serviceExists ? ReadinessStatus::Ok : ReadinessStatus::Fail,
          "service installed",
          15,
          "run `vix service install`");

      add_item(
          running ? ReadinessStatus::Ok : ReadinessStatus::Fail,
          "service running",
          15,
          "run `vix service start` or `vix service status`");

      add_item(
          binaryExists ? ReadinessStatus::Ok : ReadinessStatus::Fail,
          "binary exists",
          10,
          "run `vix build`");

      add_item(
          nginxExists ? ReadinessStatus::Ok : ReadinessStatus::Fail,
          "nginx proxy configured",
          15,
          "run `vix proxy nginx init`");

      add_item(
          tls ? ReadinessStatus::Ok : ReadinessStatus::Fail,
          "TLS enabled",
          15,
          domain ? "run `vix proxy nginx certbot`" : "configure production.proxy.domain in vix.json");

      add_item(
          localHealth ? ReadinessStatus::Ok : ReadinessStatus::Fail,
          "local health check",
          10,
          "configure production.health.local and run `vix health local`");

      add_item(
          publicHealth ? ReadinessStatus::Ok : ReadinessStatus::Fail,
          "public health check",
          10,
          "configure production.health.public and run `vix health public`");

      add_item(
          websocketHealthConfigured ? ReadinessStatus::Ok : ReadinessStatus::Warn,
          "websocket health configured",
          5,
          "add production.health.websocket to vix.json");

      add_item(
          deployRollbackConfigured ? ReadinessStatus::Ok : ReadinessStatus::Warn,
          "deploy rollback configured",
          5,
          "add production.deploy.rollback=true to vix.json");

      int readinessScore = 0;
      int readinessMax = 0;

      for (const auto &item : readiness)
      {
        readinessScore += item.points;
        readinessMax += item.maxPoints;
      }

      vix::cli::util::kv(std::cout, "App", projectName);
      vix::cli::util::kv(std::cout, "Status", running ? "running" : "not running");
      vix::cli::util::kv(std::cout, "Build dir", buildDir ? buildDir->string() : "not found");
      vix::cli::util::kv(std::cout, "Binary", binary ? binary->string() : "not found");
      vix::cli::util::kv(std::cout, "Binary exists", binaryExists ? "yes" : "no");
      vix::cli::util::kv(std::cout, "Service", service ? *service : "not found");
      vix::cli::util::kv(std::cout, "Service status", serviceStatus.value_or("unknown"));
      vix::cli::util::kv(std::cout, "Restart policy", restartPolicy.value_or("unknown"));
      vix::cli::util::kv(std::cout, "Working directory", workingDir.value_or("unknown"));
      vix::cli::util::kv(std::cout, "HTTP port", port.value_or("unknown"));
      vix::cli::util::kv(std::cout, "Proxy", nginxExists ? "nginx" : "not found");
      vix::cli::util::kv(std::cout, "Public URL", domain ? "https://" + *domain : "unknown");
      vix::cli::util::kv(std::cout, "TLS", tls ? "enabled" : "unknown");
      vix::cli::util::kv(std::cout, "Local health", localHealth ? "ok" : "unknown");
      vix::cli::util::kv(std::cout, "Public health", publicHealth ? "ok" : "unknown");
      vix::cli::util::section(std::cout, "Production Readiness");

      vix::cli::util::kv(
          std::cout,
          "Score",
          std::to_string(readinessScore) + "/" + std::to_string(readinessMax));

      for (const auto &item : readiness)
        print_readiness_item(item);

      int rc = 0;

      if (!buildDir)
      {
        rc = 1;
        vix::cli::util::warn_line(std::cerr, "Fix: run `vix build` to create a build directory.");
      }

      if (!binaryExists)
      {
        rc = 1;
        vix::cli::util::warn_line(std::cerr, "Fix: build the project and verify the executable name.");
      }

      if (!running)
      {
        rc = 1;
        vix::cli::util::warn_line(std::cerr, "Fix: start the app or install a systemd service.");
      }

      if (!serviceExists)
      {
        rc = 1;
        vix::cli::util::warn_line(std::cerr, "Fix: create a systemd service for this app.");
      }

      if (!nginxExists)
      {
        rc = 1;
        vix::cli::util::warn_line(std::cerr, "Fix: create an Nginx reverse proxy config.");
      }

      if (domain && !tls)
      {
        rc = 1;
        vix::cli::util::warn_line(std::cerr, "Fix: enable TLS with Let's Encrypt for " + *domain + ".");
      }

      if (jsonOut)
      {
        json out;
        out["app"] = projectName;
        out["running"] = running;
        out["build_dir"] = buildDir ? buildDir->string() : "";
        out["binary"] = binary ? binary->string() : "";
        out["binary_exists"] = binaryExists;
        out["service"] = service ? *service : "";
        out["service_exists"] = serviceExists;
        out["service_status"] = serviceStatus.value_or("");
        out["restart_policy"] = restartPolicy.value_or("");
        out["working_directory"] = workingDir.value_or("");
        out["http_port"] = port.value_or("");
        out["proxy"] = nginxExists ? "nginx" : "";
        out["domain"] = domain.value_or("");
        out["tls"] = tls;
        out["local_health"] = localHealth;
        out["public_health"] = publicHealth;
        out["readiness_score"] = readinessScore;
        out["readiness_max"] = readinessMax;
        out["websocket_health_configured"] = websocketHealthConfigured;
        out["deploy_rollback_configured"] = deployRollbackConfigured;
        out["readiness"] = json::array();

        for (const auto &item : readiness)
        {
          out["readiness"].push_back(
              {
                  {"status", readiness_status_text(item.status)},
                  {"label", item.label},
                  {"points", item.points},
                  {"max_points", item.maxPoints},
                  {"fix", item.fix},
              });
        }

        std::cout << "\n"
                  << out.dump(2) << "\n";
      }

      if (rc == 0)
        vix::cli::util::ok_line(std::cout, "production doctor: ok");
      else
        vix::cli::util::warn_line(std::cerr, "production doctor: issues detected");

      return rc;
    }
#endif

  } // namespace

  int DoctorCommand::run(const std::vector<std::string> &args)
  {
    vix::cli::util::section(std::cout, "Doctor");

    Options opt;
    try
    {
      opt = parse_args(args);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, ex.what());
      vix::cli::util::warn_line(std::cerr, "Tip: vix doctor --help");
      return 1;
    }

    if (opt.toolchain)
      return doctor_toolchain();

#ifndef _WIN32
    if (opt.production)
    {
      return run_production_doctor(opt.jsonOut);
    }
#else
    if (opt.production)
    {
      vix::cli::util::err_line(std::cerr, "vix doctor production is currently supported on Linux only.");
      return 1;
    }
#endif

    const fs::path exe = current_exe_path();
    fs::path realExe = exe;
    if (!fs::exists(realExe))
    {
      if (auto w = which_vix())
        realExe = *w;
    }
    vix::cli::util::kv(std::cout, "exe", realExe.string());

    const std::string os = detect_os();
    const std::string arch = detect_arch();

    vix::cli::util::kv(std::cout, "os", os);
    vix::cli::util::kv(std::cout, "arch", arch);
    vix::cli::util::kv(std::cout, "exe", exe.string());

    const auto localVer = vix_version_from_self();
    vix::cli::util::kv(std::cout, "version", localVer.has_value() ? *localVer : "unknown");

    const fs::path stats = stats_file();
    vix::cli::util::kv(std::cout, "install_stats", stats.string());

    json statsJson;
    bool statsOk = false;
    if (fs::exists(stats))
    {
      try
      {
        statsJson = read_json_or_throw(stats);
        statsOk = true;
        vix::cli::util::ok_line(std::cout, "install.json: found");
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, std::string("install.json: invalid: ") + ex.what());
      }
    }
    else
    {
      vix::cli::util::warn_line(std::cerr, "install.json: not found (run: vix upgrade to generate it)");
    }

    fs::path installDir = realExe.has_parent_path() ? realExe.parent_path() : fs::current_path();
    if (statsOk && statsJson.is_object() && statsJson.contains("install_dir") && statsJson["install_dir"].is_string())
      installDir = fs::path(statsJson["install_dir"].get<std::string>());

    vix::cli::util::kv(std::cout, "install_dir", installDir.string());

    const bool inPath = path_contains_dir(installDir.string());
    if (inPath)
      vix::cli::util::ok_line(std::cout, "PATH: contains install_dir");
    else
    {
      vix::cli::util::warn_line(std::cerr, "PATH: missing install_dir");
#ifdef _WIN32
      vix::cli::util::warn_line(std::cerr, "Tip: add to User PATH: " + installDir.string() + " (restart terminal)");
#else
      vix::cli::util::warn_line(std::cerr, "Tip: add to shell config:");
      vix::cli::util::warn_line(std::cerr, "  export PATH=\"" + installDir.string() + ":$PATH\"");
#endif
    }

    const bool writable = is_writable_dir(installDir);
    if (writable)
      vix::cli::util::ok_line(std::cout, "install_dir: writable");
    else
      vix::cli::util::warn_line(std::cerr, "install_dir: not writable (upgrade may fail)");

#ifdef _WIN32
    print_dep_status("powershell", true, "");
    print_dep_status("Get-FileHash", true, "");
    print_dep_status("Expand-Archive", true, "");
#else
    print_dep_status("curl or wget", have_cmd("curl") || have_cmd("wget"),
                     "Install curl (recommended) or wget.");
    print_dep_status("tar", have_cmd("tar"), "Install tar (usually preinstalled).");
    print_dep_status("sha256sum or shasum", have_cmd("sha256sum") || have_cmd("shasum"),
                     "Install sha256sum (Linux coreutils) or shasum (macOS).");

    if (have_cmd("minisign"))
      vix::cli::util::ok_line(std::cout, "minisign: present (optional)");
    else
      vix::cli::util::warn_line(std::cerr, "minisign: missing (optional; sha256 still secures upgrades)");
#endif

    if (const char *lvl = vix::utils::vix_getenv("VIX_LOG_LEVEL"))
      vix::cli::util::kv(std::cout, "VIX_LOG_LEVEL", std::string(lvl));

    // Online check
    std::optional<std::string> latestTag;
    bool updateAvailable = false;

    if (opt.online)
    {
      vix::cli::util::section(std::cout, "Online");
      vix::cli::util::kv(std::cout, "repo", opt.repo);

      latestTag = github_latest_tag(opt.repo);
      if (latestTag)
      {
        vix::cli::util::kv(std::cout, "latest", *latestTag);

        if (localVer && *localVer != *latestTag)
        {
          updateAvailable = true;
          vix::cli::util::warn_line(std::cerr, "update available: " + *localVer + " -> " + *latestTag);
          vix::cli::util::warn_line(std::cerr, "Tip: run: vix upgrade");
        }
        else if (localVer && *localVer == *latestTag)
        {
          vix::cli::util::ok_line(std::cout, "up to date");
        }
        else
        {
          vix::cli::util::warn_line(std::cerr, "could not parse local version for comparison");
        }
      }
      else
      {
        vix::cli::util::warn_line(std::cerr, "failed to resolve latest tag (missing curl/wget, no network, or API rate limit)");
      }
    }

    if (opt.jsonOut)
    {
      json out;
      out["os"] = os;
      out["arch"] = arch;
      out["exe"] = exe.string();
      out["version"] = localVer.has_value() ? *localVer : "unknown";
      out["install_stats_path"] = stats.string();
      out["install_stats_ok"] = statsOk;
      out["install_dir"] = installDir.string();
      out["install_dir_writable"] = writable;
      out["path_contains_install_dir"] = inPath;

      out["online_enabled"] = opt.online;
      out["repo"] = opt.repo;
      out["latest"] = latestTag.has_value() ? *latestTag : "";
      out["update_available"] = updateAvailable;

#ifndef _WIN32
      out["have_curl"] = have_cmd("curl");
      out["have_wget"] = have_cmd("wget");
      out["have_tar"] = have_cmd("tar");
      out["have_sha256sum"] = have_cmd("sha256sum");
      out["have_shasum"] = have_cmd("shasum");
      out["have_minisign"] = have_cmd("minisign");
#else
      out["have_powershell"] = true;
#endif

      std::cout << "\n"
                << out.dump(2) << "\n";
    }

    // Exit code policy:
    // 1 if issues detected (DX or upgrade blockers) or update available when --online is used
    int rc = 0;

#ifndef _WIN32
    const bool haveNet = have_cmd("curl") || have_cmd("wget");
    const bool haveTar = have_cmd("tar");
    const bool haveSha = have_cmd("sha256sum") || have_cmd("shasum");
    if (!haveNet || !haveTar || !haveSha)
      rc = 1;
#endif

    if (!writable)
      rc = 1;

    if (!inPath)
      rc = 1;

    if (opt.online && updateAvailable)
      rc = 1;

    if (rc == 0)
      vix::cli::util::ok_line(std::cout, "doctor: ok");
    else
      vix::cli::util::warn_line(std::cerr, "doctor: issues detected");

    return rc;
  }

  int DoctorCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix doctor [options]\n"
        << "  vix doctor production [options]\n\n"
        << "Description:\n"
        << "  Check local environment for running and upgrading Vix.\n"
        << "  Check production state for a deployed Vix app.\n\n"
        << "Options:\n"
        << "  --json                 Print a JSON summary at the end\n"
        << "  --online               Also check latest release on GitHub\n"
        << "  --repo <owner/name>    Repo to check when using --online (default: vixcpp/vix)\n\n"
        << "Examples:\n"
        << "  vix doctor\n"
        << "  vix doctor production\n"
        << "  vix doctor production --json\n"
        << "  vix doctor --online\n"
        << "  vix doctor --online --repo vixcpp/vix\n"
        << "  vix doctor --json --online\n";
    return 0;
  }

} // namespace vix::commands
