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
#include <vix/cli/util/Shell.hpp>
#include <vix/cli/util/Hash.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#include <ctime>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
#ifdef _WIN32
    FILE *popen_wrap(const char *cmd, const char *mode)
    {
      return _popen(cmd, mode);
    }

    int pclose_wrap(FILE *f)
    {
      return _pclose(f);
    }
#else
    FILE *popen_wrap(const char *cmd, const char *mode)
    {
      return popen(cmd, mode);
    }

    int pclose_wrap(FILE *f)
    {
      return pclose(f);
    }
#endif

    struct Options
    {
      bool check{false};
      bool dryRun{false};
      bool jsonOut{false};
      bool globalMode{false};

      std::optional<std::string> version;
      std::optional<std::string> globalSpec;
    };

    struct PkgSpec
    {
      std::string ns;
      std::string name;

      std::string requestedVersion;
      std::string resolvedVersion;

      std::string id() const
      {
        return ns + "/" + name;
      }
    };

    struct DepResolved
    {
      std::string id;
      std::string version;
      std::string repo;
      std::string tag;
      std::string commit;
      std::string hash;

      std::string type;
      std::string include{"include"};

      fs::path checkout;
      fs::path linkDir;
    };

    std::string trim_copy(std::string s)
    {
      while (!s.empty() &&
             (s.back() == '\n' || s.back() == '\r' ||
              std::isspace(static_cast<unsigned char>(s.back()))))
      {
        s.pop_back();
      }

      std::size_t i = 0;
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;

      return s.substr(i);
    }

    std::string to_lower(std::string s)
    {
      std::transform(
          s.begin(),
          s.end(),
          s.begin(),
          [](unsigned char c)
          { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    std::string shell_quote(const std::string &s)
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

    bool have_cmd(const std::string &name)
    {
#ifdef _WIN32
      const std::string cmd = "where " + name + " >nul 2>&1";
#else
      const std::string cmd = "command -v " + name + " >/dev/null 2>&1";
#endif
      return std::system(cmd.c_str()) == 0;
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
        const std::size_t n = std::fread(buf, 1, sizeof(buf), pipe);
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

    void print_json(const json &j)
    {
      std::cout << j.dump(2) << "\n";
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
      std::snprintf(
          buf,
          sizeof(buf),
          "%04d-%02d-%02dT%02d:%02d:%02dZ",
          tm.tm_year + 1900,
          tm.tm_mon + 1,
          tm.tm_mday,
          tm.tm_hour,
          tm.tm_min,
          tm.tm_sec);

      return std::string(buf);
    }

    std::string home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    fs::path vix_root()
    {
      const std::string h = home_dir();
      if (h.empty())
        return fs::path(".vix");
      return fs::path(h) / ".vix";
    }

    fs::path registry_dir()
    {
      return vix_root() / "registry" / "index";
    }

    fs::path registry_index_dir()
    {
      return registry_dir() / "index";
    }

    fs::path store_git_dir()
    {
      return vix_root() / "store" / "git";
    }

    fs::path global_root_dir()
    {
      return vix_root() / "global";
    }

    fs::path global_pkgs_dir()
    {
      return global_root_dir() / "packages";
    }

    fs::path global_manifest_path()
    {
      return global_root_dir() / "installed.json";
    }

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

    json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());

      json j;
      in >> j;
      return j;
    }

    void write_json_or_throw(const fs::path &p, const json &j)
    {
      std::error_code ec;
      fs::create_directories(p.parent_path(), ec);

      std::ofstream out(p);
      if (!out)
        throw std::runtime_error("cannot write: " + p.string());

      out << j.dump(2) << "\n";
    }

    void write_install_json(
        const std::string &repoStr,
        const std::string &tag,
        const std::string &os,
        const std::string &arch,
        const fs::path &installDir,
        std::optional<long long> downloadBytes,
        const std::string &installedVersion,
        const std::string &assetUrl,
        bool jsonOut)
    {
      const fs::path out = stats_file();

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

      write_json_or_throw(out, j);

      if (!jsonOut)
        vix::cli::util::kv(std::cout, "stats", out.string());
    }

    std::string sanitize_id_dot(const std::string &id)
    {
      std::string s = id;
      for (char &c : s)
      {
        if (c == '/')
          c = '.';
      }
      return s;
    }

    fs::path store_checkout_path(const std::string &id, const std::string &commit)
    {
      return store_git_dir() / sanitize_id_dot(id) / commit;
    }

    fs::path global_pkg_dir(const std::string &id, const std::string &commit)
    {
      return global_pkgs_dir() / sanitize_id_dot(id) / commit;
    }

    fs::path entry_path(const std::string &ns, const std::string &name)
    {
      return registry_index_dir() / (ns + "." + name + ".json");
    }

    bool parse_pkg_spec(const std::string &rawIn, PkgSpec &out)
    {
      const std::string raw = trim_copy(rawIn);

      const auto slash = raw.find('/');
      if (slash == std::string::npos)
        return false;

      if (!raw.empty() && raw[0] == '@')
      {
        if (slash <= 1)
          return false;
        out.ns = trim_copy(raw.substr(1, slash - 1));
      }
      else
      {
        out.ns = trim_copy(raw.substr(0, slash));
      }

      const auto atVersion = raw.find('@', slash + 1);

      if (atVersion == std::string::npos)
      {
        out.name = trim_copy(raw.substr(slash + 1));
        out.requestedVersion.clear();
      }
      else
      {
        out.name = trim_copy(raw.substr(slash + 1, atVersion - (slash + 1)));
        out.requestedVersion = trim_copy(raw.substr(atVersion + 1));
      }

      out.resolvedVersion.clear();

      if (out.ns.empty() || out.name.empty())
        return false;

      if (atVersion != std::string::npos && out.requestedVersion.empty())
        return false;

      return true;
    }

    std::string find_latest_version(const json &entry)
    {
      if (entry.contains("latest") && entry["latest"].is_string())
        return entry["latest"].get<std::string>();

      if (entry.contains("versions") && entry["versions"].is_object())
      {
        std::string best;
        for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
        {
          const std::string v = it.key();
          if (best.empty() || v > best)
            best = v;
        }
        return best;
      }

      return {};
    }

    int resolve_version_v1(const json &entry, PkgSpec &spec)
    {
      if (!spec.requestedVersion.empty())
      {
        spec.resolvedVersion = spec.requestedVersion;
        return 0;
      }

      const std::string latest = find_latest_version(entry);
      if (latest.empty())
      {
        vix::cli::util::err_line(
            std::cerr,
            "no versions available for: " + spec.ns + "/" + spec.name);
        return 1;
      }

      spec.resolvedVersion = latest;
      return 0;
    }

    int ensure_registry_present()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
        return 0;

      vix::cli::util::err_line(std::cerr, "registry not synced");
      vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
      return 1;
    }

    int clone_checkout(
        const std::string &repoUrl,
        const std::string &idDot,
        const std::string &commit,
        std::string &outDir)
    {
      fs::create_directories(store_git_dir());

      const fs::path dst = store_git_dir() / idDot / commit;
      outDir = dst.string();

      if (fs::exists(dst))
        return 0;

      fs::create_directories(dst.parent_path());

      {
        const std::string cmd = "git clone -q " + repoUrl + " " + dst.string();
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
          return rc;
      }

      {
        const std::string cmd =
            "git -C " + dst.string() +
            " -c advice.detachedHead=false checkout -q " + commit;
        const int rc = vix::cli::util::run_cmd_retry_debug(cmd);
        if (rc != 0)
          return rc;
      }

      return 0;
    }

    void remove_all_if_exists(const fs::path &p)
    {
      std::error_code ec;
      if (fs::exists(p, ec))
        fs::remove_all(p, ec);
    }

    void ensure_symlink_or_copy_dir(const fs::path &src, const fs::path &dst)
    {
      std::error_code ec;
      remove_all_if_exists(dst);

#ifdef _WIN32
      fs::create_directories(dst, ec);
      fs::copy(
          src,
          dst,
          fs::copy_options::recursive |
              fs::copy_options::copy_symlinks |
              fs::copy_options::overwrite_existing,
          ec);
      if (ec)
        throw std::runtime_error("failed to copy dependency: " + dst.string());
#else
      fs::create_directories(dst.parent_path(), ec);
      fs::create_directory_symlink(src, dst, ec);
      if (ec)
      {
        ec.clear();
        fs::create_directories(dst, ec);
        fs::copy(
            src,
            dst,
            fs::copy_options::recursive |
                fs::copy_options::copy_symlinks |
                fs::copy_options::overwrite_existing,
            ec);
        if (ec)
          throw std::runtime_error("failed to link/copy dependency: " + dst.string());
      }
#endif
    }

    bool verify_dependency_hash(const DepResolved &dep)
    {
      if (dep.hash.empty())
        return true;

      const auto actualHashOpt = vix::cli::util::sha256_directory(dep.checkout);
      if (!actualHashOpt)
      {
        vix::cli::util::warn_line(std::cerr, "could not compute hash for: " + dep.id);
        return true;
      }

      if (*actualHashOpt != dep.hash)
      {
        vix::cli::util::err_line(std::cerr, "integrity check failed: " + dep.id);
        vix::cli::util::err_line(std::cerr, "expected: " + dep.hash);
        vix::cli::util::err_line(std::cerr, "actual:   " + *actualHashOpt);
        return false;
      }

      return true;
    }

    void load_dep_manifest(DepResolved &dep)
    {
      const fs::path manifest = dep.checkout / "vix.json";
      if (!fs::exists(manifest))
      {
        dep.type = "unknown";
        dep.include = "include";
        return;
      }

      const json j = read_json_or_throw(manifest);

      dep.type = j.value("type", "");
      if (dep.type.empty())
        dep.type = "header-only";

      if (j.contains("include") && j["include"].is_string())
        dep.include = j["include"].get<std::string>();
      else
        dep.include = "include";
    }

    std::optional<DepResolved> resolve_package_from_registry(const std::string &rawSpec)
    {
      PkgSpec spec;
      if (!parse_pkg_spec(rawSpec, spec))
        return std::nullopt;

      const fs::path p = entry_path(spec.ns, spec.name);
      if (!fs::exists(p))
        return std::nullopt;

      const json entry = read_json_or_throw(p);

      if (resolve_version_v1(entry, spec) != 0)
        return std::nullopt;

      if (!entry.contains("repo") || !entry["repo"].is_object())
        throw std::runtime_error("invalid registry entry: missing repo object");

      if (!entry["repo"].contains("url") || !entry["repo"]["url"].is_string())
        throw std::runtime_error("invalid registry entry: missing repo.url");

      if (!entry.contains("versions") || !entry["versions"].is_object())
        throw std::runtime_error("invalid registry entry: missing versions object");

      const json versions = entry.at("versions");
      if (!versions.contains(spec.resolvedVersion))
      {
        throw std::runtime_error(
            "version not found: " + spec.id() + "@" + spec.resolvedVersion);
      }

      const json v = versions.at(spec.resolvedVersion);

      DepResolved dep;
      dep.id = spec.id();
      dep.version = spec.resolvedVersion;
      dep.repo = entry.at("repo").at("url").get<std::string>();
      dep.tag = v.at("tag").get<std::string>();
      dep.commit = v.at("commit").get<std::string>();
      dep.checkout = store_checkout_path(dep.id, dep.commit);

      return dep;
    }

    json load_global_manifest()
    {
      if (!fs::exists(global_manifest_path()))
      {
        return json{
            {"packages", json::array()}};
      }

      json root = read_json_or_throw(global_manifest_path());
      if (!root.contains("packages") || !root["packages"].is_array())
        root["packages"] = json::array();

      return root;
    }

    void save_global_manifest(const json &root)
    {
      fs::create_directories(global_root_dir());
      write_json_or_throw(global_manifest_path(), root);
    }

    void save_global_install(const DepResolved &dep, const fs::path &installedPath)
    {
      json root = load_global_manifest();
      auto &arr = root["packages"];

      bool updated = false;

      for (auto &item : arr)
      {
        if (item.value("id", "") == dep.id)
        {
          item["id"] = dep.id;
          item["version"] = dep.version;
          item["repo"] = dep.repo;
          item["tag"] = dep.tag;
          item["commit"] = dep.commit;
          item["hash"] = dep.hash;
          item["type"] = dep.type;
          item["include"] = dep.include;
          item["installed_path"] = installedPath.string();
          updated = true;
          break;
        }
      }

      if (!updated)
      {
        arr.push_back({
            {"id", dep.id},
            {"version", dep.version},
            {"repo", dep.repo},
            {"tag", dep.tag},
            {"commit", dep.commit},
            {"hash", dep.hash},
            {"type", dep.type},
            {"include", dep.include},
            {"installed_path", installedPath.string()},
        });
      }

      save_global_manifest(root);
    }

    std::optional<json *> find_global_manifest_item(json &root, const std::string &id)
    {
      if (!root.contains("packages") || !root["packages"].is_array())
        return std::nullopt;

      for (auto &item : root["packages"])
      {
        if (item.value("id", "") == id)
          return &item;
      }

      return std::nullopt;
    }

    std::string repo()
    {
      if (const char *v = vix::utils::vix_getenv("VIX_REPO"))
        return std::string(v);
      return "vixcpp/vix";
    }

    std::string current_exe_path()
    {
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
        for (std::size_t i = 1; i < x.size(); ++i)
        {
          const char c = x[i];

          if (c == '.')
          {
            ++dots;
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
        if (looks_like(parts[static_cast<std::size_t>(i)]))
          return parts[static_cast<std::size_t>(i)];
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
      return extract_version_token(out);
    }

    bool starts_with(const std::string &s, const std::string &prefix)
    {
      return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    std::string resolve_latest_tag_github_api(const std::string &repoStr)
    {
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

      if (!body.empty() && body.front() == '{')
      {
        const std::string key = "\"tag_name\"";
        const std::size_t k = body.find(key);
        if (k == std::string::npos)
          throw std::runtime_error("could not resolve latest tag (missing tag_name)");

        const std::size_t colon = body.find(':', k + key.size());
        if (colon == std::string::npos)
          throw std::runtime_error("could not resolve latest tag");

        const std::size_t q1 = body.find('"', colon);
        if (q1 == std::string::npos)
          throw std::runtime_error("could not resolve latest tag");

        const std::size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string::npos)
          throw std::runtime_error("could not resolve latest tag");

        const std::string tag = body.substr(q1 + 1, q2 - (q1 + 1));
        if (tag.empty())
          throw std::runtime_error("could not resolve latest tag");

        return tag;
      }

      if (starts_with(body, "v"))
        return body;

      throw std::runtime_error("could not resolve latest tag. Set explicit version: vix upgrade vX.Y.Z");
    }

    std::optional<long long> remote_content_length(const std::string &url)
    {
      if (have_cmd("curl"))
      {
        const std::string headers = exec_capture("curl -fsSLI " + shell_quote(url));
        long long lastLen = -1;

        std::string line;
        line.reserve(256);

        auto flush_line = [&](std::string &ln)
        {
          if (!ln.empty() && ln.back() == '\r')
            ln.pop_back();

          std::string low = ln;
          std::transform(
              low.begin(),
              low.end(),
              low.begin(),
              [](unsigned char c)
              { return static_cast<char>(std::tolower(c)); });

          const std::string key = "content-length:";
          if (low.rfind(key, 0) == 0)
          {
            std::string v = trim_copy(ln.substr(key.size()));
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

    std::string human_bytes(long long bytes)
    {
      static const char *units[] = {"B", "KB", "MB", "GB", "TB"};

      double value = static_cast<double>(bytes);
      std::size_t unit = 0;

      while (value >= 1024.0 && unit + 1 < (sizeof(units) / sizeof(units[0])))
      {
        value /= 1024.0;
        ++unit;
      }

      char buf[64];
      if (unit == 0)
        std::snprintf(buf, sizeof(buf), "%lld %s", bytes, units[unit]);
      else
        std::snprintf(buf, sizeof(buf), "%.2f %s", value, units[unit]);

      return std::string(buf);
    }

    void download_to_file(const std::string &url, const fs::path &out)
    {
      const std::string o = out.string();

      if (have_cmd("curl"))
      {
        const std::string cmd = "curl -fSsL " + shell_quote(url) + " -o " + shell_quote(o);
#ifdef _WIN32
        if (exec_status(cmd + " >nul 2>&1") != 0)
#else
        if (exec_status(cmd + " >/dev/null 2>&1") != 0)
#endif
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

      if (line.size() >= 64)
      {
        const std::string first = line.substr(0, 64);
        if (is_hex64(first))
          return to_lower(first);
      }

      if (line.size() >= 64)
      {
        const std::string last = line.substr(line.size() - 64);
        if (is_hex64(last))
          return to_lower(last);
      }

      return {};
    }

    std::string sha256_of_file(const fs::path &p)
    {
#ifdef _WIN32
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -Command "
          "\"(Get-FileHash -Algorithm SHA256 -LiteralPath '" +
          p.string() + "').Hash\"";

      return to_lower(trim_copy(exec_capture(ps)));
#else
      if (have_cmd("sha256sum"))
      {
        const std::string cmd =
            "sha256sum " + shell_quote(p.string()) + " | awk '{print $1}'";
        return to_lower(trim_copy(exec_capture(cmd)));
      }

      if (have_cmd("shasum"))
      {
        const std::string cmd =
            "shasum -a 256 " + shell_quote(p.string()) + " | awk '{print $1}'";
        return to_lower(trim_copy(exec_capture(cmd)));
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
        throw std::runtime_error("could not compute sha256 locally");

      if (to_lower(actual) != to_lower(expected))
      {
        throw std::runtime_error(
            "sha256 mismatch: expected " + expected + ", got " + actual);
      }
    }

    bool try_verify_minisign(
        const std::string &sigUrl,
        const fs::path &artifact,
        const fs::path &tmpDir,
        const std::string &pubkey)
    {
      if (!have_cmd("minisign"))
        return false;

      const fs::path sigPath = tmpDir / (artifact.filename().string() + ".minisig");

      try
      {
        download_to_file(sigUrl, sigPath);
      }
      catch (...)
      {
        return false;
      }

#ifdef _WIN32
      const std::string cmd =
          "minisign -V -P " + shell_quote(pubkey) +
          " -m " + shell_quote(artifact.string()) +
          " -x " + shell_quote(sigPath.string()) + " >nul 2>&1";
#else
      const std::string cmd =
          "minisign -V -P " + shell_quote(pubkey) +
          " -m " + shell_quote(artifact.string()) +
          " -x " + shell_quote(sigPath.string()) + " >/dev/null 2>&1";
#endif

      return exec_status(cmd) == 0;
    }

    void extract_archive_or_throw(
        const fs::path &archive,
        const fs::path &extractDir)
    {
      std::error_code ec;
      fs::create_directories(extractDir, ec);

#ifdef _WIN32
      if (os == "windows")
      {
        const std::string ps =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"Expand-Archive -LiteralPath '" +
            archive.string() + "' -DestinationPath '" + extractDir.string() + "' -Force\"";

        if (exec_status(ps + " >nul 2>&1") != 0)
          throw std::runtime_error("failed to extract archive");

        return;
      }
#endif

      if (!have_cmd("tar"))
        throw std::runtime_error("tar is required to extract the archive");

      const std::string cmd =
          "tar -xzf " + shell_quote(archive.string()) +
          " -C " + shell_quote(extractDir.string());

#ifdef _WIN32
      if (exec_status(cmd + " >nul 2>&1") != 0)
#else
      if (exec_status(cmd + " >/dev/null 2>&1") != 0)
#endif
        throw std::runtime_error("failed to extract archive");
    }

    fs::path find_binary_in_tree(const fs::path &root, const std::string &binName)
    {
      std::error_code ec;
      if (!fs::exists(root, ec))
        return {};

      for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
      {
        if (ec)
          continue;

        if (!it->is_regular_file())
          continue;

        if (it->path().filename() == binName)
          return it->path();
      }

      return {};
    }

    bool is_writable_dir(const fs::path &dir)
    {
      std::error_code ec;
      fs::create_directories(dir, ec);

      const fs::path probe = dir / ".vix-write-test.tmp";
      {
        std::ofstream out(probe);
        if (!out)
          return false;
      }

      fs::remove(probe, ec);
      return true;
    }

    fs::path exe_install_dir_guess(const fs::path &exePath)
    {
      std::error_code ec;
      if (!exePath.empty())
      {
        const fs::path parent = exePath.parent_path();
        if (!parent.empty() && fs::exists(parent, ec))
          return parent;
      }

#ifdef _WIN32
      if (const char *local = vix::utils::vix_getenv("LOCALAPPDATA"))
        return fs::path(local) / "Vix" / "bin";
      return fs::current_path();
#else
      if (const char *home = vix::utils::vix_getenv("HOME"))
        return fs::path(home) / ".local" / "bin";
      return fs::current_path();
#endif
    }

    Options parse_args(const std::vector<std::string> &args)
    {
      Options o;

      for (std::size_t i = 0; i < args.size(); ++i)
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

        if (a == "-g" || a == "--global")
        {
          o.globalMode = true;

          if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
          {
            o.globalSpec = args[i + 1];
            ++i;
          }

          continue;
        }

        if (a == "-h" || a == "--help")
          continue;

        if (!a.empty() && a[0] != '-')
        {
          if (o.globalMode)
          {
            if (!o.globalSpec.has_value())
            {
              o.globalSpec = a;
              continue;
            }
          }
          else
          {
            if (!o.version.has_value())
            {
              o.version = a;
              continue;
            }
          }
        }

        throw std::runtime_error("unknown argument: " + a);
      }

      return o;
    }

#ifdef _WIN32
    void schedule_windows_replace(const fs::path &newExe, const fs::path &destExe)
    {
      const int pid = static_cast<int>(::GetCurrentProcessId());

      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command "
          "\"$p=" +
          std::to_string(pid) + ";"
                                "try{ Get-Process -Id $p -ErrorAction SilentlyContinue | Wait-Process }catch{};"
                                "Start-Sleep -Milliseconds 200;"
                                "Move-Item -LiteralPath '" +
          newExe.string() + "' -Destination '" + destExe.string() + "' -Force\"";

      exec_status(ps + " >nul 2>&1");
    }
#endif

    int run_global_upgrade(const Options &opt)
    {
      if (!opt.globalSpec.has_value() || opt.globalSpec->empty())
      {
        if (opt.jsonOut)
        {
          print_json({
              {"command", "upgrade"},
              {"mode", "global"},
              {"status", "error"},
              {"message", "missing package after -g"},
          });
        }
        else
        {
          vix::cli::util::err_line(std::cerr, "missing package after -g");
          vix::cli::util::warn_line(std::cerr, "Example: vix upgrade -g @gk/jwt");
        }

        return 1;
      }

      if (ensure_registry_present() != 0)
      {
        if (opt.jsonOut)
        {
          print_json({
              {"command", "upgrade"},
              {"mode", "global"},
              {"status", "error"},
              {"message", "registry not synced"},
          });
        }
        return 1;
      }

      json result;
      result["command"] = "upgrade";
      result["mode"] = "global";
      result["spec"] = *opt.globalSpec;
      result["check"] = opt.check;
      result["dry_run"] = opt.dryRun;

      try
      {
        std::optional<DepResolved> resolvedOpt = resolve_package_from_registry(*opt.globalSpec);
        if (!resolvedOpt)
          throw std::runtime_error("invalid package spec or package not found");

        DepResolved dep = *resolvedOpt;

        result["package"] = dep.id;
        result["target_version"] = dep.version;
        result["repo"] = dep.repo;
        result["commit"] = dep.commit;

        json root = load_global_manifest();
        auto itemOpt = find_global_manifest_item(root, dep.id);

        if (!itemOpt.has_value())
          throw std::runtime_error("global package is not installed: " + dep.id);

        json *current = *itemOpt;

        const std::string currentVersion = (*current).value("version", "");
        const std::string currentCommit = (*current).value("commit", "");
        const std::string currentPath = (*current).value("installed_path", "");

        result["current_version"] = currentVersion;
        result["current_commit"] = currentCommit;
        result["current_path"] = currentPath;

        if (currentCommit == dep.commit && !opt.check && !opt.dryRun)
        {
          result["status"] = "ok";
          result["action"] = "noop";
          result["message"] = "global package already up to date";

          if (opt.jsonOut)
            print_json(result);
          else
            vix::cli::util::ok_line(std::cout, "Global package already up to date");

          return 0;
        }

        if (opt.check)
        {
          result["status"] = "ok";
          result["action"] = "check";
          result["message"] = "check mode: no install";

          if (opt.jsonOut)
          {
            print_json(result);
          }
          else
          {
            vix::cli::util::section(std::cout, "Upgrade global package");
            vix::cli::util::kv(std::cout, "package", dep.id);
            vix::cli::util::kv(std::cout, "current", currentVersion.empty() ? "(unknown)" : currentVersion);
            vix::cli::util::kv(std::cout, "target", dep.version);
            vix::cli::util::ok_line(std::cout, "check mode: no install");
          }

          return 0;
        }

        if (opt.dryRun)
        {
          result["status"] = "ok";
          result["action"] = "dry-run";
          result["message"] = "dry-run: no install";

          if (opt.jsonOut)
          {
            print_json(result);
          }
          else
          {
            vix::cli::util::section(std::cout, "Upgrade global package");
            vix::cli::util::kv(std::cout, "package", dep.id);
            vix::cli::util::kv(std::cout, "current", currentVersion.empty() ? "(unknown)" : currentVersion);
            vix::cli::util::kv(std::cout, "target", dep.version);
            vix::cli::util::ok_line(std::cout, "dry-run: no install");
          }

          return 0;
        }

        fs::create_directories(global_pkgs_dir());

        const bool checkoutExistedBefore = fs::exists(dep.checkout);
        if (!checkoutExistedBefore)
        {
          if (!opt.jsonOut)
          {
            vix::cli::util::section(std::cout, "Upgrade global package");
            vix::cli::util::one_line_spacer(std::cout);
          }

          std::string outDir;
          const int rc = clone_checkout(dep.repo, sanitize_id_dot(dep.id), dep.commit, outDir);
          if (rc != 0)
            throw std::runtime_error("fetch failed: " + dep.id);

          dep.checkout = fs::path(outDir);
        }

        dep.hash = vix::cli::util::sha256_directory(dep.checkout).value_or("");
        if (!verify_dependency_hash(dep))
          throw std::runtime_error("integrity check failed: " + dep.id);

        load_dep_manifest(dep);

        const fs::path dst = global_pkg_dir(dep.id, dep.commit);
        ensure_symlink_or_copy_dir(dep.checkout, dst);

        dep.linkDir = dst;
        save_global_install(dep, dst);

        if (!currentPath.empty())
        {
          std::error_code ec;
          fs::path oldPath = fs::path(currentPath);

          if (oldPath != dst && fs::exists(oldPath, ec))
            fs::remove_all(oldPath, ec);
        }

        result["status"] = "ok";
        result["action"] = "upgrade";
        result["installed_path"] = dst.string();
        result["installed_version"] = dep.version;
        result["message"] = "done";

        if (opt.jsonOut)
        {
          print_json(result);
        }
        else
        {
          vix::cli::util::section(std::cout, "Upgrade global package");
          vix::cli::util::one_line_spacer(std::cout);
          std::cout << "  " << CYAN << "•" << RESET << " "
                    << CYAN << BOLD << dep.id << RESET
                    << GRAY << "@" << RESET
                    << YELLOW << BOLD << dep.version << RESET
                    << "  "
                    << GRAY << "upgraded globally" << RESET
                    << "\n";
          vix::cli::util::one_line_spacer(std::cout);
          vix::cli::util::ok_line(std::cout, "Global package ready");
          vix::cli::util::info(std::cout, "Installed into: " + dst.string());
          vix::cli::util::info(std::cout, "Manifest updated: " + global_manifest_path().string());
        }

        return 0;
      }
      catch (const std::exception &ex)
      {
        if (opt.jsonOut)
        {
          result["status"] = "error";
          result["message"] = ex.what();
          print_json(result);
        }
        else
        {
          vix::cli::util::err_line(std::cerr, ex.what());
        }

        return 1;
      }
    }

  } // namespace

  int UpgradeCommand::run(const std::vector<std::string> &args)
  {
    for (const auto &a : args)
    {
      if (a == "-h" || a == "--help")
        return help();
    }

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

    if (opt.globalMode)
      return run_global_upgrade(opt);

    json result;
    result["command"] = "upgrade";
    result["mode"] = "cli";
    result["check"] = opt.check;
    result["dry_run"] = opt.dryRun;

    try
    {
      if (!opt.jsonOut)
        vix::cli::util::section(std::cout, "Upgrade");

      const std::string repoStr = repo();
      const std::string os = detect_os();
      const std::string arch = detect_arch();

      if (arch == "unknown")
        throw std::runtime_error("unsupported cpu arch for upgrade");

      const fs::path exePath = fs::path(current_exe_path());
      const fs::path installDir = exe_install_dir_guess(exePath);

      result["repo"] = repoStr;
      result["os"] = os;
      result["arch"] = arch;
      result["exe"] = exePath.string();
      result["install_dir"] = installDir.string();

      if (!opt.jsonOut)
      {
        vix::cli::util::kv(std::cout, "repo", repoStr);
        vix::cli::util::kv(std::cout, "os", os);
        vix::cli::util::kv(std::cout, "arch", arch);
        vix::cli::util::kv(std::cout, "exe", exePath.string());
        vix::cli::util::kv(std::cout, "install_dir", installDir.string());
      }

      std::string tag;
      if (opt.version.has_value())
        tag = *opt.version;
      else
        tag = resolve_latest_tag_github_api(repoStr);

      result["target"] = tag;

      if (!opt.jsonOut)
        vix::cli::util::kv(std::cout, "target", tag);

      const fs::path destExe = installDir / (
#ifdef _WIN32
                                                "vix.exe"
#else
                                                "vix"
#endif
                                            );

      const auto installed = get_installed_version(destExe);
      if (installed.has_value())
        result["current_version"] = *installed;
      else
        result["current_version"] = nullptr;

      if (installed.has_value() && *installed == tag && !opt.check && !opt.dryRun)
      {
        write_install_json(repoStr, tag, os, arch, installDir, std::nullopt, *installed, "", opt.jsonOut);

        result["status"] = "ok";
        result["action"] = "noop";
        result["installed"] = *installed;
        result["message"] = "already installed";

        if (opt.jsonOut)
          print_json(result);
        else
          vix::cli::util::ok_line(std::cout, "already installed: " + *installed);

        return 0;
      }

      std::string asset;
      if (os == "windows")
        asset = "vix-windows-" + arch + ".zip";
      else
        asset = "vix-" + os + "-" + arch + ".tar.gz";

      const std::string base = "https://github.com/" + repoStr + "/releases/download/" + tag;
      const std::string urlBin = base + "/" + asset;
      const std::string urlSha = urlBin + ".sha256";
      const std::string urlSig = urlBin + ".minisig";

      result["asset"] = asset;
      result["url"] = urlBin;

      if (!opt.jsonOut)
      {
        vix::cli::util::kv(std::cout, "asset", asset);
        vix::cli::util::kv(std::cout, "url", urlBin);
      }

      std::optional<long long> len = remote_content_length(urlBin);
      result["download_size_bytes"] = len.has_value() ? json(*len) : json(nullptr);

      if (!opt.jsonOut)
      {
        if (len.has_value())
          vix::cli::util::kv(std::cout, "download_size", human_bytes(*len) + " (" + std::to_string(*len) + " bytes)");
        else
          vix::cli::util::kv(std::cout, "download_size", "unknown");
      }

      if (opt.check)
      {
        result["status"] = "ok";
        result["action"] = "check";
        result["message"] = "check mode: no download, no install";

        if (opt.jsonOut)
          print_json(result);
        else
          vix::cli::util::ok_line(std::cout, "check mode: no download, no install");

        return 0;
      }

      if (opt.dryRun)
      {
        result["status"] = "ok";
        result["action"] = "dry-run";
        result["message"] = "dry-run: no download, no install";

        if (opt.jsonOut)
          print_json(result);
        else
          vix::cli::util::ok_line(std::cout, "dry-run: no download, no install");

        return 0;
      }

      if (!is_writable_dir(installDir))
        throw std::runtime_error("install_dir is not writable: " + installDir.string());

      const fs::path tmpDir =
          fs::temp_directory_path() /
          ("vix-upgrade-" + std::to_string(static_cast<long long>(std::time(nullptr))));

      std::error_code ec;
      fs::create_directories(tmpDir, ec);

      struct Cleanup
      {
        fs::path p;

        ~Cleanup()
        {
          std::error_code cleanupEc;
          if (!p.empty())
            fs::remove_all(p, cleanupEc);
        }
      } cleanup{tmpDir};

      const fs::path archive = tmpDir / asset;
      const fs::path extractDir = tmpDir / "extract";

      if (!opt.jsonOut)
        vix::cli::util::info(std::cout, "downloading...");

      download_to_file(urlBin, archive);

      if (!opt.jsonOut)
        vix::cli::util::info_line(std::cout, "verifying sha256...");

      verify_sha256_or_throw(urlSha, archive, tmpDir);

      if (!opt.jsonOut)
        vix::cli::util::ok_line(std::cout, "sha256 ok");

      const std::string minisign_pubkey =
          "RWSIfpPSznK9A1gWUc8Eg2iXXQwU5d9BYuQNKGOcoujAF2stPu5rKFjQ";

      const bool minisignOk = try_verify_minisign(urlSig, archive, tmpDir, minisign_pubkey);
      result["minisign_verified"] = minisignOk;

      if (!opt.jsonOut)
      {
        if (minisignOk)
          vix::cli::util::ok_line(std::cout, "minisign ok");
        else
          vix::cli::util::warn_line(std::cerr, "minisig not found (sha256 already verified)");
      }

      if (!opt.jsonOut)
        vix::cli::util::info_line(std::cout, "extracting...");

      extract_archive_or_throw(archive, extractDir);

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

#ifndef _WIN32
      exec_status("chmod +x " + shell_quote(bin.string()) + " >/dev/null 2>&1");
#endif

#ifdef _WIN32
      const fs::path staged = installDir / "vix.exe.new";
      fs::copy_file(bin, staged, fs::copy_options::overwrite_existing, ec);
      if (ec)
        throw std::runtime_error("failed to stage new exe: " + ec.message());

      schedule_windows_replace(staged, destExe);
      write_install_json(repoStr, tag, os, arch, installDir, len, tag, urlBin, opt.jsonOut);

      result["status"] = "ok";
      result["action"] = "upgrade";
      result["installed"] = tag;
      result["message"] = "upgrade scheduled";
      result["staged"] = staged.string();

      if (opt.jsonOut)
      {
        print_json(result);
      }
      else
      {
        vix::cli::util::ok_line(std::cout, "staged: " + staged.string());
        vix::cli::util::warn_line(std::cerr, "Windows: replacing vix.exe after this process exits.");
        vix::cli::util::ok_line(std::cout, "upgrade scheduled. reopen your terminal.");
      }

      return 0;
#else
      const fs::path staged =
          installDir / (binName + std::string(".tmp.") + std::to_string(::getpid()));

      ec.clear();
      fs::copy_file(bin, staged, fs::copy_options::overwrite_existing, ec);
      if (ec)
        throw std::runtime_error("failed to stage new binary: " + ec.message());

      exec_status("chmod +x " + shell_quote(staged.string()) + " >/dev/null 2>&1");

      ec.clear();
      fs::rename(staged, destExe, ec);
      if (ec)
      {
        ec.clear();
        fs::copy_file(staged, destExe, fs::copy_options::overwrite_existing, ec);

        std::error_code ec2;
        fs::remove(staged, ec2);
      }

      if (ec)
        throw std::runtime_error("failed to install: " + ec.message());

      const auto newVer = get_installed_version(destExe);
      const std::string finalVersion = newVer.has_value() ? *newVer : tag;

      write_install_json(repoStr, tag, os, arch, installDir, len, finalVersion, urlBin, opt.jsonOut);

      result["status"] = "ok";
      result["action"] = "upgrade";
      result["installed"] = finalVersion;
      result["message"] = "done";

      if (opt.jsonOut)
      {
        print_json(result);
      }
      else
      {
        vix::cli::util::kv(std::cout, "installed", finalVersion);
        vix::cli::util::ok_line(std::cout, "done");
      }

      return 0;
#endif
    }
    catch (const std::exception &ex)
    {
      result["status"] = "error";
      result["message"] = ex.what();

      if (opt.jsonOut)
        print_json(result);
      else
        vix::cli::util::err_line(std::cerr, ex.what());

      return 1;
    }
  }

  int UpgradeCommand::help()
  {
    std::cout
        << "vix upgrade\n"
        << "Upgrade the Vix CLI or a globally installed package.\n\n"

        << "Usage\n"
        << "  vix upgrade\n"
        << "  vix upgrade vX.Y.Z\n"
        << "  vix upgrade --check\n"
        << "  vix upgrade --dry-run\n"
        << "  vix upgrade --json\n"
        << "  vix upgrade -g [@]namespace/name[@version]\n\n"

        << "Examples\n"
        << "  vix upgrade\n"
        << "  vix upgrade v2.0.1\n"
        << "  vix upgrade --check\n"
        << "  vix upgrade --dry-run\n"
        << "  vix upgrade --json\n"
        << "  vix upgrade -g gk/jwt\n"
        << "  vix upgrade -g gk/jwt@1.0.0\n"
        << "  vix upgrade -g @gk/jwt\n\n"

        << "Options\n"
        << "  -g, --global    Upgrade a globally installed package\n"
        << "  --check         Show target version and download info without installing\n"
        << "  --dry-run       Simulate the upgrade without installing\n"
        << "  --json          Print machine-readable JSON output\n\n"

        << "Environment\n"
        << "  VIX_REPO        Override repo for CLI upgrades (default: vixcpp/vix)\n\n"

        << "Notes\n"
        << "  • CLI upgrades use GitHub releases\n"
        << "  • Global package upgrades use the registry + ~/.vix/global/installed.json\n"
        << "  • On Unix, minisign is verified if available\n";

    return 0;
  }

} // namespace vix::commands
