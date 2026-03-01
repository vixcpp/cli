/**
 *
 *  @file RunFlow.cpp
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
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/utils/Env.hpp>
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <cctype>
#include <fstream>
#include <cstring>

#ifndef _WIN32
#include <sys/wait.h>
#include <chrono>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
  {
    auto is_opt = [](std::string_view s)
    { return !s.empty() && s.front() == '-'; };

    for (size_t i = 0; i < args.size(); ++i)
    {
      const auto &a = args[i];

      if (a == "-d" || a == "--dir")
      {
        if (i + 1 < args.size() && !is_opt(args[i + 1]))
          return args[i + 1];
        return std::nullopt;
      }

      constexpr const char pfx[] = "--dir=";
      if (a.rfind(pfx, 0) == 0)
      {
        std::string v = a.substr(sizeof(pfx) - 1);
        if (v.empty())
          return std::nullopt;
        return v;
      }
    }
    return std::nullopt;
  }

  static std::string lower(std::string s)
  {
    for (auto &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  std::filesystem::path manifest_entry_cpp(const std::filesystem::path &manifestFile)
  {
    namespace fs = std::filesystem;

    const fs::path root = manifestFile.parent_path();

    auto abs_if_exists = [&](fs::path p) -> std::optional<fs::path>
    {
      std::error_code ec;
      if (p.is_relative())
        p = root / p;
      p = fs::weakly_canonical(p, ec);
      if (ec)
        p = fs::absolute(p);

      if (fs::exists(p, ec) && !ec)
        return p;

      return std::nullopt;
    };

    auto trim = [](std::string s) -> std::string
    {
      auto is_space = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && is_space((unsigned char)s.front()))
        s.erase(s.begin());
      while (!s.empty() && is_space((unsigned char)s.back()))
        s.pop_back();
      return s;
    };

    auto strip_quotes = [&](std::string s) -> std::string
    {
      s = trim(std::move(s));
      if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
      return s;
    };

    // Better fallbacks for typical layouts
    const fs::path fallback1 = root / "main.cpp";
    const fs::path fallback2 = root / "src" / "main.cpp";

    // If manifest missing, best-effort fallback
    {
      std::error_code ec;
      if (!fs::exists(manifestFile, ec) || ec)
      {
        if (auto p = abs_if_exists(fallback2))
          return *p;
        if (auto p = abs_if_exists(fallback1))
          return *p;
        return fs::absolute(fallback2); // last resort (even if missing)
      }
    }

    // Parse minimal: find `entry = "..."` anywhere (ignore comments, spaces)
    std::ifstream in(manifestFile.string());
    std::string line;

    while (std::getline(in, line))
    {
      line = trim(line);
      if (line.empty() || line[0] == '#')
        continue;

      // drop inline comment: foo = "bar" # comment
      if (auto hash = line.find('#'); hash != std::string::npos)
        line = trim(line.substr(0, hash));

      // accept: entry = "main.cpp" or entry=main.cpp
      // also accept: entry : "main.cpp" (nice-to-have)
      if (line.rfind("entry", 0) != 0)
        continue;

      auto pos_eq = line.find('=');
      auto pos_cl = line.find(':');
      auto pos = (pos_eq == std::string::npos) ? pos_cl : pos_eq;
      if (pos == std::string::npos)
        continue;

      std::string rhs = strip_quotes(line.substr(pos + 1));
      if (rhs.empty())
        continue;

      if (auto p = abs_if_exists(fs::path(rhs)))
        return *p;
    }

    // Default fallbacks
    if (auto p = abs_if_exists(fallback2))
      return *p;
    if (auto p = abs_if_exists(fallback1))
      return *p;

    // last resort: keep consistent output
    return fs::absolute(fallback2);
  }

  Options parse(const std::vector<std::string> &args)
  {
    Options o;

    for (size_t i = 0; i < args.size(); ++i)
    {
      const auto &a = args[i];

      auto is_known_vix_flag = [&](const std::string &v) -> bool
      {
        // flags Vix qui n'ont rien à faire après `--`
        return v == "--verbose" || v == "--quiet" || v == "--watch" || v == "--reload" ||
               v == "--force-server" || v == "--force-script" ||
               v == "--san" || v == "--ubsan" ||
               v == "--docs" || v == "--no-docs" ||
               v == "--no-color" ||
               v == "--preset" || v.rfind("--preset=", 0) == 0 ||
               v == "--run-preset" || v.rfind("--run-preset=", 0) == 0 ||
               v == "--cwd" || v.rfind("--cwd=", 0) == 0 ||
               v == "--env" || v.rfind("--env=", 0) == 0 ||
               v == "--args" || v.rfind("--args=", 0) == 0 ||
               v == "--log-level" || v.rfind("--log-level=", 0) == 0 ||
               v == "--log-format" || v.rfind("--log-format=", 0) == 0 ||
               v == "--log-color" || v.rfind("--log-color=", 0) == 0 ||
               v == "--clear" || v.rfind("--clear=", 0) == 0;
      };

      if (a == "--")
      {
        o.hasDoubleDash = true;

        for (size_t j = i + 1; j < args.size(); ++j)
        {
          const std::string v = args[j];
          if (v == "--")
            continue;

          if (!o.warnedVixFlagAfterDoubleDash && is_known_vix_flag(v))
          {
            o.warnedVixFlagAfterDoubleDash = true;
            o.warnedArg = v;
          }

          o.doubleDashArgs.push_back(v);
        }

        break;
      }

      if (a == "--preset" && i + 1 < args.size())
      {
        o.preset = args[++i];
      }
      else if (a == "--run-preset" && i + 1 < args.size())
      {
        o.runPreset = args[++i];
      }
      else if ((a == "-j" || a == "--jobs") && i + 1 < args.size())
      {
        try
        {
          o.jobs = std::stoi(args[++i]);
        }
        catch (...)
        {
          o.jobs = 0;
        }
      }
      else if (a == "--quiet" || a == "-q")
      {
        o.quiet = true;
      }
      else if (a == "--verbose")
      {
        o.verbose = true;
      }
      else if ((a == "--log-level" || a == "--loglevel") && i + 1 < args.size())
      {
        o.logLevel = args[++i];
      }
      else if (a.rfind("--log-level=", 0) == 0)
      {
        o.logLevel = a.substr(std::string("--log-level=").size());
      }
      else if (a == "--log-format" && i + 1 < args.size())
      {
        o.logFormat = args[++i];
      }
      else if (a.rfind("--log-format=", 0) == 0)
      {
        o.logFormat = a.substr(std::string("--log-format=").size());
      }
      else if (a == "--log-color" && i + 1 < args.size())
      {
        o.logColor = args[++i]; // auto|always|never
      }
      else if (a.rfind("--log-color=", 0) == 0)
      {
        o.logColor = a.substr(std::string("--log-color=").size());
      }
      else if (a == "--no-color")
      {
        o.noColor = true;
      }

      else if (a == "--watch" || a == "--reload")
      {
        o.watch = true;
      }
      else if (a == "--force-server")
      {
        o.forceServerLike = true;
      }
      else if (a == "--force-script")
      {
        o.forceScriptLike = true;
      }
      else if (a == "--docs")
      {
        o.docs = true;
      }
      else if (a == "--no-docs")
      {
        o.docs = false;
      }
      else if (a.rfind("--docs=", 0) == 0)
      {
        std::string v = lower(a.substr(std::string("--docs=").size()));
        if (v == "1" || v == "true" || v == "yes" || v == "on")
          o.docs = true;
        else if (v == "0" || v == "false" || v == "no" || v == "off")
          o.docs = false;
        else
          hint("Invalid value for --docs. Use 0|1|true|false.");
      }
      else if (a == "--cwd" && i + 1 < args.size())
      {
        std::filesystem::path p = args[++i];
        if (p.is_relative())
          p = std::filesystem::absolute(p);
        o.cwd = p.string();
      }
      else if (a.rfind("--cwd=", 0) == 0)
      {
        std::filesystem::path p = a.substr(std::string("--cwd=").size());
        if (p.is_relative())
          p = std::filesystem::absolute(p);
        o.cwd = p.string();
      }
      // --env K=V (repeatable)
      else if (a == "--env" && i + 1 < args.size())
      {
        o.runEnv.push_back(args[++i]);
      }
      else if (a.rfind("--env=", 0) == 0)
      {
        o.runEnv.push_back(a.substr(std::string("--env=").size()));
      }
      // --args value (repeatable)
      // Exemple: --args --port --args 8080
      else if (a == "--args" && i + 1 < args.size())
      {
        o.runArgs.push_back(args[++i]);
      }
      else if (a.rfind("--args=", 0) == 0)
      {
        o.runArgs.push_back(a.substr(std::string("--args=").size()));
      }
      else if (a == "--san")
      {
        o.enableSanitizers = true;
        o.enableUbsanOnly = false;
      }
      else if (a == "--ubsan")
      {
        o.enableUbsanOnly = true;
        o.enableSanitizers = false;
      }
      else if (a == "--auto-deps")
      {
        o.autoDeps = AutoDepsMode::Local;
      }
      else if (a.rfind("--auto-deps=", 0) == 0)
      {
        const std::string v = a.substr(std::string("--auto-deps=").size());

        if (v == "up")
        {
          o.autoDeps = AutoDepsMode::Up;
        }
        else if (v == "local")
        {
          o.autoDeps = AutoDepsMode::Local;
        }
        else
        {
          error("Invalid value for --auto-deps: " + v);
          hint("Valid values: local, up");

          o.parseFailed = true;
          o.parseExitCode = 2;
          return o;
        }
      }
      else if (a == "--clear" && i + 1 < args.size())
      {
        o.clearMode = args[++i];
      }
      else if (a.rfind("--clear=", 0) == 0)
      {
        o.clearMode = a.substr(std::string("--clear=").size());
      }
      else if (a == "--no-clear")
      {
        o.clearMode = "never";
      }

      else if (!a.empty() && a[0] != '-')
      {
        if (o.appName.empty())
        {
          o.appName = a;

          std::filesystem::path p{a};
          if (p.extension() == ".cpp")
          {
            o.singleCpp = true;
            o.cppFile = std::filesystem::absolute(p);
          }
        }
        else if (o.appName == "example" && o.exampleName.empty())
        {
          o.exampleName = a;
        }

        std::filesystem::path p{a};

        if (p.extension() == ".vix")
        {
          o.manifestMode = true;
          o.manifestFile = std::filesystem::absolute(p);

          // On ne force pas singleCpp ici
          // Le manifest va décider project/script
        }
        else if (p.extension() == ".cpp")
        {
          o.singleCpp = true;
          o.cppFile = std::filesystem::absolute(p);
        }
      }
    }

    if (auto d = pick_dir_opt_local(args))
      o.dir = *d;

    if (o.forceServerLike && o.forceScriptLike)
    {
      hint("Both --force-server and --force-script were provided; "
           "preferring --force-server.");
      o.forceScriptLike = false;
    }

    // normalize clearMode
    for (auto &c : o.clearMode)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (o.clearMode != "auto" && o.clearMode != "always" && o.clearMode != "never")
    {
      hint("Invalid value for --clear. Using 'auto'. Valid: auto|always|never.");
      o.clearMode = "auto";
    }

    return o;
  }

  void handle_runtime_exit_code(
      int code,
      const std::string &context,
      bool alreadyHandled)
  {
    if (code == 0)
      return;

    if (code == 130)
    {
      hint("ℹ Server interrupted by user (SIGINT).");
      return;
    }

    if (alreadyHandled)
      return;

    error(context + " (exit code " + std::to_string(code) + ").");
  }

  std::string quote(const std::string &s)
  {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    if (s.find_first_of(" \t\"'\\$`") != std::string::npos)
      return "'" + s + "'";
    return s;
#endif
  }

  // Build log analysis
#ifndef _WIN32
  bool has_real_build_work(const std::string &log)
  {
    if (log.find("Building") != std::string::npos)
      return true;
    if (log.find("Linking") != std::string::npos)
      return true;
    if (log.find("Compiling") != std::string::npos)
      return true;
    if (log.find("Scanning dependencies") != std::string::npos)
      return true;

    if (log.find("no work to do") != std::string::npos)
      return false;

    bool hasBuiltTarget = log.find("Built target") != std::string::npos;
    if (hasBuiltTarget)
    {
      return false;
    }

    return true;
  }

  static bool ends_with_2to1(const std::string &s)
  {
    // Simple check: ignore trailing spaces
    std::size_t end = s.find_last_not_of(" \t\r\n");
    if (end == std::string::npos)
      return false;

    // Look for "2>&1" at end
    const std::string needle = "2>&1";
    if (end + 1 < needle.size())
      return false;

    std::size_t start = end + 1 - needle.size();
    return s.compare(start, needle.size(), needle) == 0;
  }

  static int normalize_exit_status(int status)
  {
#if defined(_WIN32)
    // On Windows, _pclose typically returns the process exit code.
    return status;
#else
    if (status == -1)
      return -1;

    if (WIFEXITED(status))
      return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
      return 128 + WTERMSIG(status); // common convention

    return status;
#endif
  }

  std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
  {
    std::string out;

    std::string captureCmd = cmd;
    if (!ends_with_2to1(captureCmd))
      captureCmd += " 2>&1";

#if defined(_WIN32)
    FILE *p = _popen(captureCmd.c_str(), "r");
#else
    FILE *p = popen(captureCmd.c_str(), "r");
#endif

    if (!p)
    {
      exitCode = -1;
      return out;
    }

    char buf[4096];
    while (fgets(buf, sizeof(buf), p))
      out.append(buf);

#if defined(_WIN32)
    int status = _pclose(p);
#else
    int status = pclose(p);
#endif

    exitCode = normalize_exit_status(status);
    return out;
  }

  std::string run_and_capture(const std::string &cmd)
  {
    int code = 0;
    return run_and_capture_with_code(cmd, code);
  }
#else
  bool has_real_build_work(const std::string &)
  {
    return true;
  }
  std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
  {
    (void)cmd;
    exitCode = 0;
    return {};
  }
  std::string run_and_capture(const std::string &)
  {
    return {};
  }
#endif

  bool has_presets(const fs::path &projectDir)
  {
    std::error_code ec;
    return fs::exists(projectDir / "CMakePresets.json", ec) ||
           fs::exists(projectDir / "CMakeUserPresets.json", ec);
  }

  std::optional<fs::path> preset_binary_dir(const fs::path &projectDir,
                                            const std::string &configurePreset)
  {
#ifdef _WIN32
    (void)projectDir;
    (void)configurePreset;
    return std::nullopt;
#else
    std::error_code ec;

    const fs::path presetsPath = projectDir / "CMakePresets.json";
    if (!fs::exists(presetsPath, ec) || ec)
      return std::nullopt;

    std::ifstream in(presetsPath.string());
    if (!in)
      return std::nullopt;

    std::string json((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    const std::string nameKey = "\"name\"";
    const std::string binKey = "\"binaryDir\"";
    const std::string targetName = "\"" + configurePreset + "\"";

    std::size_t pos = 0;
    while (true)
    {
      pos = json.find(nameKey, pos);
      if (pos == std::string::npos)
        break;

      const std::size_t namePos = json.find(targetName, pos);
      if (namePos == std::string::npos)
        break;

      const std::size_t objStart = json.rfind('{', namePos);
      const std::size_t objEnd = json.find('}', namePos);
      if (objStart == std::string::npos || objEnd == std::string::npos || objEnd <= objStart)
      {
        pos = namePos + 1;
        continue;
      }

      const std::string obj = json.substr(objStart, objEnd - objStart + 1);

      const std::size_t b = obj.find(binKey);
      if (b == std::string::npos)
        return std::nullopt;

      std::size_t colon = obj.find(':', b + binKey.size());
      if (colon == std::string::npos)
        return std::nullopt;

      std::size_t q1 = obj.find('"', colon);
      if (q1 == std::string::npos)
        return std::nullopt;

      std::size_t q2 = obj.find('"', q1 + 1);
      if (q2 == std::string::npos || q2 <= q1 + 1)
        return std::nullopt;

      std::string val = obj.substr(q1 + 1, q2 - (q1 + 1));
      if (val.empty())
        return std::nullopt;

      fs::path p = fs::path(val);
      if (p.is_relative())
        p = projectDir / p;

      p = fs::weakly_canonical(p, ec);
      if (ec)
        p = fs::absolute(p);

      return p;
    }

    return std::nullopt;
#endif
  }

  fs::path resolve_build_dir_smart(const fs::path &projectDir,
                                   const std::string &configurePreset)
  {
    fs::path buildDir = projectDir / "build";

    if (auto binDir = preset_binary_dir(projectDir, configurePreset))
      return *binDir;

    fs::path p = projectDir / ("build-" + configurePreset);
    if (fs::exists(p))
      return p;

    if (configurePreset.rfind("dev-", 0) == 0)
    {
      fs::path p2 = projectDir / ("build-" + configurePreset.substr(4));
      if (fs::exists(p2))
        return p2;
    }

    return buildDir;
  }

  static std::vector<std::string> list_presets(const fs::path &dir, const std::string &kind)
  {
#ifdef _WIN32
    (void)dir;
    (void)kind;
    return {};
#else
    std::ostringstream oss;
    oss << "cd " << quote(dir.string()) << " && cmake --list-presets=" << kind;
    auto out = run_and_capture(oss.str());
    std::vector<std::string> names;
    std::istringstream is(out);
    std::string line;
    while (std::getline(is, line))
    {
      auto q1 = line.find('\"');
      auto q2 = line.find('\"', q1 == std::string::npos ? q1 : q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
        names.emplace_back(line.substr(q1 + 1, q2 - (q1 + 1)));
    }
    return names;
#endif
  }

  std::string choose_run_preset(
      const fs::path &dir,
      const std::string &configurePreset,
      const std::string &userRunPreset)
  {
    auto runs = list_presets(dir, "build");
    auto has = [&](const std::string &n)
    {
      return std::find(runs.begin(), runs.end(), n) != runs.end();
    };

    if (!userRunPreset.empty() && (runs.empty() || has(userRunPreset)))
      return userRunPreset;

    if (!runs.empty())
    {
      if (has("run-" + configurePreset))
        return "run-" + configurePreset;

      if (configurePreset.rfind("dev-", 0) == 0)
      {
        std::string mapped = "run-" + configurePreset.substr(4);
        if (has(mapped))
          return mapped;
      }

      if (has("run-ninja"))
        return "run-ninja";

      if (has("build-ninja"))
        return "build-ninja";

      return runs.front();
    }

    if (configurePreset.rfind("dev-", 0) == 0)
      return std::string("run-") + configurePreset.substr(4);
    return "run-ninja";
  }

  bool has_cmake_cache(const fs::path &buildDir)
  {
    std::error_code ec;
    return fs::exists(buildDir / "CMakeCache.txt", ec);
  }

#ifndef _WIN32
  static std::optional<std::chrono::file_clock::time_point>
  mtime_if_exists(const fs::path &p)
  {
    std::error_code ec;
    if (!fs::exists(p, ec) || ec)
      return std::nullopt;
    auto t = fs::last_write_time(p, ec);
    if (ec)
      return std::nullopt;
    return t;
  }

  std::string choose_configure_preset_smart(
      const fs::path &projectDir,
      const std::string &userPreset)
  {

    // Respect user choice always
    if (!userPreset.empty())
      return userPreset;

    auto cfgs = list_presets(projectDir, "configure");
    if (cfgs.empty())
      return "dev-ninja";

    struct Candidate
    {
      std::string preset;
      fs::path buildDir;
      std::chrono::file_clock::time_point stamp{};
    };

    std::optional<Candidate> best;

    for (const auto &preset : cfgs)
    {
      // IMPORTANT: do NOT rely on parsing binaryDir from presets json
      const fs::path buildDir = resolve_build_dir_smart(projectDir, preset);

      if (!has_cmake_cache(buildDir))
        continue;

      auto t = mtime_if_exists(buildDir / "CMakeCache.txt");
      if (!t)
        continue;

      Candidate c{preset, buildDir, *t};

      if (!best || c.stamp > best->stamp)
        best = c;
    }

    if (vix::utils::vix_getenv("VIX_DEBUG_PRESET"))
    {
      info("Preset candidates:");
      for (const auto &preset : cfgs)
      {
        const auto buildDir = resolve_build_dir_smart(projectDir, preset);
        step("• " + preset + " -> " + buildDir.string() +
             (has_cmake_cache(buildDir) ? " [cache]" : " [no-cache]"));
      }
    }

    // If we found an existing configured preset, prefer it.
    if (best)
      return best->preset;

    // Otherwise keep a stable default:
    if (std::find(cfgs.begin(), cfgs.end(), "dev-ninja") != cfgs.end())
      return "dev-ninja";

    return cfgs.front();
  }

#else
  std::string choose_configure_preset_smart(const fs::path &, const std::string &userPreset)
  {
    return userPreset.empty() ? std::string("dev-ninja") : userPreset;
  }
#endif

  std::optional<fs::path> choose_project_dir(const Options &opt, const fs::path &cwd)
  {
    auto exists_cml = [](const fs::path &p)
    {
      std::error_code ec;
      return fs::exists(p / "CMakeLists.txt", ec);
    };

    if (!opt.dir.empty() && exists_cml(opt.dir))
      return fs::path(opt.dir);

    if (exists_cml(cwd))
      return cwd;

    if (!opt.appName.empty())
    {
      fs::path a = opt.appName;
      if (exists_cml(a))
        return a;
      if (exists_cml(cwd / a))
        return cwd / a;
    }

    return cwd;
  }

  void apply_log_level_env(const Options &opt)
  {
    std::string level;

    if (!opt.logLevel.empty())
      level = lower(opt.logLevel);
    else if (opt.quiet)
      level = "warn";
    else if (opt.verbose)
      level = "debug";

    if (level.empty())
      return;

    if (level == "never" || level == "silent" || level == "0")
      level = "off";

    if (level == "unset" || level == "default")
    {
#if defined(_WIN32)
      _putenv_s("VIX_LOG_LEVEL", "");
#else
      ::unsetenv("VIX_LOG_LEVEL");
#endif
      return;
    }

    if (level == "none")
      level = "off";

    if (level == "on")
      level = "info";

    if (level != "trace" && level != "debug" && level != "info" &&
        level != "warn" && level != "error" && level != "critical" &&
        level != "off")
    {
      hint("Invalid value for --log-level. Using 'info'. Valid: trace|debug|info|warn|error|critical|off.");
      level = "info";
    }

#if defined(_WIN32)
    _putenv_s("VIX_LOG_LEVEL", level.c_str());
#else
    ::setenv("VIX_LOG_LEVEL", level.c_str(), 1);
#endif
  }

  void apply_log_format_env(const Options &opt)
  {
    if (opt.logFormat.empty())
      return;

    std::string fmt = lower(opt.logFormat);

    // aliases
    if (fmt == "pretty" || fmt == "pretty-json" || fmt == "pretty_json")
      fmt = "json-pretty";

    if (fmt != "kv" && fmt != "json" && fmt != "json-pretty")
    {
      hint("Invalid value for --log-format. Using 'kv'. Valid: kv|json|json-pretty.");
      fmt = "kv";
    }

#if defined(_WIN32)
    _putenv_s("VIX_LOG_FORMAT", fmt.c_str());
#else
    ::setenv("VIX_LOG_FORMAT", fmt.c_str(), 1);
#endif
  }

  void apply_log_color_env(const Options &opt)
  {
    // --no-color gagne sur tout
    if (opt.noColor)
    {
#if defined(_WIN32)
      _putenv_s("VIX_COLOR", "never");
#else
      ::setenv("VIX_COLOR", "never", 1);
#endif
      return;
    }

    if (opt.logColor.empty())
      return;

    std::string v = lower(opt.logColor);
    if (v != "auto" && v != "always" && v != "never")
    {
      hint("Invalid value for --log-color. Using 'auto'. Valid: auto|always|never.");
      v = "auto";
    }

#if defined(_WIN32)
    _putenv_s("VIX_COLOR", v.c_str());
#else
    ::setenv("VIX_COLOR", v.c_str(), 1);
#endif
  }

} // namespace vix::commands::RunCommand::detail
