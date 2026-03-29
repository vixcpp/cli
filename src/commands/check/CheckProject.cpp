/**
 *
 *  @file CheckProject.cpp
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
#include <vix/cli/commands/check/CheckDetail.hpp>
#include <vix/cli/commands/helpers/ProcessHelpers.hpp>
#include <vix/cli/commands/run/RunScriptHelpers.hpp>
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/ErrorHandler.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/cmake/GlobalPackages.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <cstdlib> // setenv
#endif

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;
  namespace run = vix::commands::RunCommand::detail;
  namespace ui = vix::cli::util;
  namespace build = vix::cli::build;
  namespace style = vix::cli::style;

#ifndef _WIN32
  using vix::commands::RunCommand::detail::handle_runtime_exit_code;
  using vix::commands::RunCommand::detail::run_cmd_live_filtered;
  using vix::commands::RunCommand::detail::run_cmd_live_filtered_capture;
#endif

  using vix::cli::commands::helpers::has_cmake_cache;
  using vix::cli::commands::helpers::quote;
  using vix::cli::commands::helpers::run_and_capture_with_code;
  using vix::commands::RunCommand::detail::has_presets;

  namespace
  {
    struct ProjectCheckSummary
    {
      bool configured = false;
      bool built = false;
      bool testsRan = false;
      bool runtimeRan = false;
      bool sanitizersEnabled = false;
      bool ubsanOnly = false;
      bool runtimeTimedOutButHealthy = false;
    };

    static bool write_text_file(const fs::path &path, const std::string &content)
    {
      std::ofstream ofs(path);
      if (!ofs)
        return false;

      ofs << content;
      return static_cast<bool>(ofs);
    }

    static std::string to_lower_copy(std::string s)
    {
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    static bool is_timeout_exit_code(int code) noexcept
    {
      return code == 124;
    }

    static bool contains_preset(const std::vector<std::string> &presets, const std::string &name)
    {
      return std::find(presets.begin(), presets.end(), name) != presets.end();
    }

    static std::vector<std::string> parse_preset_names_from_output(const std::string &text)
    {
      std::vector<std::string> out;
      std::istringstream iss(text);
      std::string line;

      while (std::getline(iss, line))
      {
        const auto q1 = line.find('"');
        if (q1 == std::string::npos)
          continue;

        const auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos)
          continue;

        const std::string name = line.substr(q1 + 1, q2 - q1 - 1);
        if (!name.empty())
          out.push_back(name);
      }

      return out;
    }

    static std::vector<std::string> list_configure_presets(const fs::path &projectDir)
    {
      std::ostringstream cmd;
#ifdef _WIN32
      cmd << "cmd /C \"cd /D " << quote(projectDir.string())
          << " && cmake --list-presets=configure\"";
#else
      cmd << "cd " << quote(projectDir.string())
          << " && cmake --list-presets=configure";
#endif

      int code = 0;
      const std::string out = run_and_capture_with_code(cmd.str(), code);
      if (code != 0 || out.empty())
        return {};

      return parse_preset_names_from_output(out);
    }

    static std::vector<std::string> list_build_presets(const fs::path &projectDir)
    {
      std::ostringstream cmd;
#ifdef _WIN32
      cmd << "cmd /C \"cd /D " << quote(projectDir.string())
          << " && cmake --list-presets=build\"";
#else
      cmd << "cd " << quote(projectDir.string())
          << " && cmake --list-presets=build";
#endif

      int code = 0;
      const std::string out = run_and_capture_with_code(cmd.str(), code);
      if (code != 0 || out.empty())
        return {};

      return parse_preset_names_from_output(out);
    }

    static std::string profile_name(bool san, bool ubsanOnly)
    {
      if (san)
        return "asan+ubsan";
      if (ubsanOnly)
        return "ubsan";
      return "default";
    }

    static std::string profile_suffix(bool san, bool ubsanOnly)
    {
      if (san)
        return "-san";
      if (ubsanOnly)
        return "-ubsan";
      return "";
    }

    static fs::path compute_check_build_dir(
        const fs::path &projectDir,
        const std::string &basePreset,
        bool san,
        bool ubsanOnly)
    {
      const std::string suffix = profile_suffix(san, ubsanOnly);

      if (basePreset.rfind("dev-", 0) == 0)
        return projectDir / ("build-" + basePreset.substr(4) + suffix);

      return projectDir / ("build-" + basePreset + suffix);
    }

    static std::string derive_build_preset_name(const std::string &configurePreset)
    {
      if (configurePreset.rfind("dev-", 0) == 0)
        return "build-" + configurePreset.substr(4);

      return "build-" + configurePreset;
    }

    static std::string pick_configure_preset_smart(
        const fs::path &projectDir,
        const std::string &basePreset,
        bool san,
        bool ubsanOnly)
    {
      const auto presets = list_configure_presets(projectDir);

      auto has = [&](const std::string &name)
      {
        return contains_preset(presets, name);
      };

      if (san)
      {
        const std::string dedicated = basePreset + "-san";
        if (has(dedicated))
          return dedicated;
      }

      if (ubsanOnly)
      {
        const std::string dedicated = basePreset + "-ubsan";
        if (has(dedicated))
          return dedicated;
      }

      if (has(basePreset))
        return basePreset;

      if (has("dev-ninja"))
        return "dev-ninja";

      if (!presets.empty())
        return presets.front();

      return basePreset;
    }

    static std::string pick_build_preset_smart(
        const fs::path &projectDir,
        const std::string &configurePreset,
        const std::string &userOverride)
    {
      if (!userOverride.empty())
        return userOverride;

      const auto presets = list_build_presets(projectDir);
      if (presets.empty())
        return derive_build_preset_name(configurePreset);

      const std::string derived = derive_build_preset_name(configurePreset);
      if (contains_preset(presets, derived))
        return derived;

      if (contains_preset(presets, configurePreset))
        return configurePreset;

      if (configurePreset == "dev-ninja")
      {
        if (contains_preset(presets, "build-ninja"))
          return "build-ninja";
        if (contains_preset(presets, "build-dev-ninja"))
          return "build-dev-ninja";
      }

      return derived;
    }

    static bool wants_listing_only_ctest(const Options &opt)
    {
      for (const auto &arg : opt.ctestArgs)
      {
        if (arg == "--show-only" ||
            arg == "--show-only=json-v1" ||
            arg == "-N")
          return true;
      }
      return false;
    }

    static void append_ctest_args(std::ostringstream &os, const Options &opt)
    {
      for (const auto &arg : opt.ctestArgs)
        os << " " << quote(arg);
    }

    static std::string make_check_summary(const ProjectCheckSummary &summary)
    {
      std::vector<std::string> parts;

      if (summary.built)
        parts.push_back("build");
      if (summary.testsRan)
        parts.push_back("tests");
      if (summary.runtimeRan)
        parts.push_back("runtime");
      if (summary.sanitizersEnabled)
      {
        if (summary.ubsanOnly)
          parts.push_back("ubsan");
        else
          parts.push_back("asan+ubsan");
      }
      if (summary.runtimeTimedOutButHealthy)
        parts.push_back("server-stayed-alive");

      std::ostringstream os;
      for (std::size_t i = 0; i < parts.size(); ++i)
      {
        if (i > 0)
          os << ", ";
        os << parts[i];
      }

      return os.str();
    }

    static std::string read_cmake_cache_value(const fs::path &cacheFile, const std::string &key)
    {
      std::ifstream in(cacheFile);
      if (!in.is_open())
        return {};

      const std::string prefix = key + ":";
      std::string line;

      while (std::getline(in, line))
      {
        if (line.rfind(prefix, 0) != 0)
          continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos)
          continue;

        return line.substr(eq + 1);
      }

      return {};
    }

#ifndef _WIN32
    static std::string guess_project_name_from_dir(const fs::path &projectDir)
    {
      std::string name = projectDir.filename().string();
      if (name.empty())
        name = "app";
      return name;
    }

    static std::string resolve_build_type_from_cache_or_default(
        const fs::path &buildDir,
        const std::string &fallback = "Debug")
    {
      const fs::path cacheFile = buildDir / "CMakeCache.txt";
      if (!fs::exists(cacheFile))
        return fallback;

      std::string bt = read_cmake_cache_value(cacheFile, "CMAKE_BUILD_TYPE");
      if (bt.empty())
        return fallback;

      return bt;
    }

    static fs::path compute_runtime_executable_path(
        const fs::path &buildDir,
        const std::string &projectName,
        const std::string &configName)
    {
      std::string exeName = projectName;
#ifdef _WIN32
      exeName += ".exe";
#endif

      std::vector<fs::path> candidates;
      candidates.push_back(buildDir / exeName);
      candidates.push_back(buildDir / "bin" / exeName);
      candidates.push_back(buildDir / configName / exeName);
      candidates.push_back(buildDir / "bin" / configName / exeName);
      candidates.push_back(buildDir / "src" / exeName);
      candidates.push_back(buildDir / "src" / configName / exeName);

      for (const auto &candidate : candidates)
      {
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec)
          return candidate;
      }

      const fs::path cacheFile = buildDir / "CMakeCache.txt";
      if (fs::exists(cacheFile))
      {
        std::string outDir =
            read_cmake_cache_value(cacheFile, "CMAKE_RUNTIME_OUTPUT_DIRECTORY_" + configName);

        if (outDir.empty())
          outDir = read_cmake_cache_value(cacheFile, "CMAKE_RUNTIME_OUTPUT_DIRECTORY");

        if (!outDir.empty())
        {
          fs::path base = fs::path(outDir);
          if (base.is_relative())
            base = buildDir / base;

          const fs::path candidate = base / exeName;
          std::error_code ec;
          if (fs::exists(candidate, ec) && !ec)
            return candidate;
        }
      }

      return buildDir / exeName;
    }
#endif

    static void apply_log_level_env_local(const Options &opt)
    {
      if (opt.logLevel.empty() && !opt.quiet && !opt.verbose)
        return;

      std::string level = opt.logLevel;
      if (level.empty() && opt.quiet)
        level = "warn";
      if (level.empty() && opt.verbose)
        level = "debug";

#if defined(_WIN32)
      _putenv_s("VIX_LOG_LEVEL", level.c_str());
#else
      ::setenv("VIX_LOG_LEVEL", level.c_str(), 1);
#endif
    }

    static bool ensure_directory(const fs::path &dir)
    {
      std::error_code ec;
      fs::create_directories(dir, ec);
      return !ec;
    }

    static std::optional<fs::path> write_global_packages_file(const fs::path &buildDir)
    {
      if (!ensure_directory(buildDir))
        return std::nullopt;

      const auto packages = build::load_global_packages();
      const std::string cmakeContent = build::make_global_packages_cmake(packages);
      const fs::path file = buildDir / "vix-global-packages.cmake";

      if (!write_text_file(file, cmakeContent))
        return std::nullopt;

      return file;
    }

#ifndef _WIN32
    static int run_tests_with_fallback(
        const Options &opt,
        const fs::path &projectDir,
        const fs::path &buildDir,
        const std::string &configurePreset)
    {
      const bool listingOnly = wants_listing_only_ctest(opt);
      const fs::path ctestPresets = projectDir / "CTestPresets.json";
      const bool hasCTestPresets = fs::exists(ctestPresets);

      if (hasCTestPresets || !opt.ctestPreset.empty())
      {
        const std::string ctestPreset =
            opt.ctestPreset.empty() ? ("test-" + configurePreset) : opt.ctestPreset;

        std::ostringstream cmd;
        cmd << "cd " << quote(projectDir.string())
            << " && ctest --preset " << quote(ctestPreset);

        if (!listingOnly)
          cmd << " --output-on-failure";

        append_ctest_args(cmd, opt);

        int code = run_cmd_live_filtered(cmd.str(), "Running tests");
        if (code == 0)
          return 0;

        ui::warn_line(std::cout, "CTest preset failed, fallback to build directory.");
      }

      std::ostringstream fb;
      fb << "cd " << quote(buildDir.string()) << " && ctest";

      if (!listingOnly)
        fb << " --output-on-failure";

      append_ctest_args(fb, opt);

      return run_cmd_live_filtered(fb.str(), "Running tests (fallback)");
    }

    static bool runtime_log_looks_like_failure(const std::string &log)
    {
      if (log.empty())
        return false;

      const std::string s = to_lower_copy(log);

      return s.find("addresssanitizer") != std::string::npos ||
             s.find("undefinedbehaviorsanitizer") != std::string::npos ||
             s.find("leaksanitzer") != std::string::npos ||
             s.find("leaksanitizer") != std::string::npos ||
             s.find("threadsanitizer") != std::string::npos ||
             s.find("memorysanitizer") != std::string::npos ||
             s.find("runtime error:") != std::string::npos ||
             s.find("segmentation fault") != std::string::npos ||
             s.find("core dumped") != std::string::npos ||
             s.find("abort") != std::string::npos ||
             s.find("stack trace") != std::string::npos ||
             s.find("exception") != std::string::npos;
    }

    static bool runtime_log_looks_like_healthy_server(const std::string &log)
    {
      if (log.empty())
        return false;

      const std::string s = to_lower_copy(log);

      return s.find("ready") != std::string::npos ||
             s.find("listening") != std::string::npos ||
             s.find("http://") != std::string::npos ||
             s.find("https://") != std::string::npos ||
             s.find("ctrl+c to stop") != std::string::npos ||
             s.find("status: ready") != std::string::npos;
    }

    static int run_runtime_check(
        const Options &opt,
        const fs::path &projectDir,
        const fs::path &buildDir,
        ProjectCheckSummary &summary)
    {
      const std::string projectName = guess_project_name_from_dir(projectDir);
      const std::string configName = resolve_build_type_from_cache_or_default(buildDir, "Debug");
      const fs::path exePath = compute_runtime_executable_path(buildDir, projectName, configName);
      const int timeoutSec = (opt.runTimeoutSec > 0) ? opt.runTimeoutSec : 15;

      if (!fs::exists(exePath))
      {
        style::error("Runtime executable not found: " + exePath.string());
        style::hint("The build succeeded, but no runnable binary matching the project name was found.");
        style::hint("Use --run only for projects that produce an executable with the project directory name.");
        return 5;
      }

      if (opt.enableSanitizers || opt.enableUbsanOnly)
        run::apply_sanitizer_env_if_needed(opt.enableSanitizers, opt.enableUbsanOnly);

      std::ostringstream cmd;
      cmd << "cd " << quote(buildDir.string()) << " && " << quote(exePath.string());

      auto rr = run_cmd_live_filtered_capture(
          cmd.str(),
          "Checking runtime (" + exePath.filename().string() + ")",
          /*passthroughRuntime=*/false,
          /*timeoutSec=*/timeoutSec);

      std::string runtimeLog;
      runtimeLog.reserve(rr.stdoutText.size() + rr.stderrText.size() + 1);

      if (!rr.stdoutText.empty())
        runtimeLog += rr.stdoutText;

      if (!rr.stderrText.empty())
      {
        if (!runtimeLog.empty() && runtimeLog.back() != '\n')
          runtimeLog.push_back('\n');
        runtimeLog += rr.stderrText;
      }

      if (rr.exitCode == 0)
      {
        summary.runtimeRan = true;
        return 0;
      }

      if (is_timeout_exit_code(rr.exitCode) &&
          !runtime_log_looks_like_failure(runtimeLog) &&
          runtime_log_looks_like_healthy_server(runtimeLog))
      {
        summary.runtimeRan = true;
        summary.runtimeTimedOutButHealthy = true;
        ui::warn_line(std::cout, "Runtime timed out, but the process looked healthy and stayed alive.");
        ui::kv(std::cout, "timeout", std::to_string(timeoutSec) + "s");
        return 0;
      }

      bool handled = false;
      if (!runtimeLog.empty())
      {
        handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
            runtimeLog,
            projectDir,
            "Project check failed (runtime)");

        if (!handled &&
            vix::cli::errors::RawLogDetectors::handleKnownRunFailure(runtimeLog, projectDir))
          handled = true;
      }

      handle_runtime_exit_code(
          rr.exitCode,
          "Project check failed (runtime)",
          /*alreadyHandled=*/handled);

      return rr.exitCode;
    }
