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
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  namespace
  {
    enum class Zone
    {
      Vix,
      Script,
      Run
    };

    bool is_option_token(std::string_view s)
    {
      return !s.empty() && s.front() == '-';
    }

    std::string lower_copy(std::string s)
    {
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return s;
    }

    std::string take_eq_value(const std::string &arg, const std::string &prefix)
    {
      return arg.substr(prefix.size());
    }

    std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
    {
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "-d" || a == "--dir")
        {
          if (i + 1 < args.size() && !is_option_token(args[i + 1]))
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

    bool is_known_vix_flag(const std::string &v)
    {
      return v == "--verbose" || v == "--quiet" || v == "-q" ||
             v == "--watch" || v == "--reload" ||
             v == "--force-server" || v == "--force-script" ||
             v == "--san" || v == "--ubsan" ||
             v == "--docs" || v == "--no-docs" || v.rfind("--docs=", 0) == 0 ||
             v == "--no-color" ||
             v == "--preset" || v.rfind("--preset=", 0) == 0 ||
             v == "--run-preset" || v.rfind("--run-preset=", 0) == 0 ||
             v == "--cwd" || v.rfind("--cwd=", 0) == 0 ||
             v == "--env" || v.rfind("--env=", 0) == 0 ||
             v == "--args" || v.rfind("--args=", 0) == 0 ||
             v == "--log-level" || v == "--loglevel" || v.rfind("--log-level=", 0) == 0 ||
             v == "--log-format" || v.rfind("--log-format=", 0) == 0 ||
             v == "--log-color" || v.rfind("--log-color=", 0) == 0 ||
             v == "--clear" || v.rfind("--clear=", 0) == 0 ||
             v == "--no-clear" ||
             v == "--auto-deps" || v.rfind("--auto-deps=", 0) == 0 ||
             v == "--with-sqlite" ||
             v == "--with-mysql" ||
             v == "--clean" ||
             v == "-j" || v == "--jobs";
    }

    void warn_if_vix_flag_in_script(Options &opt, const std::string &value)
    {
      if (!opt.warnedVixFlagAfterDoubleDash && is_known_vix_flag(value))
      {
        opt.warnedVixFlagAfterDoubleDash = true;
        opt.warnedArg = value;
      }
    }

    std::string take_value(
        const std::vector<std::string> &args,
        std::size_t &i,
        const std::string &flag,
        Options &opt)
    {
      if (i + 1 >= args.size())
      {
        error("Missing value for " + flag);
        opt.parseFailed = true;
        opt.parseExitCode = 2;
        return {};
      }

      return args[++i];
    }

    void set_absolute_cwd(std::string &out, const fs::path &p)
    {
      fs::path value = p;
      if (value.is_relative())
        value = fs::absolute(value);

      out = value.string();
    }

    void normalize_clear_mode(Options &opt)
    {
      opt.clearMode = lower_copy(opt.clearMode);

      if (opt.clearMode != "auto" &&
          opt.clearMode != "always" &&
          opt.clearMode != "never")
      {
        hint("Invalid value for --clear. Using 'auto'. Valid: auto|always|never.");
        opt.clearMode = "auto";
      }
    }

    void finalize_parse_options(Options &opt, const std::vector<std::string> &args)
    {
      if (auto d = pick_dir_opt_local(args))
        opt.dir = *d;

      if (opt.forceServerLike && opt.forceScriptLike)
      {
        hint("Both --force-server and --force-script were provided; preferring --force-server.");
        opt.forceScriptLike = false;
      }

      if (opt.singleCpp)
      {
        if (opt.autoDeps != AutoDepsMode::Up)
          opt.autoDeps = AutoDepsMode::Local;
      }

      normalize_clear_mode(opt);
    }

    void handle_positional_argument(Options &opt, const std::string &arg)
    {
      if (opt.appName.empty())
      {
        opt.appName = arg;

        const fs::path p{arg};
        if (p.extension() == ".cpp")
        {
          opt.singleCpp = true;
          opt.cppFile = fs::absolute(p);
        }
      }
      else if (opt.appName == "example" && opt.exampleName.empty())
      {
        opt.exampleName = arg;
      }

      const fs::path p{arg};
      if (p.extension() == ".vix")
      {
        opt.manifestMode = true;
        opt.manifestFile = fs::absolute(p);
      }
      else if (p.extension() == ".cpp")
      {
        opt.singleCpp = true;
        opt.cppFile = fs::absolute(p);
      }
    }

#ifndef _WIN32
    bool ends_with_2to1(const std::string &s)
    {
      const std::size_t end = s.find_last_not_of(" \t\r\n");
      if (end == std::string::npos)
        return false;

      const std::string needle = "2>&1";
      if (end + 1 < needle.size())
        return false;

      const std::size_t start = end + 1 - needle.size();
      return s.compare(start, needle.size(), needle) == 0;
    }

    int normalize_exit_status(int status)
    {
      if (status == -1)
        return -1;

      if (WIFEXITED(status))
        return WEXITSTATUS(status);

      if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

      return status;
    }

    std::optional<std::chrono::file_clock::time_point> mtime_if_exists(const fs::path &p)
    {
      std::error_code ec;
      if (!fs::exists(p, ec) || ec)
        return std::nullopt;

      auto t = fs::last_write_time(p, ec);
      if (ec)
        return std::nullopt;

      return t;
    }
#endif
  } // namespace

  fs::path manifest_entry_cpp(const fs::path &manifestFile)
  {
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

      while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());

      while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
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

    const fs::path fallbackMain = root / "main.cpp";
    const fs::path fallbackSrcMain = root / "src" / "main.cpp";

    {
      std::error_code ec;
      if (!fs::exists(manifestFile, ec) || ec)
      {
        if (auto p = abs_if_exists(fallbackSrcMain))
          return *p;
        if (auto p = abs_if_exists(fallbackMain))
          return *p;

        return fs::absolute(fallbackSrcMain);
      }
    }

    std::ifstream in(manifestFile.string());
    std::string line;

    while (std::getline(in, line))
    {
      line = trim(line);
      if (line.empty() || line[0] == '#')
        continue;

      if (const auto hash = line.find('#'); hash != std::string::npos)
        line = trim(line.substr(0, hash));

      if (line.rfind("entry", 0) != 0)
        continue;

      const auto posEq = line.find('=');
      const auto posCl = line.find(':');
      const auto pos = (posEq == std::string::npos) ? posCl : posEq;
      if (pos == std::string::npos)
        continue;

      std::string rhs = strip_quotes(line.substr(pos + 1));
      if (rhs.empty())
        continue;

      if (auto p = abs_if_exists(fs::path(rhs)))
        return *p;
    }

    if (auto p = abs_if_exists(fallbackSrcMain))
      return *p;
    if (auto p = abs_if_exists(fallbackMain))
      return *p;

    return fs::absolute(fallbackSrcMain);
  }

  Options parse(const std::vector<std::string> &args)
  {
    Options opt;
    Zone zone = Zone::Vix;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &a = args[i];

      if (a == "--run")
      {
        opt.hasRunSeparator = true;
        zone = Zone::Run;
        continue;
      }

      if (a == "--")
      {
        opt.hasDoubleDash = true;
        zone = Zone::Script;
        continue;
      }

      if (zone == Zone::Run)
      {
        opt.runArgs.push_back(a);
        opt.runArgsAfterRun.push_back(a);
        continue;
      }

      if (zone == Zone::Script)
      {
        warn_if_vix_flag_in_script(opt, a);
        opt.scriptFlags.push_back(a);
        opt.doubleDashArgs.push_back(a);
        continue;
      }

      if (a == "--preset")
      {
        opt.preset = take_value(args, i, "--preset", opt);
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--preset=", 0) == 0)
      {
        opt.preset = take_eq_value(a, "--preset=");
      }
      else if (a == "--run-preset")
      {
        opt.runPreset = take_value(args, i, "--run-preset", opt);
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--run-preset=", 0) == 0)
      {
        opt.runPreset = take_eq_value(a, "--run-preset=");
      }
      else if (a == "-j" || a == "--jobs")
      {
        const std::string v = take_value(args, i, a, opt);
        if (opt.parseFailed)
          return opt;

        try
        {
          opt.jobs = std::stoi(v);
        }
        catch (...)
        {
          opt.jobs = 0;
        }
      }
      else if (a == "--quiet" || a == "-q")
      {
        opt.quiet = true;
      }
      else if (a == "--verbose")
      {
        opt.verbose = true;
      }
      else if (a == "--log-level" || a == "--loglevel")
      {
        opt.logLevel = take_value(args, i, a, opt);
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--log-level=", 0) == 0)
      {
        opt.logLevel = take_eq_value(a, "--log-level=");
      }
      else if (a == "--log-format")
      {
        opt.logFormat = take_value(args, i, "--log-format", opt);
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--log-format=", 0) == 0)
      {
        opt.logFormat = take_eq_value(a, "--log-format=");
      }
      else if (a == "--log-color")
      {
        opt.logColor = take_value(args, i, "--log-color", opt);
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--log-color=", 0) == 0)
      {
        opt.logColor = take_eq_value(a, "--log-color=");
      }
      else if (a == "--no-color")
      {
        opt.noColor = true;
      }
      else if (a == "--watch" || a == "--reload")
      {
        opt.watch = true;
      }
      else if (a == "--force-server")
      {
        opt.forceServerLike = true;
      }
      else if (a == "--force-script")
      {
        opt.forceScriptLike = true;
      }
      else if (a == "--docs")
      {
        opt.docs = true;
      }
      else if (a == "--no-docs")
      {
        opt.docs = false;
      }
      else if (a.rfind("--docs=", 0) == 0)
      {
        const std::string v = lower_copy(take_eq_value(a, "--docs="));
        if (v == "1" || v == "true" || v == "yes" || v == "on")
          opt.docs = true;
        else if (v == "0" || v == "false" || v == "no" || v == "off")
          opt.docs = false;
        else
          hint("Invalid value for --docs. Use 0|1|true|false.");
      }
      else if (a == "--with-sqlite")
      {
        opt.withSqlite = true;
      }
      else if (a == "--with-mysql")
      {
        opt.withMySql = true;
      }
      else if (a == "--clean")
      {
        opt.clean = true;
      }
      else if (a == "--cwd")
      {
        const fs::path p = take_value(args, i, "--cwd", opt);
        if (opt.parseFailed)
          return opt;

        set_absolute_cwd(opt.cwd, p);
      }
      else if (a.rfind("--cwd=", 0) == 0)
      {
        set_absolute_cwd(opt.cwd, fs::path(take_eq_value(a, "--cwd=")));
      }
      else if (a == "--env")
      {
        opt.runEnv.push_back(take_value(args, i, "--env", opt));
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--env=", 0) == 0)
      {
        opt.runEnv.push_back(take_eq_value(a, "--env="));
      }
      else if (a == "--args")
      {
        opt.runArgs.push_back(take_value(args, i, "--args", opt));
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--args=", 0) == 0)
      {
        opt.runArgs.push_back(take_eq_value(a, "--args="));
      }
      else if (a == "--san")
      {
        opt.enableSanitizers = true;
        opt.enableUbsanOnly = false;
      }
      else if (a == "--ubsan")
      {
        opt.enableUbsanOnly = true;
        opt.enableSanitizers = false;
      }
      else if (a == "--auto-deps")
      {
        opt.autoDeps = AutoDepsMode::Local;
      }
      else if (a.rfind("--auto-deps=", 0) == 0)
      {
        const std::string v = lower_copy(take_eq_value(a, "--auto-deps="));
        if (v == "up")
          opt.autoDeps = AutoDepsMode::Up;
        else if (v == "local")
          opt.autoDeps = AutoDepsMode::Local;
        else
        {
          error("Invalid value for --auto-deps: " + v);
          hint("Valid values: local, up");
          opt.parseFailed = true;
          opt.parseExitCode = 2;
          return opt;
        }
      }
      else if (a == "--clear")
      {
        opt.clearMode = take_value(args, i, "--clear", opt);
        if (opt.parseFailed)
          return opt;
      }
      else if (a.rfind("--clear=", 0) == 0)
      {
        opt.clearMode = take_eq_value(a, "--clear=");
      }
      else if (a == "--no-clear")
      {
        opt.clearMode = "never";
      }
      else if (!a.empty() && a[0] != '-')
      {
        handle_positional_argument(opt, a);
      }
      else if (!a.empty() && a[0] == '-')
      {
        error("Unknown option: " + a);
        hint("Use: vix run --help");
        opt.parseFailed = true;
        opt.parseExitCode = 2;
        return opt;
      }
    }

    finalize_parse_options(opt, args);
    return opt;
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

    if (log.find("Built target") != std::string::npos)
      return false;

    return true;
  }

  std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
  {
    std::string out;
    std::string captureCmd = cmd;

    if (!ends_with_2to1(captureCmd))
      captureCmd += " 2>&1";

    FILE *p = popen(captureCmd.c_str(), "r");
    if (!p)
    {
      exitCode = -1;
      return out;
    }

    char buf[4096];
    while (fgets(buf, sizeof(buf), p))
      out.append(buf);

    const int status = pclose(p);
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

  std::optional<fs::path> preset_binary_dir(
      const fs::path &projectDir,
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

      const std::size_t colon = obj.find(':', b + binKey.size());
      if (colon == std::string::npos)
        return std::nullopt;

      const std::size_t q1 = obj.find('"', colon);
      if (q1 == std::string::npos)
        return std::nullopt;

      const std::size_t q2 = obj.find('"', q1 + 1);
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

  fs::path resolve_build_dir_smart(
      const fs::path &projectDir,
      const std::string &configurePreset)
  {
    if (auto binDir = preset_binary_dir(projectDir, configurePreset))
      return *binDir;

    fs::path p = projectDir / ("build-" + configurePreset);
    if (fs::exists(p))
      return p;

    if (configurePreset.rfind("dev-", 0) == 0)
    {
      fs::path alt = projectDir / ("build-" + configurePreset.substr(4));
      if (fs::exists(alt))
        return alt;
    }

    return projectDir / "build";
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

    const std::string out = run_and_capture(oss.str());
    std::vector<std::string> names;

    std::istringstream is(out);
    std::string line;
    while (std::getline(is, line))
    {
      const auto q1 = line.find('"');
      const auto q2 = line.find('"', q1 == std::string::npos ? q1 : q1 + 1);
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
    const auto runs = list_presets(dir, "build");

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
        const std::string mapped = "run-" + configurePreset.substr(4);
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
      return "run-" + configurePreset.substr(4);

    return "run-ninja";
  }

  bool has_cmake_cache(const fs::path &buildDir)
  {
    std::error_code ec;
    return fs::exists(buildDir / "CMakeCache.txt", ec);
  }

#ifndef _WIN32
  std::string choose_configure_preset_smart(
      const fs::path &projectDir,
      const std::string &userPreset)
  {
    if (!userPreset.empty())
      return userPreset;

    const auto cfgs = list_presets(projectDir, "configure");
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
      const fs::path buildDir = resolve_build_dir_smart(projectDir, preset);

      if (!has_cmake_cache(buildDir))
        continue;

      const auto t = mtime_if_exists(buildDir / "CMakeCache.txt");
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

    if (best)
      return best->preset;

    if (std::find(cfgs.begin(), cfgs.end(), "dev-ninja") != cfgs.end())
      return "dev-ninja";

    return cfgs.front();
  }
#else
  std::string choose_configure_preset_smart(
      const fs::path &,
      const std::string &userPreset)
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
      level = lower_copy(opt.logLevel);
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

    if (level != "trace" &&
        level != "debug" &&
        level != "info" &&
        level != "warn" &&
        level != "error" &&
        level != "critical" &&
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

    std::string fmt = lower_copy(opt.logFormat);

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

    std::string v = lower_copy(opt.logColor);
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
