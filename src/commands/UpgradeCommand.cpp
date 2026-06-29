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
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
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

    enum class UpgradeKind
    {
      Cli,
      Sdk,
      GlobalPackage,
    };

    struct Options
    {
      bool check{false};
      bool dryRun{false};
      bool jsonOut{false};
      bool verbose{false};
      bool globalMode{false};
      bool sdkMode{false};
      bool sdkList{false};
      bool sdkInfo{false};

      std::string sdkProfile{"default"};
      std::vector<std::string> sdkProfiles;
      std::optional<std::string> version;
      std::optional<std::string> globalSpec;
    };

    struct Platform
    {
      std::string os;
      std::string arch;

      std::string label() const
      {
        return os + "/" + arch;
      }
    };

    struct ReleaseAsset
    {
      std::string name;
      std::string url;
      std::string sha256Url;
      std::string minisignUrl;
      std::optional<long long> downloadBytes;
    };

    struct UpgradePlan
    {
      UpgradeKind kind{UpgradeKind::Cli};
      Platform platform;
      std::string repo;
      std::string version;
      std::optional<std::string> currentVersion;
      std::string sdkProfile;
      ReleaseAsset asset;
      fs::path installDir;
      fs::path destination;
      bool supported{true};
      std::string unsupportedReason;
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

    struct ScopedCleanup
    {
      fs::path p;

      ~ScopedCleanup()
      {
        std::error_code ec;
        if (!p.empty())
          fs::remove_all(p, ec);
      }
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

    constexpr const char *kMinisignPubkey =
        "RWSIfpPSznK9A1gWUc8Eg2iXXQwU5d9BYuQNKGOcoujAF2stPu5rKFjQ";

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

    bool wants_json(const std::vector<std::string> &args)
    {
      return std::find(args.begin(), args.end(), "--json") != args.end();
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

    std::string quiet_suffix()
    {
#ifdef _WIN32
      return " >nul 2>&1";
#else
      return " >/dev/null 2>&1";
#endif
    }

    std::string stderr_null_suffix()
    {
#ifdef _WIN32
      return " 2>nul";
#else
      return " 2>/dev/null";
#endif
    }

    bool have_cmd(const std::string &name)
    {
#ifdef _WIN32
      const std::string cmd = "where " + shell_quote(name) + " >nul 2>&1";
#else
      const std::string cmd = "command -v " + shell_quote(name) + " >/dev/null 2>&1";
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

    int run_quiet_or_verbose(const std::string &cmd, bool verbose)
    {
      if (verbose)
      {
        std::cerr << "debug: " << cmd << "\n";
        return exec_status(cmd);
      }

      return exec_status(cmd + quiet_suffix());
    }

    void print_json(const json &j)
    {
      std::cout << j.dump(2) << "\n";
    }

    std::vector<std::string> split_sdk_profiles(const std::string &raw)
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
      for (const auto &profile : split_sdk_profiles(raw))
      {
        if (!profile.empty())
          o.sdkProfiles.push_back(profile);
      }

      if (!o.sdkProfiles.empty())
        o.sdkProfile = o.sdkProfiles.front();
    }

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

      std::string dl() const
      {
        return color ? std::string(ansi::blue) + "↓" + ansi::reset : "↓";
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

    void blank_line(std::ostream &os = std::cout)
    {
      os << "\n";
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

    void styled_step(
        std::ostream &os,
        const Fmt &f,
        const std::string &symKind,
        const std::string &msg)
    {
      std::string sym =
          symKind == "dl"   ? f.dl()
          : symKind == "ok" ? f.ok()
                            : f.arrow();

      sym_line(os, sym, msg);
    }

    void styled_ok(std::ostream &os, const Fmt &f, const std::string &msg)
    {
      sym_line(os, f.ok(), msg);
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

    // Compatibility wrappers.
    // Keep these because run_global_upgrade and old error paths still call them.
    void info_line(std::ostream &os, const std::string &line)
    {
      Fmt f(os);
      sym_line(os, f.dot(), line);
    }

    void step_line(std::ostream &os, const std::string &line)
    {
      Fmt f(os);
      styled_step(os, f, "arrow", line);
    }

    void error_line(std::ostream &os, const std::string &line)
    {
      Fmt f(os);
      sym_line(os, f.err(), f.red_(line));
    }

    void field_line(std::ostream &os, const std::string &key, const std::string &value)
    {
      Fmt f(os);
      print_kv(os, f, to_lower(key), value);
    }

    void verbose_line(const Options &opt, const std::string &line)
    {
      if (opt.verbose && !opt.jsonOut)
        std::cerr << "debug: " << line << "\n";
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

      std::ostringstream oss;
      oss << std::setw(4) << std::setfill('0') << (tm.tm_year + 1900)
          << "-"
          << std::setw(2) << std::setfill('0') << (tm.tm_mon + 1)
          << "-"
          << std::setw(2) << std::setfill('0') << tm.tm_mday
          << "T"
          << std::setw(2) << std::setfill('0') << tm.tm_hour
          << ":"
          << std::setw(2) << std::setfill('0') << tm.tm_min
          << ":"
          << std::setw(2) << std::setfill('0') << tm.tm_sec
          << "Z";

      return oss.str();
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

    fs::path sdk_root_dir()
    {
      return vix_root() / "sdk";
    }

    fs::path sdk_profile_root(const std::string &profile)
    {
      return sdk_root_dir() / profile;
    }

    fs::path sdk_install_dir(const std::string &profile, const std::string &version)
    {
      return sdk_profile_root(profile) / version;
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

    void register_sdk_with_cmake_user_registry(
        const std::string &profile,
        const fs::path &installDir)
    {
      const fs::path configDir = installDir / "lib" / "cmake" / "Vix";
      const fs::path configFile = configDir / "VixConfig.cmake";

      std::error_code ec;

      if (!fs::exists(configFile, ec) || ec)
      {
        throw std::runtime_error(
            "installed SDK is missing VixConfig.cmake: " + configFile.string());
      }

      const fs::path registryDir = cmake_user_package_registry_dir();
      fs::create_directories(registryDir, ec);

      if (ec)
      {
        throw std::runtime_error(
            "failed to create CMake user package registry: " + registryDir.string());
      }

      const fs::path entry =
          registryDir / sanitize_cmake_registry_entry_name("vix-sdk-" + profile);

      std::ofstream out(entry);

      if (!out)
      {
        throw std::runtime_error(
            "failed to write CMake package registry entry: " + entry.string());
      }

      out << configDir.string() << "\n";
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

    void write_cli_metadata(
        const std::string &repoStr,
        const std::string &tag,
        const std::string &os,
        const std::string &arch,
        const fs::path &installDir,
        std::optional<long long> downloadBytes,
        const std::string &installedVersion,
        const std::string &assetUrl)
    {
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

      write_json_or_throw(stats_file(), j);
    }

    json make_sdk_metadata(
        const std::string &profile,
        const std::string &tag,
        const Platform &platform,
        const fs::path &installDir,
        const ReleaseAsset &asset)
    {
      return json{
          {"kind", "sdk"},
          {"profile", profile},
          {"version", tag},
          {"installed_version", tag},
          {"os", platform.os},
          {"arch", platform.arch},
          {"install_dir", installDir.string()},
          {"asset_url", asset.url},
          {"installed_at", utc_now_iso()},
          {"download_bytes", asset.downloadBytes.has_value() ? json(*asset.downloadBytes) : json(nullptr)},
      };
    }

    void write_sdk_metadata(
        const std::string &profile,
        const std::string &tag,
        const Platform &platform,
        const fs::path &installDir,
        const ReleaseAsset &asset)
    {
      const json meta = make_sdk_metadata(profile, tag, platform, installDir, asset);
      write_json_or_throw(installDir / "install.json", meta);
      write_json_or_throw(sdk_current_metadata_path(profile), meta);
    }

    std::optional<std::string> read_sdk_current_version(const std::string &profile)
    {
      const auto meta = read_json_if_exists(sdk_current_metadata_path(profile));
      if (!meta.has_value())
        return std::nullopt;

      const std::string version = meta->value("installed_version", meta->value("version", ""));
      if (version.empty())
        return std::nullopt;

      const std::string installDir = meta->value("install_dir", "");
      if (!installDir.empty())
      {
        std::error_code ec;
        if (!fs::exists(fs::path(installDir), ec))
          return std::nullopt;
      }

      return version;
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

    std::string normalize_os(std::string os)
    {
      os = to_lower(trim_copy(os));
      if (os == "darwin" || os == "osx" || os == "mac")
        return "macos";
      if (os == "win32" || os == "win64" || os == "mingw" || os == "msys")
        return "windows";
      return os;
    }

    std::string normalize_arch(std::string arch)
    {
      arch = to_lower(trim_copy(arch));
      if (arch == "amd64" || arch == "x64")
        return "x86_64";
      if (arch == "arm64")
        return "aarch64";
      return arch;
    }

    Platform detect_platform()
    {
      Platform p;

#ifdef _WIN32
      p.os = "windows";
#elif __APPLE__
      p.os = "macos";
#else
      p.os = "linux";
#endif

#if defined(__x86_64__) || defined(_M_X64)
      p.arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
      p.arch = "aarch64";
#else
      p.arch = "unknown";
#endif

      p.os = normalize_os(p.os);
      p.arch = normalize_arch(p.arch);
      return p;
    }

    bool is_supported_sdk_profile(const std::string &profile)
    {
      return std::find(kSdkProfiles.begin(), kSdkProfiles.end(), profile) != kSdkProfiles.end();
    }

    bool is_sdk_list_token(const std::string &value)
    {
      const std::string v = to_lower(trim_copy(value));
      return v == "list" || v == "ls" || v == "profiles";
    }

    bool is_sdk_info_token(const std::string &value)
    {
      const std::string v = to_lower(trim_copy(value));
      return v == "info" || v == "about" || v == "show" || v == "deps";
    }

    std::string available_sdk_profiles_text()
    {
      std::ostringstream oss;
      for (const char *profile : kSdkProfiles)
        oss << "  " << profile << "\n";
      return oss.str();
    }

    std::string cli_asset_name(const Platform &platform)
    {
      if (platform.os == "windows")
        return "vix-windows-" + platform.arch + ".zip";
      return "vix-" + platform.os + "-" + platform.arch + ".tar.gz";
    }

    std::string sdk_asset_name(const std::string &profile, const Platform &platform)
    {
      return "vix-sdk-" + profile + "-" + platform.os + "-" + platform.arch + ".tar.gz";
    }

    std::string legacy_sdk_asset_name(const Platform &platform)
    {
      if (platform.os == "windows")
        return "vix-sdk-windows-" + platform.arch + ".zip";

      return "vix-sdk-" + platform.os + "-" + platform.arch + ".tar.gz";
    }

    std::string github_release_base_url(const std::string &repoStr, const std::string &tag)
    {
      return "https://github.com/" + repoStr + "/releases/download/" + tag;
    }

    ReleaseAsset github_release_asset(
        const std::string &repoStr,
        const std::string &tag,
        const std::string &assetName)
    {
      const std::string url = github_release_base_url(repoStr, tag) + "/" + assetName;
      return ReleaseAsset{
          assetName,
          url,
          url + ".sha256",
          url + ".minisig",
          std::nullopt,
      };
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

    std::optional<std::string> get_installed_version_from_command(const fs::path &exe)
    {
#ifdef _WIN32
      const std::string cmd = shell_quote(exe.string()) + " --version 2>nul";
#else
      const std::string cmd = shell_quote(exe.string()) + " --version 2>/dev/null";
#endif

      const std::string out = exec_capture(cmd);
      return extract_version_token(out);
    }

    std::optional<std::string> get_installed_version(const fs::path &exe)
    {
      std::error_code ec;
      if (fs::exists(exe, ec))
        return get_installed_version_from_command(exe);

      if (!exe.is_absolute() && have_cmd(exe.string()))
        return get_installed_version_from_command(exe);

      return std::nullopt;
    }

    std::string resolve_latest_tag_github_api(const std::string &repoStr)
    {
      const std::string api = "https://api.github.com/repos/" + repoStr + "/releases/latest";

      std::string body;

      if (have_cmd("curl"))
      {
        body = exec_capture("curl -fsSL -H " + shell_quote("User-Agent: vix-upgrade") + " " + shell_quote(api) + stderr_null_suffix());
      }
#ifndef _WIN32
      else if (have_cmd("wget"))
      {
        body = exec_capture("wget -qO- --header=" + shell_quote("User-Agent: vix-upgrade") + " " + shell_quote(api) + stderr_null_suffix());
      }
#else
      else
      {
        const std::string ps =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$r=Invoke-RestMethod -Uri '" +
            api + "' -Headers @{ 'User-Agent'='vix-upgrade' }; "
                  "if($null -eq $r.tag_name){ exit 1 } "
                  "Write-Output $r.tag_name\"";
        body = exec_capture(ps);
      }
#endif

      body = trim_copy(body);

      if (!body.empty() && body.front() == '{')
      {
        try
        {
          const json j = json::parse(body);
          const std::string tag = j.value("tag_name", "");
          if (!tag.empty())
            return tag;
        }
        catch (...)
        {
          const std::string key = "\"tag_name\"";
          const std::size_t k = body.find(key);
          if (k != std::string::npos)
          {
            const std::size_t colon = body.find(':', k + key.size());
            const std::size_t q1 = colon == std::string::npos ? std::string::npos : body.find('"', colon);
            const std::size_t q2 = q1 == std::string::npos ? std::string::npos : body.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
            {
              const std::string tag = body.substr(q1 + 1, q2 - (q1 + 1));
              if (!tag.empty())
                return tag;
            }
          }
        }

        throw std::runtime_error("could not resolve latest tag");
      }

      if (starts_with(body, "v"))
        return body;

      throw std::runtime_error("could not resolve latest tag. Use: vix upgrade --version vX.Y.Z");
    }

    std::optional<long long> remote_content_length(const std::string &url)
    {
      if (have_cmd("curl"))
      {
        const std::string headers = exec_capture("curl -fsSLI " + shell_quote(url) + stderr_null_suffix());
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

    bool remote_url_exists(const std::string &url)
    {
      if (url.empty())
        return false;

      if (have_cmd("curl"))
      {
        const std::string cmd =
            "curl -fsSLI " + shell_quote(url) + quiet_suffix();

        return exec_status(cmd) == 0;
      }

#ifndef _WIN32
      if (have_cmd("wget"))
      {
        const std::string cmd =
            "wget --spider -q " + shell_quote(url) + quiet_suffix();

        return exec_status(cmd) == 0;
      }
#else
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -Command "
          "\"try{"
          "$r=[System.Net.HttpWebRequest]::Create('" +
          url +
          "');"
          "$r.Method='HEAD';"
          "$r.AllowAutoRedirect=$true;"
          "$resp=$r.GetResponse();"
          "$code=[int]$resp.StatusCode;"
          "$resp.Close();"
          "if($code -ge 200 -and $code -lt 400){ exit 0 }"
          "exit 1"
          "}catch{ exit 1 }\"";

      return exec_status(ps + quiet_suffix()) == 0;
#endif

      return false;
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

    void download_file(const std::string &url, const fs::path &out, bool verbose)
    {
      std::error_code ec;
      fs::create_directories(out.parent_path(), ec);

      const std::string o = out.string();

      if (have_cmd("curl"))
      {
        const std::string cmd = "curl -fSsL " + shell_quote(url) + " -o " + shell_quote(o);
        if (run_quiet_or_verbose(cmd, verbose) != 0)
          throw std::runtime_error("download failed");
        return;
      }

#ifndef _WIN32
      if (have_cmd("wget"))
      {
        const std::string cmd = "wget -qO " + shell_quote(o) + " " + shell_quote(url);
        if (run_quiet_or_verbose(cmd, verbose) != 0)
          throw std::runtime_error("download failed");
        return;
      }
#else
      const std::string ps =
          "powershell -NoProfile -ExecutionPolicy Bypass -Command "
          "\"Invoke-WebRequest -Uri '" +
          url + "' -OutFile '" + o + "' -Headers @{ 'User-Agent'='vix-upgrade' }\"";

      if (run_quiet_or_verbose(ps, verbose) != 0)
        throw std::runtime_error("download failed");

      return;
#endif

      throw std::runtime_error("need curl (or wget on Unix) to download");
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

    void verify_sha256_or_throw(
        const std::string &shaUrl,
        const fs::path &artifact,
        const fs::path &tmpDir,
        bool verbose)
    {
      const fs::path shaPath = tmpDir / (artifact.filename().string() + ".sha256");
      download_file(shaUrl, shaPath, verbose);

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
        const std::string &pubkey,
        bool verbose)
    {
      if (!have_cmd("minisign"))
        return false;

      const fs::path sigPath = tmpDir / (artifact.filename().string() + ".minisig");

      try
      {
        download_file(sigUrl, sigPath, verbose);
      }
      catch (...)
      {
        return false;
      }

      const std::string cmd =
          "minisign -V -P " + shell_quote(pubkey) +
          " -m " + shell_quote(artifact.string()) +
          " -x " + shell_quote(sigPath.string());

      return run_quiet_or_verbose(cmd, verbose) == 0;
    }

    void extract_archive(
        const fs::path &archive,
        const fs::path &extractDir,
        bool verbose)
    {
      std::error_code ec;
      fs::create_directories(extractDir, ec);

#ifdef _WIN32
      if (archive.extension() == ".zip")
      {
        const std::string ps =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"Expand-Archive -LiteralPath '" +
            archive.string() + "' -DestinationPath '" + extractDir.string() + "' -Force\"";

        if (run_quiet_or_verbose(ps, verbose) != 0)
          throw std::runtime_error("failed to extract archive");
        return;
      }
#endif

      if (!have_cmd("tar"))
        throw std::runtime_error("tar is required to extract the archive");

      const std::string cmd =
          "tar -xzf " + shell_quote(archive.string()) +
          " -C " + shell_quote(extractDir.string());

      if (run_quiet_or_verbose(cmd, verbose) != 0)
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
        return 1;

      spec.resolvedVersion = latest;
      return 0;
    }

    bool registry_present()
    {
      return fs::exists(registry_dir()) && fs::exists(registry_index_dir());
    }

    int clone_checkout(
        const std::string &repoUrl,
        const std::string &idDot,
        const std::string &commit,
        std::string &outDir,
        bool verbose)
    {
      fs::create_directories(store_git_dir());

      const fs::path dst = store_git_dir() / idDot / commit;
      outDir = dst.string();

      if (fs::exists(dst))
        return 0;

      fs::create_directories(dst.parent_path());

      {
        const std::string cmd =
            "git clone -q " + shell_quote(repoUrl) + " " + shell_quote(dst.string());
        const int rc = verbose ? vix::cli::util::run_cmd_retry_debug(cmd) : run_quiet_or_verbose(cmd, false);
        if (rc != 0)
          return rc;
      }

      {
        const std::string cmd =
            "git -C " + shell_quote(dst.string()) +
            " -c advice.detachedHead=false checkout -q " + shell_quote(commit);
        const int rc = verbose ? vix::cli::util::run_cmd_retry_debug(cmd) : run_quiet_or_verbose(cmd, false);
        if (rc != 0)
          return rc;
      }

      return 0;
    }

    void remove_all_if_exists(const fs::path &p)
    {
      std::error_code ec;
      if (fs::exists(p, ec) || fs::is_symlink(p, ec))
        fs::remove_all(p, ec);
    }

    void copy_dir_or_throw(const fs::path &src, const fs::path &dst)
    {
      std::error_code ec;
      fs::create_directories(dst, ec);
      fs::copy(
          src,
          dst,
          fs::copy_options::recursive |
              fs::copy_options::copy_symlinks |
              fs::copy_options::overwrite_existing,
          ec);
      if (ec)
        throw std::runtime_error("failed to copy directory: " + dst.string());
    }

    void ensure_symlink_or_copy_dir(const fs::path &src, const fs::path &dst)
    {
      std::error_code ec;
      remove_all_if_exists(dst);

#ifdef _WIN32
      copy_dir_or_throw(src, dst);
#else
      fs::create_directories(dst.parent_path(), ec);
      fs::create_directory_symlink(src, dst, ec);
      if (ec)
      {
        ec.clear();
        copy_dir_or_throw(src, dst);
      }
#endif
    }

    bool verify_dependency_hash(const DepResolved &dep)
    {
      if (dep.hash.empty())
        return true;

      const auto actualHashOpt = vix::cli::util::sha256_directory(dep.checkout);
      if (!actualHashOpt)
        return true;

      return *actualHashOpt == dep.hash;
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

    Options parse_options(const std::vector<std::string> &args)
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

        if (a == "--verbose")
        {
          o.verbose = true;
          continue;
        }

        if (a == "--sdk-list")
        {
          o.sdkMode = true;
          o.sdkList = true;
          continue;
        }

        if (a == "--sdk-info")
        {
          o.sdkMode = true;
          o.sdkInfo = true;

          if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
            o.sdkProfile = to_lower(trim_copy(args[++i]));

          continue;
        }

        if (starts_with(a, "--sdk-info="))
        {
          o.sdkMode = true;
          o.sdkInfo = true;

          const std::string value = to_lower(trim_copy(a.substr(std::string("--sdk-info=").size())));
          o.sdkProfile = value.empty() ? "default" : value;

          continue;
        }

        if (a == "--version")
        {
          if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-')
            throw std::runtime_error("missing value after --version");
          o.version = args[++i];
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

            if (is_sdk_list_token(value))
            {
              o.sdkList = true;
            }
            else if (is_sdk_info_token(value))
            {
              o.sdkInfo = true;

              if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
                o.sdkProfile = to_lower(trim_copy(args[++i]));
            }
            else
            {
              add_sdk_profile_arg(o, value);
            }
          }
          else
          {
            o.sdkProfile = "default";
          }

          continue;
        }

        if ((a == "--list" || a == "list" || a == "ls" || a == "profiles") && o.sdkMode)
        {
          o.sdkList = true;
          continue;
        }

        if ((a == "--info" || a == "info" || a == "about" || a == "deps") && o.sdkMode)
        {
          o.sdkInfo = true;
          continue;
        }

        if (starts_with(a, "--sdk="))
        {
          o.sdkMode = true;

          const std::string value = to_lower(trim_copy(a.substr(std::string("--sdk=").size())));

          if (value.empty())
          {
            o.sdkProfile = "default";
          }
          else if (is_sdk_list_token(value))
          {
            o.sdkList = true;
          }
          else if (is_sdk_info_token(value))
          {
            o.sdkInfo = true;
          }
          else
          {
            o.sdkProfile = value;
          }

          continue;
        }

        if (a == "-g" || a == "--global")
        {
          o.globalMode = true;

          if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
          {
            o.globalSpec = args[++i];
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
          else if (o.sdkMode)
          {
            if (is_sdk_info_token(a))
            {
              o.sdkInfo = true;
              continue;
            }

            if (is_sdk_list_token(a))
            {
              o.sdkList = true;
              continue;
            }

            add_sdk_profile_arg(o, a);
            continue;
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

      if (o.globalMode && o.sdkMode)
        throw std::runtime_error("--sdk cannot be used with -g/--global");

      if (o.sdkList && o.sdkInfo)
        throw std::runtime_error("--sdk list cannot be used with --sdk info");

      if (o.sdkMode && o.sdkProfiles.empty() && !o.sdkList && !o.sdkInfo)
      {
        o.sdkProfiles.push_back(o.sdkProfile.empty() ? "default" : o.sdkProfile);
      }

      if (!o.sdkProfiles.empty())
        o.sdkProfile = o.sdkProfiles.front();

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

    UpgradePlan make_cli_plan(const Options &opt)
    {
      UpgradePlan plan;
      plan.kind = UpgradeKind::Cli;
      plan.repo = repo();
      plan.platform = detect_platform();

      if (plan.platform.arch == "unknown")
      {
        plan.supported = false;
        plan.unsupportedReason = "unsupported cpu architecture";
        return plan;
      }

      const fs::path exePath = fs::path(current_exe_path());
      plan.installDir = exe_install_dir_guess(exePath);
      plan.destination = plan.installDir /
#ifdef _WIN32
                         "vix.exe";
#else
                         "vix";
#endif

      plan.currentVersion = get_installed_version(plan.destination);
      if (!plan.currentVersion.has_value())
        plan.currentVersion = get_installed_version(exePath);

      plan.version = opt.version.has_value() ? *opt.version : resolve_latest_tag_github_api(plan.repo);
      plan.asset = github_release_asset(plan.repo, plan.version, cli_asset_name(plan.platform));
      plan.asset.downloadBytes = remote_content_length(plan.asset.url);

      return plan;
    }

    UpgradePlan make_sdk_plan(const Options &opt)
    {
      UpgradePlan plan;
      plan.kind = UpgradeKind::Sdk;
      plan.repo = repo();
      plan.platform = detect_platform();
      plan.sdkProfile = opt.sdkProfile.empty() ? "default" : to_lower(opt.sdkProfile);

      if (!is_supported_sdk_profile(plan.sdkProfile))
      {
        plan.supported = false;
        plan.unsupportedReason = "unknown sdk profile";
        return plan;
      }

      if (plan.platform.os != "linux" || plan.platform.arch != "x86_64")
      {
        plan.version = opt.version.value_or("");
        plan.supported = false;
        plan.unsupportedReason = "sdk asset not available for platform";
        if (!plan.version.empty())
          plan.asset = github_release_asset(plan.repo, plan.version, sdk_asset_name(plan.sdkProfile, plan.platform));
        return plan;
      }

      plan.version = opt.version.has_value() ? *opt.version : resolve_latest_tag_github_api(plan.repo);
      plan.installDir = sdk_install_dir(plan.sdkProfile, plan.version);
      plan.destination = plan.installDir;
      plan.currentVersion = read_sdk_current_version(plan.sdkProfile);

      plan.asset = github_release_asset(
          plan.repo,
          plan.version,
          sdk_asset_name(plan.sdkProfile, plan.platform));

      if (!remote_url_exists(plan.asset.url))
      {
        plan.supported = false;
        plan.unsupportedReason = "sdk asset not found";
        return plan;
      }

      plan.asset.downloadBytes = remote_content_length(plan.asset.url);

      return plan;
    }

    json plan_to_json(const UpgradePlan &plan, const Options &opt)
    {
      json result;
      result["command"] = "upgrade";
      result["mode"] = plan.kind == UpgradeKind::Sdk ? "sdk" : "cli";
      result["check"] = opt.check;
      result["dry_run"] = opt.dryRun;
      result["verbose"] = opt.verbose;
      result["repo"] = plan.repo;
      result["version"] = plan.version.empty() ? json(nullptr) : json(plan.version);
      result["current_version"] = plan.currentVersion.has_value() ? json(*plan.currentVersion) : json(nullptr);
      result["os"] = plan.platform.os;
      result["arch"] = plan.platform.arch;
      result["platform"] = plan.platform.label();
      result["install_dir"] = plan.installDir.string();
      result["asset"] = plan.asset.name.empty() ? json(nullptr) : json(plan.asset.name);
      result["url"] = plan.asset.url.empty() ? json(nullptr) : json(plan.asset.url);
      result["download_size_bytes"] = plan.asset.downloadBytes.has_value() ? json(*plan.asset.downloadBytes) : json(nullptr);
      result["supported"] = plan.supported;

      if (plan.kind == UpgradeKind::Sdk)
        result["profile"] = plan.sdkProfile;

      if (!plan.unsupportedReason.empty())
        result["reason"] = plan.unsupportedReason;

      return result;
    }

    void print_already_up_to_date(const UpgradePlan &plan)
    {
      Fmt f(std::cout);

      const std::string label =
          plan.kind == UpgradeKind::Sdk
              ? "upgrade --sdk " + plan.sdkProfile
              : "upgrade";

      std::vector<std::pair<std::string, std::string>> fields;

      if (plan.kind == UpgradeKind::Sdk)
        fields.push_back({"profile", f.blu(f.bold_(plan.sdkProfile))});

      fields.push_back({"version", f.bold_(plan.version)});
      fields.push_back({"platform", f.dim_(plan.platform.label())});

      print_header_box(std::cout, f, label, fields);

      sym_line(
          std::cout,
          f.ok(),
          "already up to date " + f.dim_("(" + plan.version + ")"));
    }

    void print_human_check_or_dry_run(const UpgradePlan &plan, const Options &opt)
    {
      Fmt f(std::cout);

      const bool isSdk = plan.kind == UpgradeKind::Sdk;

      const std::string label =
          isSdk
              ? std::string("upgrade --sdk ") + plan.sdkProfile + (opt.check ? " --check" : " --dry-run")
              : std::string("upgrade") + (opt.check ? " --check" : " --dry-run");

      std::vector<std::pair<std::string, std::string>> fields;

      if (isSdk)
        fields.push_back({"profile", f.blu(f.bold_(plan.sdkProfile))});
      else
        fields.push_back({"current", f.dim_(plan.currentVersion.value_or("unknown"))});

      const std::string targetKey = opt.version.has_value() ? "target" : "latest";
      fields.push_back({targetKey, f.grn(f.bold_(plan.version))});
      fields.push_back({"platform", f.dim_(plan.platform.label())});

      if (opt.verbose)
      {
        fields.push_back({"asset", f.dim_(plan.asset.name)});

        if (isSdk && !plan.installDir.empty())
          fields.push_back({"install", f.dim_(plan.installDir.string())});

        if (plan.asset.downloadBytes.has_value())
          fields.push_back({"size", f.dim_(human_bytes(*plan.asset.downloadBytes))});
      }

      print_header_box(std::cout, f, label, fields);

      if (opt.check && !isSdk)
      {
        const std::string current = plan.currentVersion.value_or("?");

        if (current != plan.version)
        {
          sym_line(
              std::cout,
              f.ok(),
              "update available  " +
                  f.grn(f.bold_(current)) +
                  f.dim_(" → ") +
                  f.grn(f.bold_(plan.version)));
        }
        else
        {
          sym_line(
              std::cout,
              f.ok(),
              "already up to date " + f.dim_("(" + plan.version + ")"));
        }

        hint_line(std::cout, f, "run  vix upgrade  to install");
      }
      else
      {
        sym_line(
            std::cout,
            f.dot(),
            f.dim_(opt.check ? "no files changed" : "dry run — no files changed"));
      }
    }

    void print_unsupported_sdk(const UpgradePlan &plan)
    {
      Fmt f(std::cerr);

      if (plan.unsupportedReason == "unknown sdk profile")
      {
        sym_line(
            std::cerr,
            f.err(),
            f.bold_("unknown sdk profile: ") + f.red_(plan.sdkProfile));

        blank_line(std::cerr);

        hint_line(
            std::cerr,
            f,
            "available profiles:  default  web  data  desktop  p2p  game  agent  all");

        return;
      }

      if (plan.unsupportedReason == "sdk asset not found")
      {
        sym_line(
            std::cerr,
            f.err(),
            "sdk profile not available for " + f.bold_(plan.version));

        hint_line(std::cerr, f, "profile: " + plan.sdkProfile);
        hint_line(std::cerr, f, "run: vix upgrade --sdk list");
        return;
      }

      sym_line(
          std::cerr,
          f.err(),
          "sdk not available for " + f.bold_(plan.platform.label()));

      hint_line(std::cerr, f, "profile: " + plan.sdkProfile);
    }

    void print_install_header(const UpgradePlan &plan, const Options &opt)
    {
      Fmt f(std::cout);

      const bool isSdk = plan.kind == UpgradeKind::Sdk;

      const std::string label =
          isSdk ? "upgrade --sdk " + plan.sdkProfile : "upgrade";

      std::vector<std::pair<std::string, std::string>> fields;

      if (isSdk)
      {
        fields.push_back({"profile", f.blu(f.bold_(plan.sdkProfile))});
        fields.push_back({"version", f.grn(f.bold_(plan.version))});
      }
      else
      {
        fields.push_back({"current", f.dim_(plan.currentVersion.value_or("unknown"))});

        const std::string targetKey = opt.version.has_value() ? "target" : "latest";
        fields.push_back({targetKey, f.grn(f.bold_(plan.version))});
      }

      fields.push_back({"platform", f.dim_(plan.platform.label())});

      if (opt.verbose || isSdk)
      {
        if (!plan.asset.name.empty())
        {
          std::string assetLine = f.dim_(plan.asset.name);

          if (plan.asset.downloadBytes.has_value())
            assetLine += f.dim_("  (" + human_bytes(*plan.asset.downloadBytes) + ")");

          fields.push_back({"asset", assetLine});
        }

        if (isSdk && !plan.installDir.empty())
          fields.push_back({"install", f.dim_(plan.installDir.string())});
      }

      print_header_box(std::cout, f, label, fields);
    }

    void replace_directory_atomic_best_effort(const fs::path &stage, const fs::path &dest)
    {
      std::error_code ec;
      remove_all_if_exists(dest);
      fs::create_directories(dest.parent_path(), ec);

      fs::rename(stage, dest, ec);
      if (!ec)
        return;

      ec.clear();
      copy_dir_or_throw(stage, dest);
      fs::remove_all(stage, ec);
    }

    void update_current_pointer(const std::string &profile, const fs::path &installDir)
    {
      const fs::path ptr = sdk_current_pointer_path(profile);
      const fs::path fallback = sdk_profile_root(profile) / "current.txt";

      std::error_code ec;
      fs::create_directories(ptr.parent_path(), ec);
      remove_all_if_exists(ptr);

#ifndef _WIN32
      fs::create_directory_symlink(installDir, ptr, ec);
      if (!ec)
        return;
#endif

      std::ofstream out(fallback);
      if (out)
        out << installDir.string() << "\n";
    }

    void install_sdk(const UpgradePlan &plan, const Options &opt, json &result)
    {
      Fmt f(std::cout);
      fs::create_directories(sdk_profile_root(plan.sdkProfile));

      const fs::path tmpDir =
          fs::temp_directory_path() /
          ("vix-sdk-upgrade-" + plan.sdkProfile + "-" + std::to_string(static_cast<long long>(std::time(nullptr))));

      std::error_code ec;
      fs::create_directories(tmpDir, ec);
      ScopedCleanup tmpCleanup{tmpDir};

      const fs::path archive = tmpDir / plan.asset.name;
      const fs::path stage = sdk_profile_root(plan.sdkProfile) /
                             (".installing-" + plan.version + "-" + std::to_string(static_cast<long long>(std::time(nullptr))));
      ScopedCleanup stageCleanup{stage};

      if (!opt.jsonOut)
      {
        const std::string size =
            plan.asset.downloadBytes.has_value()
                ? human_bytes(*plan.asset.downloadBytes)
                : "?";

        styled_step(
            std::cout,
            f,
            "dl",
            plan.asset.name + f.dim_("  (" + size + ")"));
      }

      download_file(plan.asset.url, archive, opt.verbose && !opt.jsonOut);

      verbose_line(opt, "verifying sha256");
      verify_sha256_or_throw(plan.asset.sha256Url, archive, tmpDir, opt.verbose && !opt.jsonOut);

      if (!opt.jsonOut)
        styled_ok(std::cout, f, "sha256 verified");

      const bool minisignOk = try_verify_minisign(
          plan.asset.minisignUrl,
          archive,
          tmpDir,
          kMinisignPubkey,
          opt.verbose && !opt.jsonOut);
      result["minisign_verified"] = minisignOk;
      if (!opt.jsonOut && minisignOk)
        styled_ok(std::cout, f, "minisign verified");
      else if (opt.verbose && !minisignOk)
        verbose_line(opt, "minisign skipped or not available; sha256 verified");

      if (!opt.jsonOut)
      {
        styled_step(
            std::cout,
            f,
            "arrow",
            "Installing to  " + f.dim_(plan.installDir.string()));
      }
      remove_all_if_exists(stage);
      fs::create_directories(stage, ec);
      extract_archive(archive, stage, opt.verbose && !opt.jsonOut);

      replace_directory_atomic_best_effort(stage, plan.installDir);
      stageCleanup.p.clear();

#ifndef _WIN32
      const fs::path sdkVix = plan.installDir / "bin" / "vix";
      if (fs::exists(sdkVix, ec))
        run_quiet_or_verbose("chmod +x " + shell_quote(sdkVix.string()), opt.verbose && !opt.jsonOut);
#endif

      update_current_pointer(plan.sdkProfile, plan.installDir);
      write_sdk_metadata(plan.sdkProfile, plan.version, plan.platform, plan.installDir, plan.asset);
      register_sdk_with_cmake_user_registry(plan.sdkProfile, plan.installDir);

      result["status"] = "ok";
      result["action"] = "upgrade";
      result["installed"] = plan.version;
      result["installed_version"] = plan.version;
      result["metadata"] = (plan.installDir / "install.json").string();
      result["current_metadata"] = sdk_current_metadata_path(plan.sdkProfile).string();
      result["message"] = "done";

      if (!opt.jsonOut)
      {
        styled_done(
            std::cout,
            f,
            "sdk " + plan.sdkProfile + " " + plan.version + " installed");
      }
    }

    void install_cli(const UpgradePlan &plan, const Options &opt, json &result)
    {
      Fmt f(std::cout);
      if (!is_writable_dir(plan.installDir))
        throw std::runtime_error("install directory is not writable: " + plan.installDir.string());

      const fs::path tmpDir =
          fs::temp_directory_path() /
          ("vix-upgrade-" + std::to_string(static_cast<long long>(std::time(nullptr))));

      std::error_code ec;
      fs::create_directories(tmpDir, ec);
      ScopedCleanup cleanup{tmpDir};

      const fs::path archive = tmpDir / plan.asset.name;
      const fs::path extractDir = tmpDir / "extract";

      if (!opt.jsonOut)
      {
        const std::string size =
            plan.asset.downloadBytes.has_value()
                ? human_bytes(*plan.asset.downloadBytes)
                : "?";

        styled_step(
            std::cout,
            f,
            "dl",
            plan.asset.name + f.dim_("  (" + size + ")"));
      }

      download_file(plan.asset.url, archive, opt.verbose && !opt.jsonOut);

      verbose_line(opt, "verifying sha256");
      verify_sha256_or_throw(plan.asset.sha256Url, archive, tmpDir, opt.verbose && !opt.jsonOut);

      if (!opt.jsonOut)
        styled_ok(std::cout, f, "sha256 verified");

      const bool minisignOk = try_verify_minisign(
          plan.asset.minisignUrl,
          archive,
          tmpDir,
          kMinisignPubkey,
          opt.verbose && !opt.jsonOut);
      result["minisign_verified"] = minisignOk;
      if (!opt.jsonOut && minisignOk)
        styled_ok(std::cout, f, "minisign verified");
      else if (opt.verbose && !minisignOk)
        verbose_line(opt, "minisign skipped or not available; sha256 verified");

      if (!opt.jsonOut)
      {
        styled_step(
            std::cout,
            f,
            "arrow",
            "Installing to  " + f.dim_(plan.destination.string()));
      }
      extract_archive(archive, extractDir, opt.verbose && !opt.jsonOut);

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
      run_quiet_or_verbose("chmod +x " + shell_quote(bin.string()), opt.verbose && !opt.jsonOut);
#endif

#ifdef _WIN32
      const fs::path staged = plan.installDir / "vix.exe.new";
      fs::copy_file(bin, staged, fs::copy_options::overwrite_existing, ec);
      if (ec)
        throw std::runtime_error("failed to stage new executable: " + ec.message());

      schedule_windows_replace(staged, plan.destination);
      write_cli_metadata(
          plan.repo,
          plan.version,
          plan.platform.os,
          plan.platform.arch,
          plan.installDir,
          plan.asset.downloadBytes,
          plan.version,
          plan.asset.url);

      result["status"] = "ok";
      result["action"] = "upgrade";
      result["installed"] = plan.version;
      result["message"] = "upgrade scheduled";
      result["staged"] = staged.string();

      if (!opt.jsonOut)
      {
        styled_done(std::cout, f, "vix " + plan.version + " upgrade scheduled");
        hint_line(std::cout, f, "restart your terminal to use the new version");
      }
#else
      const fs::path staged =
          plan.installDir / (binName + std::string(".tmp.") + std::to_string(::getpid()));

      ec.clear();
      fs::copy_file(bin, staged, fs::copy_options::overwrite_existing, ec);
      if (ec)
        throw std::runtime_error("failed to stage new binary: " + ec.message());

      run_quiet_or_verbose("chmod +x " + shell_quote(staged.string()), opt.verbose && !opt.jsonOut);

      ec.clear();
      fs::rename(staged, plan.destination, ec);
      if (ec)
      {
        ec.clear();
        fs::copy_file(staged, plan.destination, fs::copy_options::overwrite_existing, ec);

        std::error_code ec2;
        fs::remove(staged, ec2);
      }

      if (ec)
        throw std::runtime_error("failed to install: " + ec.message());

      const auto newVer = get_installed_version(plan.destination);
      const std::string finalVersion = newVer.has_value() ? *newVer : plan.version;

      write_cli_metadata(
          plan.repo,
          plan.version,
          plan.platform.os,
          plan.platform.arch,
          plan.installDir,
          plan.asset.downloadBytes,
          finalVersion,
          plan.asset.url);

      result["status"] = "ok";
      result["action"] = "upgrade";
      result["installed"] = finalVersion;
      result["installed_version"] = finalVersion;
      result["message"] = "done";

      if (!opt.jsonOut)
        styled_done(std::cout, f, "vix " + finalVersion + " installed");
#endif
    }

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
          error_line(std::cerr, "Missing package after -g.");
          error_line(std::cerr, "Example: vix upgrade -g @gk/jwt");
        }

        return 1;
      }

      if (!registry_present())
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
        else
        {
          error_line(std::cerr, "Registry is not synced.");
          blank_line(std::cerr);
          error_line(std::cerr, "Run: vix registry sync");
        }
        return 1;
      }

      json result;
      result["command"] = "upgrade";
      result["mode"] = "global";
      result["spec"] = *opt.globalSpec;
      result["check"] = opt.check;
      result["dry_run"] = opt.dryRun;
      result["verbose"] = opt.verbose;

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
          {
            info_line(std::cout, "Global package is already up to date.");
            blank_line();
            field_line(std::cout, "Package", dep.id);
            field_line(std::cout, "Version", currentVersion.empty() ? dep.version : currentVersion);
          }

          return 0;
        }

        if (opt.check || opt.dryRun)
        {
          result["status"] = "ok";
          result["action"] = opt.check ? "check" : "dry-run";
          result["message"] = opt.check ? "check mode: no install" : "dry-run: no install";

          if (opt.jsonOut)
          {
            print_json(result);
          }
          else
          {
            info_line(std::cout, opt.check ? "Global package check" : "Global package upgrade");
            blank_line();
            field_line(std::cout, "Package", dep.id);
            field_line(std::cout, "Current", currentVersion.empty() ? "unknown" : currentVersion);
            field_line(std::cout, "Target", dep.version);
            blank_line();
            info_line(std::cout, opt.check ? "No files changed." : "Dry run: no files changed.");
          }

          return 0;
        }

        fs::create_directories(global_pkgs_dir());

        if (!opt.jsonOut)
        {
          info_line(std::cout, "Global package upgrade");
          blank_line();
          field_line(std::cout, "Package", dep.id);
          field_line(std::cout, "Current", currentVersion.empty() ? "unknown" : currentVersion);
          field_line(std::cout, "Target", dep.version);
          blank_line();
        }

        const bool checkoutExistedBefore = fs::exists(dep.checkout);
        if (!checkoutExistedBefore)
        {
          if (!opt.jsonOut)
            step_line(std::cout, "Fetching");
          std::string outDir;
          const int rc = clone_checkout(dep.repo, sanitize_id_dot(dep.id), dep.commit, outDir, opt.verbose && !opt.jsonOut);
          if (rc != 0)
            throw std::runtime_error("fetch failed: " + dep.id);

          dep.checkout = fs::path(outDir);
        }

        dep.hash = vix::cli::util::sha256_directory(dep.checkout).value_or("");
        if (!verify_dependency_hash(dep))
          throw std::runtime_error("integrity check failed: " + dep.id);

        load_dep_manifest(dep);

        if (!opt.jsonOut)
          step_line(std::cout, "Installing");
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
          print_json(result);
        else
          info_line(std::cout, "Done.");

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
          error_line(std::cerr, ex.what());
        }

        return 1;
      }
    }

    struct SdkProfileInfo
    {
      std::string name;
      std::string title;
      std::string description;
      std::vector<std::string> modules;
      std::vector<std::string> linuxDeps;
      std::vector<std::string> macosDeps;
      std::vector<std::string> windowsDeps;
      std::vector<std::string> notes;
    };

    std::string join_strings(const std::vector<std::string> &items, const std::string &sep)
    {
      std::ostringstream oss;

      for (std::size_t i = 0; i < items.size(); ++i)
      {
        if (i > 0)
          oss << sep;
        oss << items[i];
      }

      return oss.str();
    }

    std::string sdk_docs_url(const std::string &profile)
    {
      return "https://vixcpp.com/sdks/" + profile;
    }

    std::optional<SdkProfileInfo> sdk_profile_info(const std::string &profile)
    {
      const std::vector<std::string> baseModules{
          "cli",
          "core",
          "json",
          "error",
          "path",
          "fs",
          "io",
          "env",
          "os",
          "utils",
          "log",
          "async",
          "time",
          "process",
          "threadpool",
          "template",
          "ui",
          "note",
      };

      const std::vector<std::string> baseLinuxDeps{
          "build-essential",
          "cmake",
          "ninja-build",
          "pkg-config",
          "ca-certificates",
          "git",
          "curl",
          "tar",
          "unzip",
          "zip",
          "nlohmann-json3-dev",
          "libssl-dev",
          "zlib1g-dev",
          "libsqlite3-dev",
          "libbrotli-dev",
          "libspdlog-dev",
          "libfmt-dev",
      };

      const std::vector<std::string> baseMacosDeps{
          "xcode-select --install",
          "brew install cmake ninja pkg-config openssl@3 nlohmann-json spdlog fmt",
      };

      const std::vector<std::string> baseWindowsDeps{
          "Visual Studio 2022 Build Tools",
          "CMake",
          "Ninja",
          "Git",
          "WebView2 Runtime",
          "vcpkg install openssl sqlite3 zlib brotli nlohmann-json spdlog fmt --triplet x64-windows",
      };

      auto make = [&](std::string title,
                      std::string description,
                      std::vector<std::string> extraModules,
                      std::vector<std::string> extraLinux,
                      std::vector<std::string> extraMacos,
                      std::vector<std::string> extraWindows,
                      std::vector<std::string> notes) -> SdkProfileInfo
      {
        SdkProfileInfo info;
        info.name = profile;
        info.title = std::move(title);
        info.description = std::move(description);
        info.modules = baseModules;
        info.modules.insert(info.modules.end(), extraModules.begin(), extraModules.end());

        info.linuxDeps = baseLinuxDeps;
        info.linuxDeps.insert(info.linuxDeps.end(), extraLinux.begin(), extraLinux.end());

        info.macosDeps = baseMacosDeps;
        info.macosDeps.insert(info.macosDeps.end(), extraMacos.begin(), extraMacos.end());

        info.windowsDeps = baseWindowsDeps;
        info.windowsDeps.insert(info.windowsDeps.end(), extraWindows.begin(), extraWindows.end());

        info.notes = std::move(notes);
        return info;
      };

      if (profile == "default")
      {
        return make(
            "Default SDK",
            "Balanced SDK for normal Vix.cpp projects and local development.",
            {},
            {},
            {},
            {},
            {
                "Good first choice for most projects.",
            });
      }

      if (profile == "web")
      {
        return make(
            "Web SDK",
            "SDK for HTTP, middleware, WebSocket, validation, crypto, WebRPC and requests.",
            {
                "websocket",
                "middleware",
                "validation",
                "crypto",
                "webrpc",
                "requests",
            },
            {},
            {},
            {},
            {
                "Use this for APIs, realtime apps and backend services.",
            });
      }

      if (profile == "data")
      {
        return make(
            "Data SDK",
            "SDK for database, ORM, key-value storage and cache workflows.",
            {
                "db",
                "orm",
                "kv",
                "cache",
            },
            {
                "libsqlite3-dev",
            },
            {
                "brew install sqlite",
            },
            {
                "vcpkg install sqlite3 --triplet x64-windows",
            },
            {
                "SQLite is the default lightweight local database dependency.",
            });
      }

      if (profile == "desktop")
      {
        return make(
            "Desktop SDK",
            "SDK for desktop apps using the Vix UI desktop shell.",
            {
                "desktop",
                "ui-webview",
            },
            {
                "libgtk-3-dev",
                "libwebkit2gtk-4.1-dev",
                "libgl1-mesa-dev",
            },
            {},
            {
                "WebView2 Runtime",
                "Windows SDK",
            },
            {
                "On Linux, WebKitGTK is required for the desktop WebView backend.",
                "If libwebkit2gtk-4.1-dev is unavailable, try libwebkit2gtk-4.0-dev.",
            });
      }

      if (profile == "p2p")
      {
        return make(
            "P2P SDK",
            "SDK for peer-to-peer networking, crypto and local-first sync systems.",
            {
                "p2p",
                "p2p_http",
                "crypto",
                "cache",
            },
            {},
            {},
            {},
            {
                "Use this for node, discovery, replication and local network workflows.",
            });
      }

      if (profile == "game")
      {
        return make(
            "Game SDK",
            "SDK for game and realtime rendering workflows.",
            {
                "game",
                "sdl",
                "opengl",
            },
            {
                "libsdl2-dev",
                "libsdl2-image-dev",
                "libgl1-mesa-dev",
            },
            {
                "brew install sdl2 sdl2_image",
            },
            {
                "vcpkg install sdl2 sdl2-image --triplet x64-windows",
            },
            {
                "SDL2/OpenGL dependencies are required for native game examples.",
            });
      }

      if (profile == "agent")
      {
        return make(
            "Agent SDK",
            "SDK for AI agent tooling and controlled automation workflows.",
            {
                "agent",
                "cache",
            },
            {},
            {},
            {},
            {
                "Use this for agent-oriented tooling, local state and runtime orchestration.",
            });
      }

      if (profile == "all")
      {
        return make(
            "Full SDK",
            "Complete SDK with web, data, desktop, p2p, game and agent modules.",
            {
                "websocket",
                "middleware",
                "validation",
                "crypto",
                "webrpc",
                "requests",
                "db",
                "orm",
                "kv",
                "cache",
                "p2p",
                "p2p_http",
                "desktop",
                "ui-webview",
                "game",
                "sdl",
                "opengl",
                "agent",
            },
            {
                "libgtk-3-dev",
                "libwebkit2gtk-4.1-dev",
                "libgl1-mesa-dev",
                "libsdl2-dev",
                "libsdl2-image-dev",
                "libmysqlcppconn-dev",
            },
            {
                "brew install sqlite sdl2 sdl2_image mysql-connector-c++",
            },
            {
                "WebView2 Runtime",
                "Windows SDK",
                "vcpkg install sdl2 sdl2-image mysql-connector-cpp --triplet x64-windows",
            },
            {
                "This profile is heavier. Prefer a smaller profile when possible.",
            });
      }

      return std::nullopt;
    }

    std::vector<std::string> sdk_deps_for_platform(const SdkProfileInfo &info, const Platform &platform)
    {
      if (platform.os == "linux")
        return info.linuxDeps;

      if (platform.os == "macos")
        return info.macosDeps;

      if (platform.os == "windows")
        return info.windowsDeps;

      return {};
    }

    void sdk_section(std::ostream &os, const Fmt &f, const std::string &title)
    {
      sym_line(os, f.grn("◆"), f.grn(f.bold_(title)));
    }

    void print_wrapped_words(
        std::ostream &os,
        const Fmt &f,
        const std::vector<std::string> &items,
        std::size_t perLine = 5)
    {
      (void)f;

      if (items.empty())
      {
        os << "     none\n";
        return;
      }

      for (std::size_t i = 0; i < items.size(); ++i)
      {
        if (i % perLine == 0)
          os << "     ";

        os << items[i];

        const bool endLine = ((i + 1) % perLine == 0) || (i + 1 == items.size());
        if (endLine)
          os << "\n";
        else
          os << "  ";
      }
    }

    void print_linux_install_command(
        std::ostream &os,
        const Fmt &f,
        const std::vector<std::string> &deps)
    {
      if (deps.empty())
      {
        os << "     " << f.dim_("No dependency notes for Linux yet.") << "\n";
        return;
      }

      os << "     " << f.amb("sudo apt install") << " \\\n";

      for (std::size_t i = 0; i < deps.size(); ++i)
      {
        os << "       " << deps[i];

        if (i + 1 < deps.size())
          os << " \\";

        os << "\n";
      }
    }

    void print_sdk_info_human(const SdkProfileInfo &info, const Platform &platform)
    {
      Fmt f(std::cout);

      print_header_box(
          std::cout,
          f,
          "upgrade --sdk info " + info.name,
          {
              {"profile", f.blu(f.bold_(info.name))},
              {"platform", f.dim_(platform.label())},
          });

      sym_line(std::cout, f.dot(), info.description);

      blank_line();

      sdk_section(std::cout, f, "Modules");
      print_wrapped_words(std::cout, f, info.modules, 5);

      blank_line();

      const auto deps = sdk_deps_for_platform(info, platform);

      sdk_section(std::cout, f, "System dependencies");

      if (deps.empty())
      {
        std::cout << "     " << f.dim_("No dependency notes for this platform yet.") << "\n";
      }
      else if (platform.os == "linux")
      {
        print_linux_install_command(std::cout, f, deps);
      }
      else
      {
        for (const auto &dep : deps)
          std::cout << "     " << f.amb(dep) << "\n";
      }

      if (!info.notes.empty())
      {
        blank_line();

        sdk_section(std::cout, f, "Notes");

        for (const auto &note : info.notes)
          std::cout << "     " << note << "\n";
      }

      blank_line();

      sym_line(
          std::cout,
          f.grn("→"),
          "install  " + f.amb(f.bold_("vix upgrade --sdk " + info.name)));

      sym_line(
          std::cout,
          f.blu("↗"),
          "docs     " + f.grn(sdk_docs_url(info.name)));
    }

    int run_sdk_list(const Options &opt)
    {
      json result;

      try
      {
        const Platform platform = detect_platform();
        const std::string repoStr = repo();
        const std::string version =
            opt.version.has_value() ? *opt.version : resolve_latest_tag_github_api(repoStr);

        result["command"] = "upgrade";
        result["mode"] = "sdk";
        result["action"] = "list";
        result["version"] = version;
        result["repo"] = repoStr;
        result["platform"] = platform.label();
        result["os"] = platform.os;
        result["arch"] = platform.arch;
        result["profiles"] = json::array();
        result["legacy_sdk"] = nullptr;

        std::vector<std::pair<std::string, std::string>> available;

        if (platform.os == "linux" && platform.arch == "x86_64")
        {
          for (const char *profile : kSdkProfiles)
          {
            const std::string p = profile;
            const ReleaseAsset asset = github_release_asset(
                repoStr,
                version,
                sdk_asset_name(p, platform));

            const bool exists = remote_url_exists(asset.url);

            result["profiles"].push_back({
                {"name", p},
                {"asset", asset.name},
                {"available", exists},
            });

            if (exists)
              available.push_back({p, asset.name});
          }
        }

        const ReleaseAsset legacyAsset = github_release_asset(
            repoStr,
            version,
            legacy_sdk_asset_name(platform));

        const bool legacyExists = remote_url_exists(legacyAsset.url);

        if (legacyExists)
        {
          result["legacy_sdk"] = {
              {"asset", legacyAsset.name},
              {"available", true},
          };
        }

        result["available_count"] = available.size();
        result["status"] = "ok";

        if (opt.jsonOut)
        {
          print_json(result);
          return 0;
        }

        Fmt f(std::cout);

        print_header_box(
            std::cout,
            f,
            "upgrade --sdk list",
            {
                {"version", f.grn(f.bold_(version))},
                {"platform", f.dim_(platform.label())},
            });

        if (!available.empty())
        {
          for (const auto &[profile, assetName] : available)
          {
            sym_line(
                std::cout,
                f.ok(),
                f.blu(f.bold_(profile)) + f.dim_("  " + assetName));
          }

          hint_line(std::cout, f, "install: vix upgrade --sdk <profile>");
          return 0;
        }

        sym_line(std::cout, f.dot(), f.dim_("no profiled SDK assets found"));

        if (legacyExists)
          hint_line(std::cout, f, "legacy sdk: " + legacyAsset.name);

        hint_line(std::cout, f, "try again with v2.7.0 or newer");
        return 0;
      }
      catch (const std::exception &ex)
      {
        if (opt.jsonOut)
        {
          print_json({
              {"command", "upgrade"},
              {"mode", "sdk"},
              {"action", "list"},
              {"status", "error"},
              {"message", ex.what()},
          });
        }
        else
        {
          Fmt f(std::cerr);
          styled_error(std::cerr, f, "Unable to list Vix SDK profiles.", "reason: " + std::string(ex.what()));
        }

        return 1;
      }
    }

    int run_sdk_info(const Options &opt)
    {
      const Platform platform = detect_platform();
      const std::string profile =
          opt.sdkProfile.empty() ? "default" : to_lower(opt.sdkProfile);

      json result;
      result["command"] = "upgrade";
      result["mode"] = "sdk";
      result["action"] = "info";
      result["profile"] = profile;
      result["platform"] = platform.label();
      result["os"] = platform.os;
      result["arch"] = platform.arch;

      try
      {
        if (!is_supported_sdk_profile(profile))
        {
          result["status"] = "error";
          result["message"] = "unknown sdk profile";

          if (opt.jsonOut)
          {
            print_json(result);
          }
          else
          {
            UpgradePlan plan;
            plan.kind = UpgradeKind::Sdk;
            plan.platform = platform;
            plan.sdkProfile = profile;
            plan.supported = false;
            plan.unsupportedReason = "unknown sdk profile";
            print_unsupported_sdk(plan);
          }

          return 1;
        }

        const auto infoOpt = sdk_profile_info(profile);

        if (!infoOpt.has_value())
          throw std::runtime_error("missing sdk profile metadata");

        const SdkProfileInfo info = *infoOpt;
        const auto deps = sdk_deps_for_platform(info, platform);

        result["status"] = "ok";
        result["title"] = info.title;
        result["description"] = info.description;
        result["modules"] = info.modules;
        result["system_dependencies"] = deps;
        result["notes"] = info.notes;
        result["docs"] = sdk_docs_url(profile);

        if (opt.jsonOut)
          print_json(result);
        else
          print_sdk_info_human(info, platform);

        return 0;
      }
      catch (const std::exception &ex)
      {
        result["status"] = "error";
        result["message"] = ex.what();

        if (opt.jsonOut)
        {
          print_json(result);
        }
        else
        {
          Fmt f(std::cerr);
          styled_error(
              std::cerr,
              f,
              "Unable to show SDK info.",
              "reason: " + std::string(ex.what()));
        }

        return 1;
      }
    }

    int run_sdk_upgrade_many(const Options &opt)
    {
      if (opt.sdkProfiles.size() <= 1)
        return -1;

      json result;
      result["command"] = "upgrade";
      result["mode"] = "sdk";
      result["action"] = opt.check ? "check" : opt.dryRun ? "dry-run"
                                                          : "upgrade";
      result["profiles"] = json::array();

      int rc = 0;

      Platform platform = detect_platform();
      const std::string repoStr = repo();

      std::string version;

      try
      {
        version = opt.version.has_value() ? *opt.version : resolve_latest_tag_github_api(repoStr);
      }
      catch (const std::exception &ex)
      {
        result["status"] = "error";
        result["message"] = ex.what();

        if (opt.jsonOut)
        {
          print_json(result);
        }
        else
        {
          Fmt f(std::cerr);
          styled_error(
              std::cerr,
              f,
              "Unable to resolve SDK version.",
              "reason: " + std::string(ex.what()));
        }

        return 1;
      }

      result["repo"] = repoStr;
      result["version"] = version;
      result["platform"] = platform.label();
      result["os"] = platform.os;
      result["arch"] = platform.arch;

      Fmt f(std::cout);

      if (!opt.jsonOut)
      {
        const std::string targetKey = opt.version.has_value() ? "target" : "latest";

        print_header_box(
            std::cout,
            f,
            "upgrade --sdk",
            {
                {"profiles", f.blu(f.bold_(join_strings(opt.sdkProfiles, " ")))},
                {targetKey, f.grn(f.bold_(version))},
                {"platform", f.dim_(platform.label())},
            });
      }

      auto short_sdk_reason = [&](const UpgradePlan &plan) -> std::string
      {
        if (plan.unsupportedReason == "unknown sdk profile")
          return "unknown profile";

        if (plan.unsupportedReason == "sdk asset not found")
          return "not available for " + plan.version;

        if (plan.unsupportedReason == "sdk asset not available for platform")
          return "not available for " + plan.platform.label();

        if (!plan.unsupportedReason.empty())
          return plan.unsupportedReason;

        return "not available";
      };

      for (const auto &profile : opt.sdkProfiles)
      {
        Options one = opt;
        one.sdkProfile = profile;
        one.sdkProfiles.clear();
        one.version = version;

        try
        {
          UpgradePlan plan = make_sdk_plan(one);
          json item = plan_to_json(plan, one);
          item["profile"] = profile;

          if (!plan.supported)
          {
            item["status"] = "error";
            item["message"] = plan.unsupportedReason;
            rc = 1;

            if (!opt.jsonOut)
            {
              sym_line(
                  std::cout,
                  f.err(),
                  f.blu(f.bold_(profile)) + f.dim_(" — ") + short_sdk_reason(plan));
            }
          }
          else if (plan.currentVersion.has_value() && *plan.currentVersion == plan.version && !one.check && !one.dryRun)
          {
            item["status"] = "ok";
            item["action"] = "noop";
            item["installed"] = *plan.currentVersion;
            item["message"] = "already installed";

            if (!opt.jsonOut)
            {
              sym_line(
                  std::cout,
                  f.ok(),
                  f.blu(f.bold_(profile)) + f.dim_(" — already up to date (" + plan.version + ")"));
            }
          }
          else if (one.check || one.dryRun)
          {
            item["status"] = "ok";
            item["action"] = one.check ? "check" : "dry-run";
            item["message"] = one.check ? "check mode: no download, no install" : "dry-run: no download, no install";

            if (!opt.jsonOut)
            {
              sym_line(
                  std::cout,
                  f.dot(),
                  f.blu(f.bold_(profile)) + f.dim_(one.check ? " — no files changed" : " — dry run, no files changed"));
            }
          }
          else
          {
            if (!opt.jsonOut)
            {
              sym_line(
                  std::cout,
                  f.dot(),
                  f.blu(f.bold_(profile)));
            }

            install_sdk(plan, one, item);
          }

          result["profiles"].push_back(item);
        }
        catch (const std::exception &ex)
        {
          rc = 1;

          result["profiles"].push_back({
              {"profile", profile},
              {"status", "error"},
              {"message", ex.what()},
          });

          if (!opt.jsonOut)
          {
            sym_line(
                std::cout,
                f.err(),
                f.blu(f.bold_(profile)) + f.dim_(" — ") + std::string(ex.what()));
          }
        }
      }

      result["status"] = rc == 0 ? "ok" : "error";

      if (opt.jsonOut)
        print_json(result);

      return rc;
    }

    int run_sdk_upgrade(const Options &opt)
    {
      json result;

      if (opt.sdkList)
        return run_sdk_list(opt);

      if (opt.sdkInfo)
        return run_sdk_info(opt);

      if (opt.sdkProfiles.size() > 1)
        return run_sdk_upgrade_many(opt);

      try
      {
        UpgradePlan plan = make_sdk_plan(opt);
        result = plan_to_json(plan, opt);

        if (!plan.supported)
        {
          result["status"] = "error";
          result["message"] = plan.unsupportedReason;

          if (opt.jsonOut)
            print_json(result);
          else
            print_unsupported_sdk(plan);

          return 1;
        }

        if (plan.currentVersion.has_value() && *plan.currentVersion == plan.version && !opt.check && !opt.dryRun)
        {
          write_sdk_metadata(plan.sdkProfile, plan.version, plan.platform, plan.installDir, plan.asset);
          result["status"] = "ok";
          result["action"] = "noop";
          result["installed"] = *plan.currentVersion;
          result["message"] = "already installed";

          if (opt.jsonOut)
            print_json(result);
          else
            print_already_up_to_date(plan);

          return 0;
        }

        if (opt.check || opt.dryRun)
        {
          result["status"] = "ok";
          result["action"] = opt.check ? "check" : "dry-run";
          result["message"] = opt.check ? "check mode: no download, no install" : "dry-run: no download, no install";

          if (opt.jsonOut)
            print_json(result);
          else
            print_human_check_or_dry_run(plan, opt);

          return 0;
        }

        if (opt.jsonOut)
        {
          install_sdk(plan, opt, result);
          print_json(result);
        }
        else
        {
          print_install_header(plan, opt);
          install_sdk(plan, opt, result);
        }

        return 0;
      }
      catch (const std::exception &ex)
      {
        result["command"] = "upgrade";
        result["mode"] = "sdk";
        result["profile"] = opt.sdkProfile;
        result["check"] = opt.check;
        result["dry_run"] = opt.dryRun;
        result["status"] = "error";
        result["message"] = ex.what();

        if (opt.jsonOut)
        {
          print_json(result);
        }
        else
        {
          Fmt f(std::cerr);

          styled_error(
              std::cerr,
              f,
              "Unable to download or install Vix SDK.",
              "reason: " + std::string(ex.what()));

          print_kv(std::cerr, f, "profile", opt.sdkProfile);

          if (opt.version.has_value())
            print_kv(std::cerr, f, "version", *opt.version);

          blank_line(std::cerr);
        }

        return 1;
      }
    }

    int run_cli_upgrade(const Options &opt)
    {
      json result;

      try
      {
        UpgradePlan plan = make_cli_plan(opt);
        result = plan_to_json(plan, opt);

        if (!plan.supported)
        {
          result["status"] = "error";
          result["message"] = plan.unsupportedReason;

          if (opt.jsonOut)
            print_json(result);
          else
            error_line(std::cerr, "Unable to upgrade Vix: " + plan.unsupportedReason);

          return 1;
        }

        if (plan.currentVersion.has_value() && *plan.currentVersion == plan.version && !opt.check && !opt.dryRun)
        {
          write_cli_metadata(
              plan.repo,
              plan.version,
              plan.platform.os,
              plan.platform.arch,
              plan.installDir,
              std::nullopt,
              *plan.currentVersion,
              "");

          result["status"] = "ok";
          result["action"] = "noop";
          result["installed"] = *plan.currentVersion;
          result["message"] = "already installed";

          if (opt.jsonOut)
            print_json(result);
          else
            print_already_up_to_date(plan);

          return 0;
        }

        if (opt.check || opt.dryRun)
        {
          result["status"] = "ok";
          result["action"] = opt.check ? "check" : "dry-run";
          result["message"] = opt.check ? "check mode: no download, no install" : "dry-run: no download, no install";

          if (opt.jsonOut)
            print_json(result);
          else
            print_human_check_or_dry_run(plan, opt);

          return 0;
        }

        if (opt.jsonOut)
        {
          install_cli(plan, opt, result);
          print_json(result);
        }
        else
        {
          print_install_header(plan, opt);
          install_cli(plan, opt, result);
        }

        return 0;
      }
      catch (const std::exception &ex)
      {
        result["command"] = "upgrade";
        result["mode"] = "cli";
        result["check"] = opt.check;
        result["dry_run"] = opt.dryRun;
        result["status"] = "error";
        result["message"] = ex.what();

        if (opt.jsonOut)
        {
          print_json(result);
        }
        else
        {
          Fmt f(std::cerr);

          std::string hint = "reason: " + std::string(ex.what());

          if (std::string(ex.what()).find("permission") != std::string::npos ||
              std::string(ex.what()).find("writable") != std::string::npos)
          {
            hint += " — try running the command with the correct permissions";
          }

          styled_error(std::cerr, f, "Unable to install Vix.", hint);
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
    const bool jsonRequested = wants_json(args);

    try
    {
      opt = parse_options(args);
    }
    catch (const std::exception &ex)
    {
      if (jsonRequested)
      {
        print_json({
            {"command", "upgrade"},
            {"status", "error"},
            {"message", ex.what()},
        });
      }
      else
      {
        error_line(std::cerr, ex.what());
        error_line(std::cerr, "Tip: vix upgrade --help");
      }
      return 1;
    }

    if (opt.globalMode)
      return run_global_upgrade(opt);

    if (opt.sdkMode)
      return run_sdk_upgrade(opt);

    return run_cli_upgrade(opt);
  }

  int UpgradeCommand::help()
  {
    std::cout
        << "vix upgrade\n"
        << "Upgrade the Vix CLI, install or upgrade a Vix SDK profile, or upgrade one globally installed package.\n\n"

        << "Usage\n"
        << "  vix upgrade\n"
        << "  vix upgrade vX.Y.Z\n"
        << "  vix upgrade --version vX.Y.Z\n"
        << "  vix upgrade --check\n"
        << "  vix upgrade --dry-run\n"
        << "  vix upgrade --json\n"
        << "  vix upgrade --sdk [profile]\n"
        << "  vix upgrade --sdk list\n"
        << "  vix upgrade --sdk info [profile]\n"
        << "  vix upgrade --sdk [profile] --version vX.Y.Z\n"
        << "  vix upgrade --sdk-info <profile>\n"
        << "  vix upgrade --sdk <profile...>\n"
        << "  vix upgrade --sdk web data desktop\n"
        << "  vix upgrade -g [@]namespace/name[@version]\n\n"

        << "SDK profiles\n"
        << "  default    Balanced SDK for normal Vix.cpp projects\n"
        << "  web        HTTP, middleware, WebSocket, validation, crypto, WebRPC and requests\n"
        << "  data       Database, ORM, KV and cache workflows\n"
        << "  desktop    Desktop apps with the Vix UI desktop shell\n"
        << "  p2p        Peer-to-peer networking and local-first systems\n"
        << "  game       Game and realtime rendering workflows\n"
        << "  agent      AI agent tooling and controlled automation workflows\n"
        << "  all        Full SDK with all available profiles\n\n"

        << "Examples\n"
        << "  vix upgrade\n"
        << "  vix upgrade v2.7.0\n"
        << "  vix upgrade --check\n"
        << "  vix upgrade --dry-run\n"
        << "  vix upgrade --json\n"
        << "  vix upgrade --sdk\n"
        << "  vix upgrade --sdk list\n"
        << "  vix upgrade --sdk info web\n"
        << "  vix upgrade --sdk web\n"
        << "  vix upgrade --sdk web --info\n"
        << "  vix upgrade --sdk all --version v2.7.0\n"
        << "  vix upgrade --sdk-info desktop\n"
        << "  vix upgrade --sdk web data\n"
        << "  vix upgrade --sdk web,data,desktop\n"
        << "  vix upgrade -g gk/jwt\n"
        << "  vix upgrade -g gk/jwt@1.0.0\n"
        << "  vix upgrade -g @gk/jwt\n\n"

        << "Options\n"
        << "  -g, --global        Upgrade a globally installed package\n"
        << "  --sdk [profile]     Install or upgrade a Vix SDK profile (default: default)\n"
        << "  --sdk list          List SDK profiles available in the current release\n"
        << "  --sdk info [name]   Show modules, system dependencies and docs for an SDK profile\n"
        << "  --sdk-info <name>   Shortcut for: vix upgrade --sdk info <name>\n"
        << "  --version <tag>     Use a specific GitHub release tag\n"
        << "  --check             Check target version without installing\n"
        << "  --dry-run           Simulate without changing files\n"
        << "  --json              Print machine-readable JSON output only\n"
        << "  --verbose           Print diagnostic details\n\n"

        << "Environment\n"
        << "  VIX_REPO            Override repo for CLI/SDK upgrades (default: vixcpp/vix)\n"
        << "  VIX_CLI_PATH        Override current Vix binary path detection\n\n"

        << "Notes\n"
        << "  • CLI and SDK upgrades use GitHub Releases\n"
        << "  • SDK profiles install into ~/.vix/sdk/<profile>/<version>/\n"
        << "  • Use `vix upgrade --sdk list` to see SDK assets available in the current release\n"
        << "  • Use `vix upgrade --sdk info <profile>` before installing a SDK profile\n"
        << "  • SDK info shows modules, required system dependencies and documentation links\n"
        << "  • Global package upgrades use the registry + ~/.vix/global/installed.json\n"
        << "  • On Unix, minisign is verified if available; sha256 is always required\n"
        << "  • Multiple SDK profiles can be installed at once: vix upgrade --sdk web data\n"
        << "  • The `all` profile is a complete SDK profile, not a batch alias\n"
        << "  • SDK docs: https://vixcpp.com/sdks/\n";

    return 0;
  }

} // namespace vix::commands
