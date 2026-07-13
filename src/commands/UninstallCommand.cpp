/**
 *
 *  @file UninstallCommand.cpp
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
#include <vix/cli/commands/UninstallCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

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

    enum class UninstallKind
    {
      Cli,
      Sdk,
      GlobalPackage,
      LocalDependency,
    };

    struct Options
    {
      bool purge{false};
      bool all{false};
      bool system{false};
      bool dryRun{false};
      bool jsonOut{false};
      bool verbose{false};

      bool sdkMode{false};
      bool sdkList{false};
      bool sdkAll{false};

      bool globalMode{false};

      std::optional<fs::path> prefix;
      std::optional<fs::path> path;
      std::optional<std::string> version;

      std::vector<std::string> sdkProfiles;
      std::optional<std::string> globalSpec;
      std::optional<std::string> localSpec;
    };

    struct RemoveResult
    {
      fs::path path;
      std::string kind;
      bool removed{false};
      bool existed{false};
      bool dryRun{false};
      std::string error;
    };

    constexpr std::array<const char *, 8> kSdkProfiles{
        "default",
        "web",
        "data",
        "desktop",
        "p2p",
        "game",
        "agent",
        "all",
    };

    namespace ansi
    {
      constexpr const char *reset = "\033[0m";
      constexpr const char *bold = "\033[1m";
      constexpr const char *dim = "\033[2m";
      constexpr const char *green = "\033[38;5;35m";
      constexpr const char *blue = "\033[38;5;32m";
      constexpr const char *amber = "\033[38;5;136m";
      constexpr const char *red = "\033[38;5;124m";
      constexpr const char *muted = "\033[38;5;242m";
    }

    bool is_tty(std::ostream &os)
    {
#ifdef _WIN32
      return false;
#else
      if (&os == &std::cout)
        return isatty(STDOUT_FILENO) != 0;
      if (&os == &std::cerr)
        return isatty(STDERR_FILENO) != 0;
      return false;
#endif
    }

    struct Fmt
    {
      bool color;

      explicit Fmt(std::ostream &os)
          : color(is_tty(os))
      {
      }

      std::string ok() const
      {
        return color ? std::string(ansi::green) + "✓" + ansi::reset : "ok";
      }

      std::string arrow() const
      {
        return color ? std::string(ansi::muted) + "→" + ansi::reset : "→";
      }

      std::string err() const
      {
        return color ? std::string(ansi::red) + "✗" + ansi::reset : "error";
      }

      std::string dot() const
      {
        return color ? std::string(ansi::muted) + "·" + ansi::reset : " ";
      }

      std::string hdr() const
      {
        return color ? std::string(ansi::muted) + "▲" + ansi::reset : "»";
      }

      std::string warn() const
      {
        return color ? std::string(ansi::amber) + "!" + ansi::reset : "!";
      }

      std::string grn(const std::string &s) const
      {
        return color ? std::string(ansi::green) + s + ansi::reset : s;
      }

      std::string blu(const std::string &s) const
      {
        return color ? std::string(ansi::blue) + s + ansi::reset : s;
      }

      std::string red_(const std::string &s) const
      {
        return color ? std::string(ansi::red) + s + ansi::reset : s;
      }

      std::string dim_(const std::string &s) const
      {
        return color ? std::string(ansi::dim) + s + ansi::reset : s;
      }

      std::string bold_(const std::string &s) const
      {
        return color ? std::string(ansi::bold) + s + ansi::reset : s;
      }

      std::string amb(const std::string &s) const
      {
        return color ? std::string(ansi::amber) + s + ansi::reset : s;
      }
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

    bool starts_with(const std::string &s, const std::string &prefix)
    {
      return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    std::string format_elapsed(std::chrono::steady_clock::duration d)
    {
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
      if (ms < 1000)
        return std::to_string(ms) + "ms";

      if (ms < 10000)
      {
        const auto seconds = ms / 1000;
        const auto tenths = (ms % 1000) / 100;
        return std::to_string(seconds) + "." + std::to_string(tenths) + "s";
      }

      return std::to_string((ms + 500) / 1000) + "s";
    }

    std::string normalize_global_package_spec(std::string spec)
    {
      spec = trim_copy(std::move(spec));
      if (!spec.empty() && spec.front() == '@')
        spec.erase(spec.begin());

      const auto slash = spec.find('/');
      if (slash != std::string::npos)
      {
        const auto versionAt = spec.find('@', slash + 1);
        if (versionAt != std::string::npos)
          spec = spec.substr(0, versionAt);
      }

      return spec;
    }

    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

#ifdef _WIN32
    std::string stderr_null_suffix()
    {
      return " 2>nul";
    }

#endif
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

    void print_json(const json &j)
    {
      std::cout << j.dump(2) << "\n";
    }

    void sym_line(std::ostream &os, const std::string &sym, const std::string &msg)
    {
      os << "  " << sym << "  " << msg << "\n";
    }

    void hint_line(std::ostream &os, const Fmt &f, const std::string &msg)
    {
      os << "  " << f.dot() << "  " << f.dim_(msg) << "\n";
    }

    constexpr int kKeyWidth = 10;

    void print_kv(
        std::ostream &os,
        const Fmt &f,
        const std::string &key,
        const std::string &value)
    {
      const std::size_t padSize =
          key.size() < static_cast<std::size_t>(kKeyWidth)
              ? static_cast<std::size_t>(kKeyWidth) - key.size()
              : 1;

      os << "  " << f.dim_(key) << std::string(padSize, ' ') << value << "\n";
    }

    void print_header_box(
        std::ostream &os,
        const Fmt &f,
        const std::string &cmdLabel,
        const std::vector<std::pair<std::string, std::string>> &fields)
    {
      os << "  " << f.hdr() << "  " << f.grn(f.bold_("vix")) << "  "
         << f.dim_(cmdLabel) << "\n";
      os << "  " << f.dim_(std::string(36, '-')) << "\n";

      for (const auto &[key, value] : fields)
        print_kv(os, f, key, value);

      os << "\n";
    }

    void styled_step(std::ostream &os, const Fmt &f, const std::string &msg)
    {
      sym_line(os, f.arrow(), msg);
    }

    void styled_ok(std::ostream &os, const Fmt &f, const std::string &msg)
    {
      sym_line(os, f.ok(), msg);
    }

    void styled_warn(std::ostream &os, const Fmt &f, const std::string &msg)
    {
      sym_line(os, f.warn(), f.amb(msg));
    }

    void styled_done(std::ostream &os, const Fmt &f, const std::string &detail)
    {
      sym_line(os, f.ok(), f.bold_(f.grn("Done")) + f.dim_(" — " + detail));
    }

    void styled_error(
        std::ostream &os,
        const Fmt &f,
        const std::string &msg,
        const std::string &hint = "")
    {
      sym_line(os, f.err(), f.red_(msg));
      if (!hint.empty())
        hint_line(os, f, hint);
    }

    void verbose_line(const Options &opt, const std::string &line)
    {
      if (opt.verbose && !opt.jsonOut)
        std::cerr << "debug: " << line << "\n";
    }

    std::string bin_name()
    {
#ifdef _WIN32
      return "vix.exe";
#else
      return "vix";
#endif
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

    fs::path default_local_bin_path()
    {
#ifdef _WIN32
      if (const char *local = vix::utils::vix_getenv("LOCALAPPDATA"))
        return fs::path(local) / "Vix" / "bin" / bin_name();
      return fs::current_path() / bin_name();
#else
      if (const char *home = vix::utils::vix_getenv("HOME"))
        return fs::path(home) / ".local" / "bin" / bin_name();
      return fs::current_path() / bin_name();
#endif
    }

    fs::path global_root_dir()
    {
      if (const char *p = vix::utils::vix_getenv("VIX_GLOBAL_PREFIX"); p && *p)
        return fs::path(p);

      return vix_root() / "global";
    }

    fs::path global_manifest_path()
    {
      return global_root_dir() / "installed.json";
    }

    fs::path sdk_root_dir()
    {
      return vix_root() / "sdk";
    }

    fs::path sdk_profile_root(const std::string &profile)
    {
      return sdk_root_dir() / profile;
    }

    fs::path sdk_current_metadata_path(const std::string &profile)
    {
      return sdk_profile_root(profile) / "current.json";
    }

    fs::path sdk_current_pointer_path(const std::string &profile)
    {
      return sdk_profile_root(profile) / "current";
    }

    fs::path cmake_user_package_registry_dir()
    {
#ifdef _WIN32
      if (const char *appdata = vix::utils::vix_getenv("APPDATA"))
        return fs::path(appdata) / "CMake" / "packages" / "Vix";

      return fs::current_path() / "CMake" / "packages" / "Vix";
#else
      if (const char *home = vix::utils::vix_getenv("HOME"))
        return fs::path(home) / ".cmake" / "packages" / "Vix";

      return fs::current_path() / ".cmake" / "packages" / "Vix";
#endif
    }

    std::string sanitize_cmake_registry_entry_name(std::string s)
    {
      for (char &c : s)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '.' || c == '_' || c == '-'))
          c = '_';
      }

      if (s.empty())
        return "vix-sdk";

      return s;
    }

    fs::path cmake_registry_entry_path(const std::string &profile)
    {
      return cmake_user_package_registry_dir() /
             sanitize_cmake_registry_entry_name("vix-sdk-" + profile);
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

    std::optional<json> read_json_if_exists(const fs::path &p)
    {
      std::error_code ec;
      if (!fs::exists(p, ec))
        return std::nullopt;

      try
      {
        return read_json_or_throw(p);
      }
      catch (...)
      {
        return std::nullopt;
      }
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

    std::optional<fs::path> read_install_dir_from_install_json()
    {
      const auto meta = read_json_if_exists(stats_file());
      if (!meta.has_value())
        return std::nullopt;

      const std::string dir = meta->value("install_dir", "");
      if (dir.empty())
        return std::nullopt;

      return fs::path(dir);
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
      const std::string out = trim_copy(exec_capture("where vix" + stderr_null_suffix()));
      if (out.empty())
        return std::nullopt;

      std::istringstream iss(out);
      std::string first;
      std::getline(iss, first);
      first = trim_copy(first);
      if (first.empty())
        return std::nullopt;

      return fs::path(first);
#else
      const std::string out = trim_copy(exec_capture("command -v vix 2>/dev/null"));
      if (out.empty())
        return std::nullopt;
      return fs::path(out);
#endif
    }

    bool is_supported_sdk_profile(const std::string &profile)
    {
      return std::find(kSdkProfiles.begin(), kSdkProfiles.end(), profile) != kSdkProfiles.end();
    }

    std::vector<std::string> split_profiles(const std::string &raw)
    {
      std::vector<std::string> out;
      std::string cur;

      auto push = [&]()
      {
        std::string v = to_lower(trim_copy(cur));
        cur.clear();

        if (!v.empty())
          out.push_back(v);
      };

      for (char c : raw)
      {
        if (c == ',' || c == ';')
        {
          push();
          continue;
        }

        cur.push_back(c);
      }

      push();
      return out;
    }

    void add_sdk_profile_arg(Options &o, const std::string &raw)
    {
      for (const auto &profile : split_profiles(raw))
      {
        if (!profile.empty())
          o.sdkProfiles.push_back(profile);
      }
    }

    std::vector<std::string> all_sdk_profiles()
    {
      std::vector<std::string> out;
      for (const char *profile : kSdkProfiles)
        out.push_back(profile);
      return out;
    }

    std::string dedup_key(const fs::path &p)
    {
      std::error_code ec;
      fs::path q = fs::weakly_canonical(p, ec);
      if (ec)
        q = fs::absolute(p, ec);
      if (ec)
        q = p;
      return q.string();
    }

    void push_unique_path(std::vector<fs::path> &out, std::set<std::string> &seen, fs::path p)
    {
      if (p.empty())
        return;

      std::error_code ec;
      if (!p.is_absolute())
        p = fs::absolute(p, ec);

      const std::string key = dedup_key(p);
      if (seen.insert(key).second)
        out.push_back(p);
    }

    std::vector<fs::path> build_cli_candidate_paths(const Options &opt)
    {
      std::vector<fs::path> out;
      std::set<std::string> seen;

      if (opt.path.has_value())
        push_unique_path(out, seen, *opt.path);

      if (auto dir = read_install_dir_from_install_json())
        push_unique_path(out, seen, *dir / bin_name());

      if (auto p = resolve_path_from_env())
        push_unique_path(out, seen, *p);

      if (auto p = resolve_path_from_shell())
        push_unique_path(out, seen, *p);

      if (opt.prefix.has_value())
        push_unique_path(out, seen, *opt.prefix / "bin" / bin_name());

      push_unique_path(out, seen, default_local_bin_path());

#ifndef _WIN32
      if (opt.system || opt.all)
      {
        push_unique_path(out, seen, fs::path("/usr/local/bin") / bin_name());
        push_unique_path(out, seen, fs::path("/usr/bin") / bin_name());
      }
#endif

      return out;
    }

    bool path_exists_or_symlink(const fs::path &p)
    {
      std::error_code ec;
      return fs::exists(p, ec) || fs::is_symlink(p, ec);
    }

    RemoveResult remove_path_best_effort(
        const fs::path &p,
        const std::string &kind,
        bool recursive,
        bool allowDirectory,
        bool dryRun)
    {
      RemoveResult r;
      r.path = p;
      r.kind = kind;
      r.dryRun = dryRun;
      r.existed = path_exists_or_symlink(p);

      if (p.empty())
      {
        r.error = "empty path";
        return r;
      }

      if (!r.existed)
        return r;

      if (dryRun)
      {
        r.removed = true;
        return r;
      }

      std::error_code ec;
      const bool isDir = fs::is_directory(p, ec) && !fs::is_symlink(p, ec);

      if (isDir && !allowDirectory)
      {
        r.error = "refusing to remove directory";
        return r;
      }

      if (isDir && recursive)
        fs::remove_all(p, ec);
      else
        fs::remove(p, ec);

      if (ec)
      {
        r.error = ec.message();
        return r;
      }

      r.removed = true;
      return r;
    }

#ifndef _WIN32
    bool is_system_path(const fs::path &p)
    {
      const std::string s = p.string();
      return starts_with(s, "/usr/") ||
             starts_with(s, "/opt/") ||
             starts_with(s, "/bin/") ||
             starts_with(s, "/sbin/");
    }
#endif

    json remove_result_json(const RemoveResult &r)
    {
      return json{
          {"path", r.path.string()},
          {"kind", r.kind},
          {"removed", r.removed},
          {"existed", r.existed},
          {"dry_run", r.dryRun},
          {"error", r.error.empty() ? json(nullptr) : json(r.error)},
      };
    }

    void print_remove_result(const Options &opt, const Fmt &f, const RemoveResult &r)
    {
      if (opt.jsonOut)
        return;

      if (r.removed)
      {
        if (opt.dryRun)
          styled_ok(std::cout, f, "Would remove " + r.kind + ": " + r.path.string());
        else
          styled_ok(std::cout, f, "Removed " + r.kind + ": " + r.path.string());
        return;
      }

      if (!r.existed)
      {
        verbose_line(opt, "not found: " + r.path.string());
        return;
      }

      if (!r.error.empty())
      {
        styled_warn(std::cerr, f, "Could not remove " + r.kind + ": " + r.path.string());
        hint_line(std::cerr, f, r.error);
#ifndef _WIN32
        if (is_system_path(r.path))
          hint_line(std::cerr, f, "run: sudo rm -rf " + r.path.string());
#endif
      }
    }

    json load_global_manifest()
    {
      if (!fs::exists(global_manifest_path()))
        return json{{"packages", json::array()}};

      json root = read_json_or_throw(global_manifest_path());
      if (!root.contains("packages") || !root["packages"].is_array())
        root["packages"] = json::array();

      return root;
    }

    void save_global_manifest(const json &root)
    {
      write_json_or_throw(global_manifest_path(), root);
    }

    std::string owner_of_global_file(const json &root, const std::string &rel)
    {
      if (!root.contains("packages") || !root["packages"].is_array())
        return {};

      for (const auto &pkg : root["packages"])
      {
        if (!pkg.contains("files") || !pkg["files"].is_array())
          continue;
        for (const auto &file : pkg["files"])
        {
          if (file.is_string() && file.get<std::string>() == rel)
            return pkg.value("id", "");
        }
      }

      return {};
    }

    bool safe_relative_manifest_path(const fs::path &rel)
    {
      const std::string s = rel.generic_string();
      return !rel.empty() && !rel.is_absolute() && s.find("..") == std::string::npos;
    }

    void remove_empty_parents_under_global(fs::path dir)
    {
      std::error_code ec;
      const fs::path root = fs::absolute(global_root_dir(), ec).lexically_normal();
      while (!dir.empty())
      {
        ec.clear();
        const fs::path abs = fs::absolute(dir, ec).lexically_normal();
        if (ec || abs == root || abs.string().find(root.string()) != 0)
          break;
        if (!fs::is_directory(abs, ec) || ec)
          break;
        if (!fs::is_empty(abs, ec) || ec)
          break;
        fs::remove(abs, ec);
        if (ec)
          break;
        dir = abs.parent_path();
      }
    }

    std::optional<std::string> read_sdk_current_version(const std::string &profile)
    {
      const auto meta = read_json_if_exists(sdk_current_metadata_path(profile));
      if (!meta.has_value())
        return std::nullopt;

      const std::string version = meta->value("installed_version", meta->value("version", ""));
      if (version.empty())
        return std::nullopt;

      return version;
    }

    std::vector<std::pair<std::string, fs::path>> installed_sdk_profiles()
    {
      std::vector<std::pair<std::string, fs::path>> out;
      std::error_code ec;

      if (!fs::exists(sdk_root_dir(), ec))
        return out;

      for (fs::directory_iterator it(sdk_root_dir(), ec), end; it != end; it.increment(ec))
      {
        if (ec)
          continue;

        const fs::path p = it->path();
        if (!it->is_directory(ec))
          continue;

        const std::string profile = p.filename().string();
        if (!profile.empty())
          out.push_back({profile, p});
      }

      std::sort(out.begin(), out.end(), [](const auto &a, const auto &b)
                { return a.first < b.first; });

      return out;
    }

    Options parse_args(const std::vector<std::string> &args)
    {
      Options o;

      for (std::size_t i = 0; i < args.size(); ++i)
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

        if (a == "--verbose")
        {
          o.verbose = true;
          continue;
        }

        if (a == "--prefix")
        {
          if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-')
            throw std::runtime_error("missing value after --prefix");

          o.prefix = fs::path(args[++i]);
          continue;
        }

        if (starts_with(a, "--prefix="))
        {
          const std::string value = trim_copy(a.substr(std::string("--prefix=").size()));
          if (value.empty())
            throw std::runtime_error("missing value after --prefix=");

          o.prefix = fs::path(value);
          continue;
        }

        if (a == "--path")
        {
          if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-')
            throw std::runtime_error("missing value after --path");

          o.path = fs::path(args[++i]);
          continue;
        }

        if (starts_with(a, "--path="))
        {
          const std::string value = trim_copy(a.substr(std::string("--path=").size()));
          if (value.empty())
            throw std::runtime_error("missing value after --path=");

          o.path = fs::path(value);
          continue;
        }

        if (a == "--version")
        {
          if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-')
            throw std::runtime_error("missing value after --version");

          o.version = trim_copy(args[++i]);
          continue;
        }

        if (starts_with(a, "--version="))
        {
          const std::string value = trim_copy(a.substr(std::string("--version=").size()));
          if (value.empty())
            throw std::runtime_error("missing value after --version=");

          o.version = value;
          continue;
        }

        if (a == "--sdk")
        {
          o.sdkMode = true;

          if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
          {
            const std::string value = to_lower(trim_copy(args[++i]));
            if (value == "list" || value == "ls" || value == "profiles")
              o.sdkList = true;
            else
              add_sdk_profile_arg(o, value);
          }

          continue;
        }

        if (starts_with(a, "--sdk="))
        {
          o.sdkMode = true;

          const std::string value = to_lower(trim_copy(a.substr(std::string("--sdk=").size())));
          if (value.empty())
            throw std::runtime_error("missing value after --sdk=");

          if (value == "list" || value == "ls" || value == "profiles")
            o.sdkList = true;
          else
            add_sdk_profile_arg(o, value);

          continue;
        }

        if (a == "--sdk-list")
        {
          o.sdkMode = true;
          o.sdkList = true;
          continue;
        }

        if (a == "--sdk-all")
        {
          o.sdkMode = true;
          o.sdkAll = true;
          continue;
        }

        if (a == "-g" || a == "--global")
        {
          o.globalMode = true;

          if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
            o.globalSpec = trim_copy(args[++i]);

          continue;
        }

        if (starts_with(a, "--global="))
        {
          o.globalMode = true;
          const std::string value = trim_copy(a.substr(std::string("--global=").size()));
          if (value.empty())
            throw std::runtime_error("missing value after --global=");

          o.globalSpec = value;
          continue;
        }

        if (a == "-h" || a == "--help")
          continue;

        if (!a.empty() && a[0] != '-')
        {
          if (o.sdkMode)
          {
            add_sdk_profile_arg(o, a);
            continue;
          }

          if (o.globalMode)
          {
            o.globalSpec = trim_copy(a);
            continue;
          }

          o.localSpec = trim_copy(a);
          continue;
        }

        throw std::runtime_error("unknown argument: " + a);
      }

      if (o.globalMode && o.sdkMode)
        throw std::runtime_error("--global and --sdk cannot be used together");

      return o;
    }

    UninstallKind kind_from_options(const Options &opt)
    {
      if (opt.globalMode)
        return UninstallKind::GlobalPackage;
      if (opt.sdkMode)
        return UninstallKind::Sdk;
      if (opt.localSpec.has_value() && !opt.localSpec->empty())
        return UninstallKind::LocalDependency;
      return UninstallKind::Cli;
    }

    std::string kind_label(UninstallKind kind)
    {
      switch (kind)
      {
      case UninstallKind::Cli:
        return "cli";
      case UninstallKind::Sdk:
        return "sdk";
      case UninstallKind::GlobalPackage:
        return "global";
      case UninstallKind::LocalDependency:
        return "dependency";
      }

      return "cli";
    }

    int list_sdk_profiles(const Options &opt)
    {
      const auto installed = installed_sdk_profiles();

      if (opt.jsonOut)
      {
        json arr = json::array();
        for (const auto &[profile, path] : installed)
        {
          arr.push_back({
              {"profile", profile},
              {"current_version", read_sdk_current_version(profile).value_or("")},
              {"path", path.string()},
          });
        }

        print_json({
            {"ok", true},
            {"kind", "sdk"},
            {"profiles", arr},
        });
        return 0;
      }

      Fmt f(std::cout);
      print_header_box(
          std::cout,
          f,
          "uninstall",
          {
              {"target", "sdk"},
              {"mode", "list"},
          });

      if (installed.empty())
      {
        styled_warn(std::cout, f, "No SDK profiles installed.");
        return 0;
      }

      for (const auto &[profile, path] : installed)
      {
        const std::string version = read_sdk_current_version(profile).value_or("unknown");
        print_kv(std::cout, f, profile, version + "  " + f.dim_(path.string()));
      }

      return 0;
    }

    std::vector<std::string> sdk_profiles_to_remove(const Options &opt)
    {
      if (opt.sdkAll || (opt.sdkMode && opt.all && opt.sdkProfiles.empty()))
        return all_sdk_profiles();

      if (!opt.sdkProfiles.empty())
        return opt.sdkProfiles;

      return {"default"};
    }

    int run_global_uninstall(const Options &opt)
    {
      const auto started = std::chrono::steady_clock::now();
      const Fmt f(std::cout);
      const std::string requestedPkg = opt.globalSpec.value_or("");
      const std::string pkg = normalize_global_package_spec(requestedPkg);
      const bool detailed = opt.verbose || opt.dryRun;

      if (pkg.empty())
      {
        if (opt.jsonOut)
        {
          print_json({
              {"ok", false},
              {"error", "missing global package name"},
          });
          return 1;
        }

        styled_error(std::cerr, Fmt(std::cerr), "Missing package name.", "usage: vix uninstall -g <namespace/package>");
        return 1;
      }

      json root;
      try
      {
        root = load_global_manifest();
      }
      catch (const std::exception &ex)
      {
        if (opt.jsonOut)
        {
          print_json({
              {"ok", false},
              {"kind", "global"},
              {"package", pkg},
              {"error", ex.what()},
          });
          return 1;
        }

        styled_error(std::cerr, Fmt(std::cerr), ex.what());
        return 1;
      }

      if (!root.contains("packages") || !root["packages"].is_array())
        root["packages"] = json::array();

      auto &packages = root["packages"];
      auto found = packages.end();

      for (auto it = packages.begin(); it != packages.end(); ++it)
      {
        if (it->value("id", "") == pkg)
        {
          found = it;
          break;
        }
      }

      if (found == packages.end())
      {
        if (opt.jsonOut)
        {
          print_json({
              {"ok", false},
              {"kind", "global"},
              {"package", pkg},
              {"error", "package not found"},
          });
          return 1;
        }

        styled_error(std::cerr, Fmt(std::cerr), "Package not found: " + pkg);
        return 1;
      }

      const std::string installedPath = found->value("installed_path", "");
      const std::string version = found->value("version", "");
      std::vector<std::string> executableNames;
      if (found->contains("executables") && (*found)["executables"].is_array())
      {
        for (const auto &exe : (*found)["executables"])
        {
          if (exe.is_string())
            executableNames.push_back(exe.get<std::string>());
        }
      }

      if (!opt.jsonOut && detailed)
      {
        print_header_box(
            std::cout,
            f,
            "uninstall",
            {
                {"target", "global"},
                {"package", pkg},
                {"dry-run", yes_no(opt.dryRun)},
            });
      }

      json removed = json::array();
      bool usedRegisteredFiles = false;

      if (found->contains("files") && (*found)["files"].is_array())
      {
        usedRegisteredFiles = true;
        if (detailed)
          styled_step(std::cout, f, "Removing registered package files");

        std::vector<fs::path> parentDirs;
        for (const auto &file : (*found)["files"])
        {
          if (!file.is_string())
            continue;

          const std::string relString = file.get<std::string>();
          const fs::path rel = fs::path(relString).lexically_normal();
          if (!safe_relative_manifest_path(rel))
          {
            removed.push_back({{"path", relString}, {"kind", "package-file"}, {"removed", false}, {"error", "unsafe path"}});
            continue;
          }

          const std::string owner = owner_of_global_file(root, rel.generic_string());
          if (!owner.empty() && owner != pkg)
          {
            removed.push_back({{"path", rel.generic_string()}, {"kind", "package-file"}, {"removed", false}, {"error", "owned by " + owner}});
            continue;
          }

          const fs::path abs = global_root_dir() / rel;
          const RemoveResult rr = remove_path_best_effort(
              abs,
              "package-file",
              false,
              false,
              opt.dryRun);
          if (detailed)
            print_remove_result(opt, f, rr);
          removed.push_back(remove_result_json(rr));
          parentDirs.push_back(abs.parent_path());
        }

        if (!opt.dryRun)
        {
          std::sort(parentDirs.begin(), parentDirs.end(), [](const fs::path &a, const fs::path &b)
                    { return a.string().size() > b.string().size(); });
          for (const auto &dir : parentDirs)
            remove_empty_parents_under_global(dir);
        }
      }

      if (found->contains("shims") && (*found)["shims"].is_array())
      {
        for (const auto &shimValue : (*found)["shims"])
        {
          if (!shimValue.is_string())
            continue;

          const fs::path shim = fs::absolute(fs::path(shimValue.get<std::string>())).lexically_normal();
          std::error_code ec;
          const auto st = fs::symlink_status(shim, ec);
          if (ec || st.type() == fs::file_type::not_found)
            continue;

          if (!fs::is_symlink(st))
          {
            removed.push_back({{"path", shim.string()}, {"kind", "command-shim"}, {"removed", false}, {"error", "not a symlink"}});
            continue;
          }

          ec.clear();
          const fs::path rawTarget = fs::read_symlink(shim, ec);
          if (ec)
            continue;

          const fs::path target = rawTarget.is_absolute()
                                      ? rawTarget.lexically_normal()
                                      : fs::absolute(shim.parent_path() / rawTarget).lexically_normal();

          const fs::path globalBin =
              fs::absolute(global_root_dir() / "bin").lexically_normal();

          const std::string targetString = target.generic_string();
          const std::string globalBinString = globalBin.generic_string();

          if (targetString.rfind(globalBinString + '/', 0) != 0)
          {
            removed.push_back({
                {"path", shim.string()},
                {"kind", "command-shim"},
                {"removed", false},
                {"error", "target outside vix global bin"},
            });
            continue;
          }

          const RemoveResult rr = remove_path_best_effort(
              shim,
              "command-shim",
              false,
              false,
              opt.dryRun);
          if (detailed)
            print_remove_result(opt, f, rr);
          removed.push_back(remove_result_json(rr));
        }
      }

      if (!usedRegisteredFiles && !installedPath.empty())
      {
        if (detailed)
          styled_step(std::cout, f, "Removing legacy package files");
        const RemoveResult rr = remove_path_best_effort(
            fs::path(installedPath),
            "package",
            true,
            true,
            opt.dryRun);
        if (detailed)
          print_remove_result(opt, f, rr);
        removed.push_back(remove_result_json(rr));
      }

      if (!opt.dryRun)
      {
        packages.erase(found);
        save_global_manifest(root);
      }

      if (opt.jsonOut)
      {
        print_json({
            {"ok", true},
            {"kind", "global"},
            {"package", pkg},
            {"dry_run", opt.dryRun},
            {"removed", removed},
            {"manifest", global_manifest_path().string()},
        });
        return 0;
      }

      if (opt.dryRun)
      {
        styled_done(std::cout, f, "global package uninstall plan ready");
      }
      else
      {
        const std::string label = pkg + (version.empty() ? std::string() : "@" + version);
        vix::cli::util::ok_line(
            std::cout,
            "removed " + label + " in " + format_elapsed(std::chrono::steady_clock::now() - started));
      }

      return 0;
    }

    static std::string read_text_file_or_empty_uninstall(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};
      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }

    static bool write_text_atomic_uninstall(const fs::path &path, const std::string &content)
    {
      std::error_code ec;
      const fs::path tmp = path.string() + ".tmp";
      {
        std::ofstream out(tmp, std::ios::binary);
        if (!out)
          return false;
        out << content;
      }
      fs::rename(tmp, path, ec);
      if (!ec)
        return true;
      ec.clear();
      fs::remove(path, ec);
      ec.clear();
      fs::rename(tmp, path, ec);
      return !ec;
    }

    static std::string remove_git_dependency_block(const std::string &content, const std::string &name)
    {
      const std::string header = "[dependencies." + name + "]";
      const std::string cmakeHeader = "[dependencies." + name + ".cmake]";
      std::istringstream in(content);
      std::ostringstream out;
      std::string line;
      bool skipping = false;
      bool removedAny = false;

      while (std::getline(in, line))
      {
        const std::string trimmed = trim_copy(line);
        const bool isSection = starts_with(trimmed, "[") && trimmed.find(']') != std::string::npos;
        if (trimmed == header || trimmed == cmakeHeader)
        {
          skipping = true;
          removedAny = true;
          continue;
        }
        if (skipping && isSection)
          skipping = false;
        if (!skipping)
          out << line << "\n";
      }

      return removedAny ? out.str() : content;
    }

    int run_local_dependency_uninstall(const Options &opt)
    {
      const auto started = std::chrono::steady_clock::now();
      const std::string name = trim_copy(opt.localSpec.value_or(""));
      const Fmt f(std::cout);
      if (name.empty())
      {
        styled_error(std::cerr, Fmt(std::cerr), "Missing dependency name.", "usage: vix uninstall <name>");
        return 1;
      }

      const fs::path appPath = fs::current_path() / "vix.app";
      const fs::path lockPath = fs::current_path() / "vix.lock";
      const fs::path depPath = fs::current_path() / ".vix" / "deps" / name;

      bool changed = false;
      if (fs::exists(appPath))
      {
        const std::string before = read_text_file_or_empty_uninstall(appPath);
        const std::string after = remove_git_dependency_block(before, name);
        if (after != before)
        {
          if (!opt.dryRun && !write_text_atomic_uninstall(appPath, after))
          {
            styled_error(std::cerr, Fmt(std::cerr), "Could not update vix.app");
            return 1;
          }
          changed = true;
        }
      }

      if (fs::exists(lockPath))
      {
        try
        {
          json root = read_json_or_throw(lockPath);
          if (root.is_object() && root.contains("dependencies") && root["dependencies"].is_array())
          {
            json next = json::array();
            for (const auto &dep : root["dependencies"])
            {
              if (dep.is_object() && dep.value("id", "") == name)
              {
                changed = true;
                continue;
              }
              next.push_back(dep);
            }
            root["dependencies"] = next;
            if (!opt.dryRun)
              write_json_or_throw(lockPath, root);
          }
        }
        catch (const std::exception &ex)
        {
          styled_error(std::cerr, Fmt(std::cerr), std::string("Could not update vix.lock: ") + ex.what());
          return 1;
        }
      }

      if (fs::exists(depPath))
      {
        if (!opt.dryRun)
        {
          std::error_code ec;
          fs::remove_all(depPath, ec);
          if (ec)
          {
            styled_error(std::cerr, Fmt(std::cerr), "Could not remove dependency directory: " + depPath.string());
            return 1;
          }
        }
        changed = true;
      }

      if (!opt.dryRun)
      {
        const fs::path depsCmake = fs::current_path() / ".vix" / "vix_deps.cmake";
        std::error_code ec;
        fs::remove(depsCmake, ec);
      }

      if (!changed)
      {
        styled_error(std::cerr, Fmt(std::cerr), "Dependency not found: " + name);
        return 1;
      }

      vix::cli::util::ok_line(
          std::cout,
          "removed " + name + " in " + format_elapsed(std::chrono::steady_clock::now() - started));
      return 0;
    }

    int run_sdk_uninstall(const Options &opt)
    {
      if (opt.sdkList)
        return list_sdk_profiles(opt);

      const std::vector<std::string> profiles = sdk_profiles_to_remove(opt);

      for (const auto &profile : profiles)
      {
        if (!is_supported_sdk_profile(profile))
        {
          if (opt.jsonOut)
          {
            print_json({
                {"ok", false},
                {"kind", "sdk"},
                {"profile", profile},
                {"error", "unsupported SDK profile"},
            });
            return 1;
          }

          styled_error(std::cerr, Fmt(std::cerr), "Unsupported SDK profile: " + profile);
          return 1;
        }
      }

      const Fmt f(std::cout);
      if (!opt.jsonOut)
      {
        std::string target;
        if (profiles.size() == 1)
          target = profiles.front();
        else
          target = "multiple";

        print_header_box(
            std::cout,
            f,
            "uninstall",
            {
                {"target", "sdk"},
                {"profile", target},
                {"version", opt.version.value_or("all")},
                {"dry-run", yes_no(opt.dryRun)},
            });
      }

      json removed = json::array();
      bool removedAny = false;

      for (const auto &profile : profiles)
      {
        fs::path target = sdk_profile_root(profile);
        if (opt.version.has_value())
          target = sdk_profile_root(profile) / *opt.version;

        styled_step(std::cout, f, "Removing SDK profile " + profile);

        const RemoveResult sdkRemove = remove_path_best_effort(
            target,
            "sdk",
            true,
            true,
            opt.dryRun);
        print_remove_result(opt, f, sdkRemove);
        removed.push_back(remove_result_json(sdkRemove));
        removedAny = removedAny || sdkRemove.removed;

        if (!opt.version.has_value())
        {
          const RemoveResult cmakeRemove = remove_path_best_effort(
              cmake_registry_entry_path(profile),
              "CMake registry entry",
              false,
              false,
              opt.dryRun);
          print_remove_result(opt, f, cmakeRemove);
          removed.push_back(remove_result_json(cmakeRemove));
          removedAny = removedAny || cmakeRemove.removed;
        }
        else
        {
          const auto current = read_sdk_current_version(profile);
          if (current.has_value() && *current == *opt.version)
          {
            const RemoveResult currentMeta = remove_path_best_effort(
                sdk_current_metadata_path(profile),
                "SDK current metadata",
                false,
                false,
                opt.dryRun);
            print_remove_result(opt, f, currentMeta);
            removed.push_back(remove_result_json(currentMeta));

            const RemoveResult currentPtr = remove_path_best_effort(
                sdk_current_pointer_path(profile),
                "SDK current pointer",
                false,
                true,
                opt.dryRun);
            print_remove_result(opt, f, currentPtr);
            removed.push_back(remove_result_json(currentPtr));

            const RemoveResult cmakeRemove = remove_path_best_effort(
                cmake_registry_entry_path(profile),
                "CMake registry entry",
                false,
                false,
                opt.dryRun);
            print_remove_result(opt, f, cmakeRemove);
            removed.push_back(remove_result_json(cmakeRemove));
          }
        }
      }

      if (opt.jsonOut)
      {
        print_json({
            {"ok", true},
            {"kind", "sdk"},
            {"profiles", profiles},
            {"version", opt.version.has_value() ? json(*opt.version) : json(nullptr)},
            {"dry_run", opt.dryRun},
            {"removed_any", removedAny},
            {"removed", removed},
        });
        return 0;
      }

      if (opt.dryRun)
        styled_done(std::cout, f, "SDK uninstall plan ready");
      else if (removedAny)
        styled_done(std::cout, f, "SDK files removed");
      else
        styled_done(std::cout, f, "nothing to remove");

      return 0;
    }

    int run_cli_uninstall(const Options &opt)
    {
      const Fmt f(std::cout);
      const auto candidates = build_cli_candidate_paths(opt);

      if (!opt.jsonOut)
      {
        print_header_box(
            std::cout,
            f,
            "uninstall",
            {
                {"target", "cli"},
                {"candidates", std::to_string(candidates.size())},
                {"purge", yes_no(opt.purge)},
                {"dry-run", yes_no(opt.dryRun)},
            });
      }

      json removed = json::array();
      bool removedAny = false;

      if (candidates.empty() && !opt.jsonOut)
        styled_warn(std::cout, f, "No candidate paths found.");

      for (const auto &p : candidates)
      {
        styled_step(std::cout, f, "Checking " + p.string());

        const RemoveResult rr = remove_path_best_effort(
            p,
            "binary",
            false,
            false,
            opt.dryRun);

        print_remove_result(opt, f, rr);
        removed.push_back(remove_result_json(rr));

        if (rr.removed)
        {
          removedAny = true;
          if (!opt.all)
            break;
        }
      }

      const RemoveResult statsRemove = remove_path_best_effort(
          stats_file(),
          "install metadata",
          false,
          false,
          opt.dryRun);
      print_remove_result(opt, f, statsRemove);
      removed.push_back(remove_result_json(statsRemove));

      if (opt.purge)
      {
        styled_step(std::cout, f, "Purging local Vix data");

        const RemoveResult purgeRemove = remove_path_best_effort(
            vix_root(),
            "local store",
            true,
            true,
            opt.dryRun);
        print_remove_result(opt, f, purgeRemove);
        removed.push_back(remove_result_json(purgeRemove));
      }

      const std::string stillFound = trim_copy(exec_capture(
#ifdef _WIN32
          "where vix" + stderr_null_suffix()
#else
          "command -v vix 2>/dev/null"
#endif
              ));

      if (opt.jsonOut)
      {
        print_json({
            {"ok", true},
            {"kind", "cli"},
            {"dry_run", opt.dryRun},
            {"purge", opt.purge},
            {"removed_any", removedAny},
            {"removed", removed},
            {"still_in_path", stillFound.empty() ? json(nullptr) : json(stillFound)},
        });
        return 0;
      }

      if (!stillFound.empty() && !opt.dryRun)
        styled_warn(std::cerr, Fmt(std::cerr), "Still found in PATH: " + stillFound);

      hint_line(std::cout, f, "run: hash -r");
      hint_line(std::cout, f, "restart the terminal if your shell still sees vix");

      if (opt.dryRun)
        styled_done(std::cout, f, "CLI uninstall plan ready");
      else if (removedAny)
        styled_done(std::cout, f, "Vix CLI uninstalled");
      else
        styled_done(std::cout, f, "uninstall finished, nothing removed");

      return 0;
    }
  }

  int UninstallCommand::run(const std::vector<std::string> &args)
  {
    try
    {
      bool wantHelp = false;
      for (const auto &a : args)
      {
        if (a == "-h" || a == "--help")
        {
          wantHelp = true;
          break;
        }
      }

      if (wantHelp)
        return help();

      const Options opt = parse_args(args);
      const UninstallKind kind = kind_from_options(opt);

      verbose_line(opt, "kind=" + kind_label(kind));

      switch (kind)
      {
      case UninstallKind::Cli:
        return run_cli_uninstall(opt);
      case UninstallKind::Sdk:
        return run_sdk_uninstall(opt);
      case UninstallKind::GlobalPackage:
        return run_global_uninstall(opt);
      case UninstallKind::LocalDependency:
        return run_local_dependency_uninstall(opt);
      }

      return 1;
    }
    catch (const std::exception &ex)
    {
      if (std::find(args.begin(), args.end(), "--json") != args.end())
      {
        print_json({
            {"ok", false},
            {"error", ex.what()},
        });
        return 1;
      }

      styled_error(std::cerr, Fmt(std::cerr), ex.what());
      return 1;
    }
  }

  int UninstallCommand::help()
  {
    std::cout
        << "vix uninstall\n"
        << "Remove the Vix CLI, SDK profiles, or globally installed packages.\n\n"

        << "Usage\n"
        << "  vix uninstall [options]\n"
        << "  vix uninstall --sdk <profile> [options]\n"
        << "  vix uninstall --sdk-list\n"
        << "  vix uninstall <dependency>\n"
        << "  vix uninstall --sdk-all\n"
        << "  vix uninstall -g <package> [options]\n\n"

        << "Examples\n"
        << "  vix uninstall\n"
        << "  vix uninstall --purge\n"
        << "  vix uninstall --dry-run\n"
        << "  vix uninstall --all --system\n"
        << "  vix uninstall --path /home/user/.local/bin/vix\n"
        << "  vix uninstall --sdk web\n"
        << "  vix uninstall --sdk all\n"
        << "  vix uninstall --sdk web --version v2.7.0\n"
        << "  vix uninstall --sdk-list\n"
        << "  vix uninstall --sdk-all\n"
        << "  vix uninstall sample\n"
        << "  vix uninstall -g gk/jwt\n\n"

        << "CLI options\n"
        << "  --purge             Remove ~/.vix after removing the CLI\n"
        << "  --all               Try every detected CLI path instead of stopping after the first removal\n"
        << "  --system            Include common system locations such as /usr/local/bin and /usr/bin\n"
        << "  --prefix <dir>      Remove <dir>/bin/vix\n"
        << "  --path <file>       Remove a specific Vix binary\n\n"

        << "SDK options\n"
        << "  --sdk <profile>     Remove an SDK profile: default, web, data, desktop, p2p, game, agent, all\n"
        << "  --sdk-list          List installed SDK profiles\n"
        << "  --sdk-all           Remove all known SDK profiles\n"
        << "  --version <tag>     Remove one SDK version from the selected profile\n\n"

        << "Dependency uninstall\n"
        << "  vix uninstall <name> removes a Git dependency from vix.app, vix.lock, and .vix/deps/\n\n"

        << "Global package options\n"
        << "  -g, --global <pkg>  Remove a globally installed package\n\n"

        << "General options\n"
        << "  --dry-run           Print what would be removed without changing the filesystem\n"
        << "  --json              Print machine-readable output\n"
        << "  --verbose           Print debug information\n"
        << "  -h, --help          Show this help\n";

    return 0;
  }
}