#endif

    static void print_header(
        const Options &opt,
        const fs::path &projectDir,
        const std::string &preset,
        const fs::path &buildDir,
        bool dedicatedSanPreset,
        bool runtimeEnabled)
    {
      if (opt.quiet)
        return;

      ui::section(std::cout, "Check");
      ui::kv(std::cout, "project", projectDir.string());
      ui::kv(std::cout, "preset", preset);
      ui::kv(std::cout, "build dir", buildDir.string());
      ui::kv(std::cout, "profile", profile_name(opt.enableSanitizers, opt.enableUbsanOnly));
      ui::kv(std::cout, "tests", opt.tests ? "enabled" : "disabled");
      ui::kv(std::cout, "runtime", runtimeEnabled ? "enabled" : "disabled");

      if (opt.enableSanitizers || opt.enableUbsanOnly)
        ui::kv(std::cout, "san preset", dedicatedSanPreset ? "dedicated preset" : "fallback manual configure");

      ui::one_line_spacer(std::cout);
    }
  } // namespace

  int check_project(const Options &opt, const fs::path &projectDir)
  {
    apply_log_level_env_local(opt);

    ProjectCheckSummary summary;
    summary.sanitizersEnabled = opt.enableSanitizers || opt.enableUbsanOnly;
    summary.ubsanOnly = opt.enableUbsanOnly;

    const bool shouldRunRuntime =
        opt.runAfterBuild || opt.enableSanitizers || opt.enableUbsanOnly;

    if (has_presets(projectDir))
    {
      const bool wantsSan = opt.enableSanitizers;
      const bool wantsUbsan = opt.enableUbsanOnly;

      const std::string configurePreset =
          pick_configure_preset_smart(projectDir, opt.preset, wantsSan, wantsUbsan);

      const bool hasDedicatedSanPreset =
          (wantsSan && configurePreset == (opt.preset + "-san")) ||
          (wantsUbsan && configurePreset == (opt.preset + "-ubsan"));

      fs::path buildDir = compute_check_build_dir(
          projectDir,
          opt.preset,
          wantsSan,
          wantsUbsan);

      if (!summary.sanitizersEnabled)
      {
        const fs::path legacyBuildDir = projectDir / derive_build_preset_name(configurePreset);
        if (!has_cmake_cache(buildDir) && has_cmake_cache(legacyBuildDir))
          buildDir = legacyBuildDir;
      }

      print_header(opt, projectDir, configurePreset, buildDir, hasDedicatedSanPreset, shouldRunRuntime);

      if (!has_cmake_cache(buildDir))
      {
        if (!opt.quiet)
        {
          ui::info_line(std::cout, "No matching CMake cache found for this check profile.");
          ui::kv(std::cout, "action", "configure");
          ui::one_line_spacer(std::cout);
        }

        const auto globalPackagesFile = write_global_packages_file(buildDir);
        if (!globalPackagesFile)
        {
          style::error("Failed to prepare global packages integration file.");
          style::hint("Check filesystem permissions for the build directory.");
          return 2;
        }

#ifdef _WIN32
        std::ostringstream conf;
        if (summary.sanitizersEnabled && !hasDedicatedSanPreset)
        {
          conf << "cmd /C \"cd /D " << quote(projectDir.string())
               << " && cmake -S . -B " << quote(buildDir.string())
               << " -G Ninja"
               << " -DCMAKE_BUILD_TYPE=Debug"
               << " -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" << quote(globalPackagesFile->string())
               << " -DVIX_ENABLE_SANITIZERS=ON"
               << " -DVIX_SANITIZER_MODE=" << quote(summary.ubsanOnly ? "ubsan" : "asan-ubsan")
               << "\"";
        }
        else
        {
          conf << "cmd /C \"cd /D " << quote(projectDir.string())
               << " && cmake --preset " << quote(configurePreset)
               << " --fresh"
               << " --"
               << " -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" << quote(globalPackagesFile->string())
               << "\"";
        }

        int code = 0;
        const std::string confLog = run_and_capture_with_code(conf.str(), code);
        if (code != 0)
        {
          if (!confLog.empty())
            std::cout << confLog;
          style::error("CMake configure failed.");
          return code != 0 ? code : 2;
        }
#else
        std::ostringstream conf;
        if (summary.sanitizersEnabled && !hasDedicatedSanPreset)
        {
          conf << "cd " << quote(projectDir.string())
               << " && cmake -S . -B " << quote(buildDir.string())
               << " -G Ninja"
               << " -DCMAKE_BUILD_TYPE=Debug"
               << " -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" << quote(globalPackagesFile->string())
               << " -DVIX_ENABLE_SANITIZERS=ON"
               << " -DVIX_SANITIZER_MODE=" << quote(summary.ubsanOnly ? "ubsan" : "asan-ubsan");
        }
        else
        {
          conf << "cd " << quote(projectDir.string())
               << " && cmake --preset " << quote(configurePreset)
               << " --fresh"
               << " --"
               << " -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" << quote(globalPackagesFile->string());
        }

        const int code = run_cmd_live_filtered(conf.str(), "Configuring project");
        if (code != 0)
        {
          style::error("CMake configure failed.");
          style::hint("Check the configure output above.");
          return code != 0 ? code : 2;
        }
#endif
        summary.configured = true;
        if (!opt.quiet)
          ui::ok_line(std::cout, "Configure OK.");
      }
      else
      {
        if (!opt.quiet)
        {
          ui::ok_line(std::cout, "CMake cache detected for this check profile.");
          ui::kv(std::cout, "build dir", buildDir.string());
          ui::one_line_spacer(std::cout);
        }
      }

      const std::string buildPreset =
          pick_build_preset_smart(projectDir, configurePreset, opt.buildPreset);

      const bool useDirectBuildDir =
          summary.sanitizersEnabled && !hasDedicatedSanPreset;

      if (!opt.quiet)
      {
        ui::info_line(std::cout, "Starting build.");
        ui::kv(std::cout, "mode", useDirectBuildDir ? "direct build dir" : "build preset");
        if (!useDirectBuildDir)
          ui::kv(std::cout, "build preset", buildPreset);
        ui::one_line_spacer(std::cout);
      }

#ifdef _WIN32
      {
        std::ostringstream buildCmd;
        if (useDirectBuildDir)
        {
          buildCmd << "cmd /C \"cd /D " << quote(projectDir.string())
                   << " && cmake --build " << quote(buildDir.string()) << " --target all";
        }
        else
        {
          buildCmd << "cmd /C \"cd /D " << quote(projectDir.string())
                   << " && cmake --build --preset " << quote(buildPreset) << " --target all";
        }

        if (opt.jobs > 0)
          buildCmd << " -- -j " << opt.jobs;

        buildCmd << "\"";

        int code = 0;
        const std::string buildLog = run_and_capture_with_code(buildCmd.str(), code);
        if (code != 0)
        {
          if (!buildLog.empty())
          {
            vix::cli::ErrorHandler::printBuildErrors(
                buildLog,
                projectDir / "CMakeLists.txt",
                "Project check failed (build)");
          }
          else
          {
            style::error("Project check failed during build.");
          }
          return code != 0 ? code : 3;
        }
      }
#else
      {
        std::ostringstream buildCmd;
        if (useDirectBuildDir)
        {
          buildCmd << "cd " << quote(projectDir.string())
                   << " && cmake --build " << quote(buildDir.string()) << " --target all";
        }
        else
        {
          buildCmd << "cd " << quote(projectDir.string())
                   << " && cmake --build --preset " << quote(buildPreset) << " --target all";
        }

        if (opt.jobs > 0)
          buildCmd << " -- -j " << opt.jobs;

        const int code = run_cmd_live_filtered(buildCmd.str(), "Checking build");
        if (code != 0)
        {
          style::error("Project check failed during build.");
          style::hint("Check the compiler output above.");
          return code != 0 ? code : 3;
        }
      }
#endif

      summary.built = true;
      if (!opt.quiet)
        ui::ok_line(std::cout, "Build OK.");

#ifndef _WIN32
      if (opt.tests)
      {
        ui::one_line_spacer(std::cout);
        ui::info_line(std::cout, "Running tests.");

        const int tcode = run_tests_with_fallback(opt, projectDir, buildDir, configurePreset);
        if (tcode != 0)
        {
          style::error("Tests failed.");
          return tcode != 0 ? tcode : 4;
        }

        summary.testsRan = true;
        if (!opt.quiet)
          ui::ok_line(std::cout, "Tests OK.");
      }

      if (shouldRunRuntime)
      {
        ui::one_line_spacer(std::cout);
        ui::info_line(std::cout, "Running runtime validation.");

        const int rcode = run_runtime_check(opt, projectDir, buildDir, summary);
        if (rcode != 0)
          return rcode;

        if (!opt.quiet)
          ui::ok_line(std::cout, "Runtime OK.");
      }
#endif

      ui::one_line_spacer(std::cout);
      if (!opt.quiet)
        ui::ok_line(std::cout, "Project check OK (" + make_check_summary(summary) + ").");
      else
        style::success("Project check OK (" + make_check_summary(summary) + ").");

      return 0;
    }

    if (!opt.quiet)
    {
      ui::section(std::cout, "Check");
      ui::kv(std::cout, "project", projectDir.string());
      ui::kv(std::cout, "mode", "fallback build/");
      ui::kv(std::cout, "profile", profile_name(opt.enableSanitizers, opt.enableUbsanOnly));
      ui::kv(std::cout, "tests", opt.tests ? "enabled" : "disabled");
      ui::kv(std::cout, "runtime", shouldRunRuntime ? "enabled" : "disabled");
      ui::one_line_spacer(std::cout);
    }

    fs::path buildDir = projectDir / ("build" + profile_suffix(opt.enableSanitizers, opt.enableUbsanOnly));

    std::error_code ec;
    fs::create_directories(buildDir, ec);
    if (ec)
    {
      style::error("Unable to create build directory: " + ec.message());
      return 1;
    }

    if (!has_cmake_cache(buildDir))
    {
      const auto globalPackagesFile = write_global_packages_file(buildDir);
      if (!globalPackagesFile)
      {
        style::error("Failed to prepare global packages integration file.");
        return 2;
      }

#ifdef _WIN32
      std::ostringstream conf;
      conf << "cmd /C \"cd /D " << quote(projectDir.string())
           << " && cmake -S . -B " << quote(buildDir.string())
           << " -G Ninja"
           << " -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" << quote(globalPackagesFile->string());

      if (summary.sanitizersEnabled)
      {
        conf << " -DVIX_ENABLE_SANITIZERS=ON"
             << " -DVIX_SANITIZER_MODE="
             << quote(summary.ubsanOnly ? "ubsan" : "asan-ubsan");
      }

      conf << "\"";

      int code = 0;
      const std::string confLog = run_and_capture_with_code(conf.str(), code);
      if (code != 0)
      {
        if (!confLog.empty())
          std::cout << confLog;
        style::error("CMake configure failed (fallback).");
        return code != 0 ? code : 2;
      }
#else
      std::ostringstream conf;
      conf << "cd " << quote(projectDir.string())
           << " && cmake -S . -B " << quote(buildDir.string())
           << " -G Ninja"
           << " -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" << quote(globalPackagesFile->string());

      if (summary.sanitizersEnabled)
      {
        conf << " -DVIX_ENABLE_SANITIZERS=ON"
             << " -DVIX_SANITIZER_MODE="
             << quote(summary.ubsanOnly ? "ubsan" : "asan-ubsan");
      }

      const int code = run_cmd_live_filtered(conf.str(), "Configuring (fallback)");
      if (code != 0)
      {
        style::error("CMake configure failed (fallback).");
        return code != 0 ? code : 2;
      }
#endif

      summary.configured = true;
      if (!opt.quiet)
        ui::ok_line(std::cout, "Configure OK.");
    }
    else
    {
      if (!opt.quiet)
        ui::ok_line(std::cout, "CMake cache detected.");
    }

#ifdef _WIN32
    {
      std::ostringstream buildCmd;
      buildCmd << "cmd /C \"cd /D " << quote(projectDir.string())
               << " && cmake --build " << quote(buildDir.string());
      if (opt.jobs > 0)
        buildCmd << " -- -j " << opt.jobs;
      buildCmd << "\"";

      int code = 0;
      const std::string buildLog = run_and_capture_with_code(buildCmd.str(), code);
      if (code != 0)
      {
        if (!buildLog.empty())
        {
          vix::cli::ErrorHandler::printBuildErrors(
              buildLog,
              buildDir,
              "Project check failed (fallback build)");
        }
        else
        {
          style::error("Build failed (fallback).");
        }
        return code != 0 ? code : 3;
      }
    }
#else
    {
      std::ostringstream buildCmd;
      buildCmd << "cd " << quote(projectDir.string())
               << " && cmake --build " << quote(buildDir.string());
      if (opt.jobs > 0)
        buildCmd << " -- -j " << opt.jobs;

      const int code = run_cmd_live_filtered(buildCmd.str(), "Checking build (fallback)");
      if (code != 0)
      {
        style::error("Build failed (fallback).");
        return code != 0 ? code : 3;
      }
    }
#endif

    summary.built = true;
    if (!opt.quiet)
      ui::ok_line(std::cout, "Build OK.");

#ifndef _WIN32
    if (opt.tests)
    {
      ui::one_line_spacer(std::cout);
      ui::info_line(std::cout, "Running tests.");

      std::ostringstream testsCmd;
      testsCmd << "cd " << quote(buildDir.string()) << " && ctest --output-on-failure";
      append_ctest_args(testsCmd, opt);

      const int tcode = run_cmd_live_filtered(testsCmd.str(), "Running tests");
      if (tcode != 0)
      {
        style::error("Tests failed.");
        return tcode != 0 ? tcode : 4;
      }

      summary.testsRan = true;
      if (!opt.quiet)
        ui::ok_line(std::cout, "Tests OK.");
    }

    if (shouldRunRuntime)
    {
      ui::one_line_spacer(std::cout);
      ui::info_line(std::cout, "Running runtime validation.");

      const int rcode = run_runtime_check(opt, projectDir, buildDir, summary);
      if (rcode != 0)
        return rcode;

      if (!opt.quiet)
        ui::ok_line(std::cout, "Runtime OK.");
    }
#endif

    ui::one_line_spacer(std::cout);
    if (!opt.quiet)
      ui::ok_line(std::cout, "Project check OK (" + make_check_summary(summary) + ").");
    else
      style::success("Project check OK (" + make_check_summary(summary) + ").");

    return 0;
  }

} // namespace vix::commands::CheckCommand::detail
