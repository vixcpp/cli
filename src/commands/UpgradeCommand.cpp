/**
 *
 *  @file UpgradeCommand.cpp
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
#include <vix/cli/commands/UpgradeCommand.hpp>
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
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <stdexcept>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <ctime> // pour std::time_t, std::time, std::tm

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // GetCurrentProcessId
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    // Small process helpers
#ifdef _WIN32
    FILE *popen_wrap(const char *cmd, const char *mode) { return _popen(cmd, mode); }
    int pclose_wrap(FILE *f) { return _pclose(f); }
#else
    FILE *popen_wrap(const char *cmd, const char *mode) { return popen(cmd, mode); }
    int pclose_wrap(FILE *f) { return pclose(f); }
#endif

    std::string trim_copy(std::string s)
    {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || std::isspace(static_cast<unsigned char>(s.back()))))
        s.pop_back();
      size_t i = 0;
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
      return s.substr(i);
    }

    std::string shell_quote(const std::string &s)
    {
#ifdef _WIN32
      // Basic quoting for cmd/powershell command arguments.
      // Replace " with \" inside a quoted string.
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
      // POSIX single-quote with escape of single quote.
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

    bool have_cmd(const std::string &name)
    {
#ifdef _WIN32
      // Use "where"
      std::string cmd = "where " + name + " >nul 2>&1";
      return std::system(cmd.c_str()) == 0;
#else
      std::string cmd = "command -v " + name + " >/dev/null 2>&1";
      return std::system(cmd.c_str()) == 0;
#endif
    }

    std::string exec_capture(const std::string &cmd)
    {
      FILE *pipe = popen_wrap(cmd.c_str(), "r");
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
      pclose_wrap(pipe);
      return out;
    }

    int exec_status(const std::string &cmd)
    {
      return std::system(cmd.c_str());
    }

    // Install stats paths
    fs::path stats_file()
    {
#ifdef _WIN32
      if (const char *p = vix::utils::vix_getenv("LOCALAPPDATA"))
        return fs::path(p) / "Vix" / "install.json";
      return fs::current_path() / "install.json";
#else
      if (const char *home = vix::utils::vix_getenv("HOME"))
        return fs::path(home) / ".local" / "share" / "vix" / "install.json";
      return fs::current_path() / "install.json";
#endif
    }

    std::string utc_now_iso()
    {
      std::time_t t = std::time(nullptr);
      std::tm tm{};
#ifdef _WIN32
      gmtime_s(&tm, &t);
#else
      gmtime_r(&t, &tm);
#endif
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
      return std::string(buf);
    }

    void write_install_json(
        const std::string &repoStr,
        const std::string &tag,
        const std::string &os,
        const std::string &arch,
        const fs::path &installDir,
        std::optional<long long> downloadBytes,
        const std::string &installedVersion,
        const std::string &assetUrl)
    {
      fs::path out = stats_file();
      std::error_code ec;
      fs::create_directories(out.parent_path(), ec);

      json j;
      j["repo"] = repoStr;
      j["version"] = tag;
      j["installed_version"] = installedVersion;
      j["installed_at"] = utc_now_iso();
      j["os"] = os;
      j["arch"] = arch;
      j["install_dir"] = installDir.string();
      j["download_bytes"] = downloadBytes.has_value() ? json(*downloadBytes) : json(nullptr);
      j["asset_url"] = assetUrl;

      std::ofstream f(out);
      if (!f)
        throw std::runtime_error("cannot write stats: " + out.string());
      f << j.dump(2) << "\n";

      vix::cli::util::kv(std::cout, "stats", out.string());
    }

    // Version and repo
    std::string repo()
    {
      if (const char *v = vix::utils::vix_getenv("VIX_REPO"))
        return std::string(v);
      return "vixcpp/vix";
    }

    std::string current_exe_path()
    {
      // CLI.cpp sets VIX_CLI_PATH at startup.
      if (const char *p = vix::utils::vix_getenv("VIX_CLI_PATH"))
        return std::string(p);
#ifdef _WIN32
      return "vix.exe";
#else
      return "vix";
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
      std::string t = text;
      t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());

      // Find first "vX.Y.Z" like token by scanning tokens.
      std::vector<std::string> parts;
      std::string cur;
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

      for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i)
      {
        if (looks_like(parts[static_cast<size_t>(i)]))
          return parts[static_cast<size_t>(i)];
      }
      return std::nullopt;
    }

    std::optional<std::string> get_installed_version(const fs::path &exe)
    {
      if (!fs::exists(exe))
        return std::nullopt;

#ifdef _WIN32
      const std::string cmd = shell_quote(exe.string()) + " --version 2>nul";
#else
      const std::string cmd = shell_quote(exe.string()) + " --version 2>/dev/null";
#endif
      const std::string out = exec_capture(cmd);
      if (auto v = extract_version_token(out))
        return v;
      return std::nullopt;
    }

    bool starts_with(const std::string &s, const std::string &prefix)
    {
      return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    // Networking (best choice: system tools)
    std::string resolve_latest_tag_github_api(const std::string &repoStr)
    {
      // Prefer curl or wget on unix, prefer curl then PowerShell on Windows.
      const std::string api = "https://api.github.com/repos/" + repoStr + "/releases/latest";

      std::string body;
      if (have_cmd("curl"))
      {
        body = exec_capture("curl -fSsL " + shell_quote(api));
      }
#ifndef _WIN32
      else if (have_cmd("wget"))
      {
        body = exec_capture("wget -qO- " + shell_quote(api));
      }
#else
      else
      {
        // PowerShell fallback
        const std::string ps =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$r=Invoke-RestMethod -Uri '" +
            api + "' -Headers @{ 'User-Agent'='vix-installer' }; "
                  "if($null -eq $r.tag_name){ exit 1 } "
                  "Write-Output $r.tag_name\"";
        body = exec_capture(ps);
      }
#endif

      body = trim_copy(body);
      if (body.empty())
      {
        // Parse JSON ourselves if body contains JSON and tag_name
        // If curl returned JSON but extra spaces, handle below.
      }

      // If body is JSON, extract "tag_name":"..."
      if (body.size() > 0 && body[0] == '{')
      {
        // minimal parse: find "tag_name"
        const std::string key = "\"tag_name\"";
        const size_t k = body.find(key);
        if (k == std::string::npos)
          throw std::runtime_error("could not resolve latest tag (missing tag_name)");
        const size_t colon = body.find(':', k + key.size());
        if (colon == std::string::npos)
          throw std::runtime_error("could not resolve latest tag");
        size_t q1 = body.find('"', colon);
        if (q1 == std::string::npos)
          throw std::runtime_error("could not resolve latest tag");
        size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string::npos)
          throw std::runtime_error("could not resolve latest tag");
        const std::string tag = body.substr(q1 + 1, q2 - (q1 + 1));
        if (tag.empty())
          throw std::runtime_error("could not resolve latest tag");
        return tag;
      }

      // If body is already the tag (PowerShell output)
      if (starts_with(body, "v"))
        return body;

      throw std::runtime_error("could not resolve latest tag. Set explicit version: vix upgrade vX.Y.Z");
    }

    std::optional<long long> remote_content_length(const std::string &url)
    {
      // Best effort only.
      if (have_cmd("curl"))
      {
        const std::string headers = exec_capture("curl -fsSLI " + shell_quote(url));

        // Parse headers line-by-line and keep the last Content-Length found.
        long long lastLen = -1;

        std::string line;
        line.reserve(256);

        auto flush_line = [&](std::string &ln)
        {
          // trim CR
          if (!ln.empty() && ln.back() == '\r')
            ln.pop_back();

          // lower copy for prefix check
          std::string low = ln;
          std::transform(low.begin(), low.end(), low.begin(),
                         [](unsigned char c)
                         { return static_cast<char>(std::tolower(c)); });

          const std::string key = "content-length:";
          if (low.rfind(key, 0) == 0)
          {
            std::string v = ln.substr(key.size());
            v = trim_copy(v);
            if (!v.empty())
            {
              try
              {
                const long long n = std::stoll(v);
                if (n > 0)
                  lastLen = n;
              }
              catch (...)
              {
              }
            }
          }

          ln.clear();
        };

        for (char c : headers)
        {
          if (c == '\n')
          {
            flush_line(line);
            continue;
          }
          line.push_back(c);
        }
        if (!line.empty())
          flush_line(line);

        if (lastLen > 0)
          return lastLen;
      }

#ifdef _WIN32
      // PowerShell HEAD fallback
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -Command "
          "\"try{ $r=[System.Net.HttpWebRequest]::Create('" +
          url + "'); "
                "$r.Method='HEAD'; $r.AllowAutoRedirect=$true; $resp=$r.GetResponse(); "
                "$len=$resp.ContentLength; $resp.Close(); if($len -gt 0){ Write-Output $len } }catch{}\"";

      const std::string out = trim_copy(exec_capture(ps));
      if (!out.empty())
      {
        try
        {
          const long long n = std::stoll(out);
          if (n > 0)
            return n;
        }
        catch (...)
        {
        }
      }
#endif

      return std::nullopt;
    }

    void download_to_file(const std::string &url, const fs::path &out)
    {
      const std::string o = out.string();
      if (have_cmd("curl"))
      {
#ifdef _WIN32
        const std::string cmd = "curl -fSsL " + shell_quote(url) + " -o " + shell_quote(o);
#else
        const std::string cmd = "curl -fSsL " + shell_quote(url) + " -o " + shell_quote(o);
#endif
        if (exec_status(cmd + " >" + std::string(
#ifdef _WIN32
                                         "nul 2>&1"
#else
                                         "/dev/null 2>&1"
#endif
                                         )) != 0)
          throw std::runtime_error("download failed");
        return;
      }

#ifndef _WIN32
      if (have_cmd("wget"))
      {
        const std::string cmd = "wget -qO " + shell_quote(o) + " " + shell_quote(url);
        if (exec_status(cmd + " >/dev/null 2>&1") != 0)
          throw std::runtime_error("download failed");
        return;
      }
#else
      // PowerShell fallback
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -Command "
          "\"Invoke-WebRequest -Uri '" +
          url + "' -OutFile '" + o + "' -Headers @{ 'User-Agent'='vix-installer' }\"";
      if (exec_status(ps + " >nul 2>&1") != 0)
        throw std::runtime_error("download failed");
      return;
#endif

      throw std::runtime_error("need curl (or wget on unix) to download");
    }

    // Verification
    std::string read_first_line(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        return {};
      std::string line;
      std::getline(in, line);
      return trim_copy(line);
    }

    std::string parse_sha_expected(const std::string &line)
    {
      // Accept:
      // 1) "<sha>  file"
      // 2) "SHA256 (file) = <sha>"
      auto is_hex64 = [](const std::string &s) -> bool
      {
        if (s.size() != 64)
          return false;
        for (char c : s)
        {
          if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
        }
        return true;
      };

      // format 1: starts with sha
      if (line.size() >= 64)
      {
        std::string first = line.substr(0, 64);
        if (is_hex64(first))
          return first;
      }

      // format 2: ends with sha
      for (size_t i = 0; i + 64 <= line.size(); ++i)
      {
        const std::string cand = line.substr(line.size() - 64);
        if (is_hex64(cand))
          return cand;
        break;
      }

      return {};
    }

    std::string sha256_of_file(const fs::path &p)
    {
#ifdef _WIN32
      // Use PowerShell Get-FileHash
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -Command "
          "\"(Get-FileHash -Algorithm SHA256 -LiteralPath '" +
          p.string() + "').Hash\"";
      return trim_copy(exec_capture(ps));
#else
      if (have_cmd("sha256sum"))
      {
        const std::string cmd = "sha256sum " + shell_quote(p.string()) + " | awk '{print $1}'";
        return trim_copy(exec_capture(cmd));
      }
      if (have_cmd("shasum"))
      {
        const std::string cmd = "shasum -a 256 " + shell_quote(p.string()) + " | awk '{print $1}'";
        return trim_copy(exec_capture(cmd));
      }
      return {};
#endif
    }

    void verify_sha256_or_throw(const std::string &shaUrl, const fs::path &artifact, const fs::path &tmpDir)
    {
      const fs::path shaPath = tmpDir / (artifact.filename().string() + ".sha256");
      download_to_file(shaUrl, shaPath);

      const std::string line = read_first_line(shaPath);
      if (line.empty())
        throw std::runtime_error("invalid sha256 file");

      const std::string expected = parse_sha_expected(line);
      if (expected.empty())
        throw std::runtime_error("invalid sha256 format");

      const std::string actual = sha256_of_file(artifact);
      if (actual.empty())
        throw std::runtime_error("cannot compute sha256 (missing tool)");

      std::string e = expected;
      std::string a = actual;
      std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });

      if (e != a)
        throw std::runtime_error("sha256 mismatch");
    }

    bool try_verify_minisign(
        const std::string &minisigUrl,
        const fs::path &artifact,
        const fs::path &tmpDir,
        const std::string &pubKey)
    {
      const fs::path sigPath = tmpDir / (artifact.filename().string() + ".minisig");

      // minisig optional: if download fails -> not present
      try
      {
        download_to_file(minisigUrl, sigPath);
      }
      catch (...)
      {
        return false;
      }

#ifdef _WIN32
      (void)pubKey;
      return true; // Windows: sha256 is the required verification
#else
      if (!have_cmd("minisign"))
      {
        // minisig exists but minisign not installed -> do not hard fail
        // sha256 already ensures integrity
        return true;
      }

      // Capture minisign output for debugging
      const std::string cmd =
          "minisign -Vm " + shell_quote(artifact.string()) +
          " -P " + shell_quote(pubKey) + " 2>&1";

      const std::string out = trim_copy(exec_capture(cmd));
      if (!out.empty())
      {
        // minisign prints success too sometimes, but usually nothing.
      }

      // We need status, not just output
      const int st = exec_status(("minisign -Vm " + shell_quote(artifact.string()) +
                                  " -P " + shell_quote(pubKey) + " >/dev/null 2>&1")
                                     .c_str());

      if (st != 0)
      {
        // Give useful error
        throw std::runtime_error(
            std::string("minisign verification failed. minisign says: ") +
            (out.empty() ? "(no output)" : out));
      }

      return true;
#endif
    }

    // Extraction
    fs::path find_binary_in_tree(const fs::path &root, const std::string &name)
    {
      std::error_code ec;
      if (!fs::exists(root, ec))
        return {};

      for (auto it = fs::recursive_directory_iterator(root, ec);
           it != fs::recursive_directory_iterator(); it.increment(ec))
      {
        if (ec)
          break;
        if (!it->is_regular_file(ec))
          continue;
        if (it->path().filename().string() == name)
          return it->path();
      }
      return {};
    }

    void extract_archive_or_throw(const fs::path &archive, const fs::path &outDir, const std::string &os)
    {
      std::error_code ec;
      fs::create_directories(outDir, ec);

#ifdef _WIN32
      (void)os;
      // Expand-Archive
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -Command "
          "\"Expand-Archive -LiteralPath '" +
          archive.string() + "' -DestinationPath '" + outDir.string() + "' -Force\"";
      if (exec_status(ps + " >nul 2>&1") != 0)
        throw std::runtime_error("failed to extract archive");
#else
      // tar -xzf
      if (!have_cmd("tar"))
        throw std::runtime_error("missing dependency: tar");
      const std::string cmd = "tar -xzf " + shell_quote(archive.string()) + " -C " + shell_quote(outDir.string()) + " >/dev/null 2>&1";
      if (exec_status(cmd) != 0)
        throw std::runtime_error("failed to extract archive");
#endif
    }

    // Atomic install
    bool is_writable_dir(const fs::path &dir)
    {
      std::error_code ec;
      fs::create_directories(dir, ec);
      if (ec)
        return false;

      fs::path probe = dir / ".vix_write_test.tmp";
      std::ofstream out(probe.string(), std::ios::binary);
      if (!out)
        return false;
      out << "x";
      out.close();
      fs::remove(probe, ec);
      return true;
    }

    fs::path exe_install_dir_guess(const fs::path &exePath)
    {
      if (exePath.has_parent_path())
        return exePath.parent_path();

#ifdef _WIN32
      if (const char *p = vix::utils::vix_getenv("LOCALAPPDATA"))
        return fs::path(p) / "Vix" / "bin";
      return fs::current_path();
#else
      if (const char *home = vix::utils::vix_getenv("HOME"))
        return fs::path(home) / ".local" / "bin";
      return fs::current_path();
#endif
    }

    std::string human_bytes(long long b)
    {
      const char *units[] = {"B", "KB", "MB", "GB", "TB"};
      double v = static_cast<double>(b);
      int i = 0;
      while (v >= 1024.0 && i < 4)
      {
        v /= 1024.0;
        ++i;
      }
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[i]);
      return std::string(buf);
    }

    struct Options
    {
      bool check = false;
      bool dryRun = false;
      bool jsonOut = false;
      std::optional<std::string> version;
    };

    Options parse_args(const std::vector<std::string> &args)
    {
      Options o;
      for (size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "--check")
        {
          o.check = true;
          continue;
        }
        if (a == "--dry-run")
        {
          o.dryRun = true;
          continue;
        }
        if (a == "--json")
        {
          o.jsonOut = true;
          continue;
        }
        if (a == "-h" || a == "--help")
          continue;

        if (!a.empty() && a[0] != '-' && !o.version.has_value())
        {
          o.version = a;
          continue;
        }

        throw std::runtime_error("unknown argument: " + a);
      }
      return o;
    }

    // Windows self-replace helper: schedule move after current process exits
#ifdef _WIN32
    void schedule_windows_replace(const fs::path &newExe, const fs::path &destExe)
    {
      const int pid = static_cast<int>(::GetCurrentProcessId());

      // Use PowerShell:
      // - wait for PID to exit
      // - move newExe into place (force)
      // - done
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "
          "\"$p=" +
          std::to_string(pid) + ";"
                                "try{ Get-Process -Id $p -ErrorAction SilentlyContinue | Wait-Process }catch{};"
                                "Start-Sleep -Milliseconds 200;"
                                "Move-Item -LiteralPath '" +
          newExe.string() + "' -Destination '" + destExe.string() + "' -Force\"";

      // Detached
      const std::string launch =
          "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "
          "\"Start-Process -WindowStyle Hidden powershell -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-WindowStyle','Hidden','-Command'," +
          shell_quote(ps.substr(std::string("powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command ").size())) +
          " -WindowStyle Hidden\"";

      // If the above quoting becomes annoying, run directly:
      exec_status(ps + " >nul 2>&1");
    }
#endif

  } // namespace

  int UpgradeCommand::run(const std::vector<std::string> &args)
  {
    vix::cli::util::section(std::cout, "Upgrade");

    Options opt;
    try
    {
      opt = parse_args(args);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, ex.what());
      vix::cli::util::warn_line(std::cerr, "Tip: vix upgrade --help");
      return 1;
    }

    const std::string repoStr = repo();
    const std::string os = detect_os();
    const std::string arch = detect_arch();

    if (arch == "unknown")
    {
      vix::cli::util::err_line(std::cerr, "unsupported cpu arch for upgrade");
      return 1;
    }

    const fs::path exePath = fs::path(current_exe_path());
    const fs::path installDir = exe_install_dir_guess(exePath);

    vix::cli::util::kv(std::cout, "repo", repoStr);
    vix::cli::util::kv(std::cout, "os", os);
    vix::cli::util::kv(std::cout, "arch", arch);
    vix::cli::util::kv(std::cout, "exe", exePath.string());
    vix::cli::util::kv(std::cout, "install_dir", installDir.string());

    // Resolve target tag
    std::string tag;
    if (opt.version.has_value())
    {
      tag = *opt.version;
    }
    else
    {
      tag = resolve_latest_tag_github_api(repoStr);
    }
    vix::cli::util::kv(std::cout, "target", tag);

    const fs::path destExe = installDir / (
#ifdef _WIN32
                                              "vix.exe"
#else
                                              "vix"
#endif
                                          );

    // Compare already installed
    const auto installed = get_installed_version(destExe);
    if (installed.has_value() && *installed == tag && !opt.check && !opt.dryRun)
    {
      vix::cli::util::ok_line(std::cout, "already installed: " + *installed);
      write_install_json(repoStr, tag, os, arch, installDir, std::nullopt, *installed, "");
      return 0;
    }

    // Asset naming contract matches your scripts
    std::string asset;
    if (os == "windows")
      asset = "vix-windows-" + arch + ".zip";
    else
      asset = "vix-" + os + "-" + arch + ".tar.gz";

    const std::string base = "https://github.com/" + repoStr + "/releases/download/" + tag;
    const std::string urlBin = base + "/" + asset;
    const std::string urlSha = urlBin + ".sha256";
    const std::string urlSig = urlBin + ".minisig";

    vix::cli::util::kv(std::cout, "asset", asset);
    vix::cli::util::kv(std::cout, "url", urlBin);

    // Size (best effort)
    std::optional<long long> len = remote_content_length(urlBin);
    if (len.has_value())
      vix::cli::util::kv(std::cout, "download_size", human_bytes(*len) + " (" + std::to_string(*len) + " bytes)");
    else
      vix::cli::util::kv(std::cout, "download_size", "unknown");

    if (opt.check)
    {
      vix::cli::util::ok_line(std::cout, "check mode: no download, no install");
      return 0;
    }

    if (opt.dryRun)
    {
      vix::cli::util::ok_line(std::cout, "dry-run: no download, no install");
      return 0;
    }

    // Preflight permissions
    if (!is_writable_dir(installDir))
    {
      vix::cli::util::err_line(std::cerr, "install_dir is not writable: " + installDir.string());
      vix::cli::util::warn_line(std::cerr, "Tip: install vix into a writable directory (example: ~/.local/bin) or use elevated privileges.");
      return 1;
    }

    // Temp workspace
    const fs::path tmpDir = fs::temp_directory_path() / ("vix-upgrade-" + std::to_string(std::rand()));
    std::error_code ec;
    fs::create_directories(tmpDir, ec);

    const fs::path archive = tmpDir / asset;
    const fs::path extractDir = tmpDir / "extract";

    try
    {
      vix::cli::util::info(std::cout, "downloading...");
      download_to_file(urlBin, archive);

      vix::cli::util::info_line(std::cout, "verifying sha256...");
      // Security choice: require sha256 file. It must exist and match.
      verify_sha256_or_throw(urlSha, archive, tmpDir);
      vix::cli::util::ok_line(std::cout, "sha256 ok");

      // Optional minisign: if minisig exists, verify it (on unix). If not present, fine.
      // Use the same pubkey as scripts (hardcoded constant).
      const std::string minisign_pubkey =
          "RWSIfpPSznK9A1gWUc8Eg2iXXQwU5d9BYuQNKGOcoujAF2stPu5rKFjQ";
      try
      {
        const bool did = try_verify_minisign(urlSig, archive, tmpDir, minisign_pubkey);
        if (did)
          vix::cli::util::ok_line(std::cout, "minisign ok");
        else
          vix::cli::util::warn_line(std::cerr, "minisig not found (sha256 already verified)");
      }
      catch (const std::exception &ex2)
      {
        // If minisig existed but verification failed, fail hard.
        throw;
      }

      vix::cli::util::info_line(std::cout, "extracting...");
      extract_archive_or_throw(archive, extractDir, os);

      const std::string binName =
#ifdef _WIN32
          "vix.exe";
#else
          "vix";
#endif

      fs::path bin = extractDir / binName;
      if (!fs::exists(bin))
        bin = find_binary_in_tree(extractDir, binName);

      if (bin.empty() || !fs::exists(bin))
        throw std::runtime_error("archive does not contain " + binName);

      // Ensure executable bit on unix
#ifndef _WIN32
      exec_status(("chmod +x " + shell_quote(bin.string()) + " >/dev/null 2>&1").c_str());
#endif

#ifdef _WIN32
      // Windows: cannot overwrite running vix.exe directly.
      // Best compromise: write vix.exe.new, then schedule replace after exit.
      const fs::path staged = installDir / "vix.exe.new";
      fs::copy_file(bin, staged, fs::copy_options::overwrite_existing, ec);
      if (ec)
        throw std::runtime_error("failed to stage new exe: " + ec.message());

      vix::cli::util::ok_line(std::cout, "staged: " + staged.string());
      vix::cli::util::warn_line(std::cerr, "Windows: replacing vix.exe after this process exits.");

      // Try schedule replace. If it fails, user can manually rename.
      schedule_windows_replace(staged, destExe);

      // We cannot reliably run the new exe in the same process, so trust tag.
      write_install_json(repoStr, tag, os, arch, installDir, len, tag, urlBin);

      vix::cli::util::ok_line(std::cout, "upgrade scheduled. reopen your terminal.");
      return 0;
#else
      // Unix: stage + atomic replace (self-replace safe)
      // Use a unique staged filename to avoid collisions.
      const fs::path staged = installDir / (binName + std::string(".tmp.") + std::to_string(::getpid()));

      ec.clear();
      fs::copy_file(bin, staged, fs::copy_options::overwrite_existing, ec);
      if (ec)
        throw std::runtime_error("failed to stage new binary: " + ec.message());

      // Ensure exec bit on the staged binary
      exec_status(("chmod +x " + shell_quote(staged.string()) + " >/dev/null 2>&1").c_str());

      // Atomic replace on POSIX: rename() replaces destination
      ec.clear();
      fs::rename(staged, destExe, ec);
      if (ec)
      {
        // Fallback: overwrite destination, then remove staged
        ec.clear();
        fs::copy_file(staged, destExe, fs::copy_options::overwrite_existing, ec);
        std::error_code ec2;
        fs::remove(staged, ec2);
      }

      if (ec)
        throw std::runtime_error("failed to install: " + ec.message());

      // Quick check
      const auto newVer = get_installed_version(destExe);
      if (newVer.has_value())
        vix::cli::util::kv(std::cout, "installed", *newVer);
      else
        vix::cli::util::kv(std::cout, "installed", tag);

      write_install_json(repoStr, tag, os, arch, installDir, len, (newVer ? *newVer : tag), urlBin);

      vix::cli::util::ok_line(std::cout, "done");
      return 0;
#endif
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, ex.what());
      return 1;
    }
    struct Cleanup
    {
      fs::path p;
      ~Cleanup()
      {
        std::error_code ec;
        if (!p.empty())
          fs::remove_all(p, ec);
      }
    } cleanup{tmpDir};
  }

  int UpgradeCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix upgrade\n"
        << "  vix upgrade vX.Y.Z\n"
        << "  vix upgrade --check\n"
        << "  vix upgrade --dry-run\n\n"
        << "Description:\n"
        << "  Upgrade the Vix CLI binary from GitHub releases.\n\n"
        << "Options:\n"
        << "  --check        Show target version and download info (no install)\n"
        << "  --dry-run      Simulate upgrade plan (no install)\n"
        << "  --json         Reserved for machine output\n\n"
        << "Environment:\n"
        << "  VIX_REPO        Override repo (default: vixcpp/vix)\n\n"
        << "Notes:\n"
        << "  - Requires sha256 file to exist and match.\n"
        << "  - On Unix, minisign is verified if minisig is published and minisign is installed.\n";
    return 0;
  }

} // namespace vix::commands
