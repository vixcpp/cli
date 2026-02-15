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
      std::string repo = "vixcpp/vix";
    };

    Options parse_args(const std::vector<std::string> &args)
    {
      Options o;

      // repo override: --repo owner/name
      for (size_t i = 0; i < args.size(); ++i)
      {
        const auto &a = args[i];
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
        << "  vix doctor [options]\n\n"
        << "Description:\n"
        << "  Check local environment for running and upgrading Vix.\n\n"
        << "Options:\n"
        << "  --json                 Print a JSON summary at the end\n"
        << "  --online               Also check latest release on GitHub\n"
        << "  --repo <owner/name>    Repo to check when using --online (default: vixcpp/vix)\n\n"
        << "Examples:\n"
        << "  vix doctor\n"
        << "  vix doctor --online\n"
        << "  vix doctor --online --repo vixcpp/vix\n"
        << "  vix doctor --json --online\n";
    return 0;
  }

} // namespace vix::commands
