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

#include <filesystem>
#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>

#ifndef _WIN32
#include <cstdlib> // setenv
#endif

using namespace vix::cli::style;

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;
  namespace run = vix::commands::RunCommand::detail;

#ifndef _WIN32
  using vix::commands::RunCommand::detail::handle_runtime_exit_code;
  using vix::commands::RunCommand::detail::run_cmd_live_filtered;
  using vix::commands::RunCommand::detail::run_cmd_live_filtered_capture;
#endif

  using vix::cli::commands::helpers::has_cmake_cache;
  using vix::cli::commands::helpers::quote;
  using vix::cli::commands::helpers::run_and_capture_with_code;
  using vix::commands::RunCommand::detail::has_presets;

  static fs::path guess_build_dir_from_configure_preset(const fs::path &projectDir,
                                                        const std::string &preset)
  {
    // Convention Vix: dev-ninja -> build-ninja
    if (preset.rfind("dev-", 0) == 0)
      return projectDir / ("build-" + preset.substr(4));

    // Fallback simple
    return projectDir / ("build-" + preset);
  }

  static std::vector<std::string> list_build_presets(const fs::path &projectDir)
  {
    std::vector<std::string> out;

    std::ostringstream cmd;
#ifdef _WIN32
    cmd << "cmd /C \"cd /D " << quote(projectDir.string()) << " && cmake --list-presets\"";
#else
    cmd << "cd " << quote(projectDir.string()) << " && cmake --list-presets";
#endif

    int code = 0;
    const std::string s = run_and_capture_with_code(cmd.str(), code);
    if (code != 0 || s.empty())
      return out;

    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line))
    {
      auto q1 = line.find('"');
      if (q1 == std::string::npos)
        continue;
      auto q2 = line.find('"', q1 + 1);
      if (q2 == std::string::npos)
        continue;

      const std::string name = line.substr(q1 + 1, q2 - (q1 + 1));
      if (!name.empty())
        out.push_back(name);
    }

    return out;
  }

  static bool contains_preset(const std::vector<std::string> &presets, const std::string &name)
  {
    for (const auto &p : presets)
      if (p == name)
        return true;
    return false;
  }

  static std::string pick_build_preset_smart(
      const fs::path &projectDir,
      const std::string &configurePreset,
      const std::string &userBuildPresetOverride)
  {
    if (!userBuildPresetOverride.empty())
      return userBuildPresetOverride;

    const auto presets = list_build_presets(projectDir);
    if (presets.empty())
    {
      if (configurePreset == "dev-ninja")
        return "build-ninja";
      if (configurePreset == "dev-ninja-san")
        return "build-ninja-san";
      if (configurePreset == "dev-ninja-ubsan")
        return "build-ninja-ubsan";
      return configurePreset;
    }

    if (configurePreset == "dev-ninja")
    {
      if (contains_preset(presets, "build-ninja"))
        return "build-ninja";
      if (contains_preset(presets, "build-dev-ninja"))
        return "build-dev-ninja";
      if (contains_preset(presets, "dev-ninja"))
        return "dev-ninja";
      return "build-ninja";
    }

    if (configurePreset == "dev-ninja-san")
    {
      if (contains_preset(presets, "build-ninja-san"))
        return "build-ninja-san";
      if (contains_preset(presets, "build-dev-ninja-san"))
        return "build-dev-ninja-san";
      if (contains_preset(presets, "dev-ninja-san"))
        return "dev-ninja-san";
      return "build-ninja-san";
    }

    if (configurePreset == "dev-ninja-ubsan")
    {
      if (contains_preset(presets, "build-ninja-ubsan"))
        return "build-ninja-ubsan";
      if (contains_preset(presets, "build-dev-ninja-ubsan"))
        return "build-dev-ninja-ubsan";
      if (contains_preset(presets, "dev-ninja-ubsan"))
        return "dev-ninja-ubsan";
      return "build-ninja-ubsan";
    }

    {
      const std::string a = "build-" + configurePreset;
      if (contains_preset(presets, a))
        return a;
      if (contains_preset(presets, configurePreset))
        return configurePreset;
    }

    return configurePreset;
  }

  static std::string guess_project_name_from_dir(const fs::path &projectDir)
  {
    // blog/ -> "blog"
    std::string name = projectDir.filename().string();
    if (name.empty())
      name = "app";
    return name;
  }

  static std::string read_cmake_cache_value(const fs::path &cacheFile, const std::string &key)
  {
    std::ifstream in(cacheFile);
    if (!in.is_open())
      return {};

    std::string line;
    const std::string prefix = key + ":";
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

    // Most common (Ninja/Make single-config)
    candidates.push_back(buildDir / exeName);
    candidates.push_back(buildDir / "bin" / exeName);

    // Multi-config layouts (MSVC-like)
    candidates.push_back(buildDir / configName / exeName);
    candidates.push_back(buildDir / "bin" / configName / exeName);

    // Some projects put outputs under src/
    candidates.push_back(buildDir / "src" / exeName);
    candidates.push_back(buildDir / "src" / configName / exeName);

    for (const auto &c : candidates)
    {
      std::error_code ec;
      if (fs::exists(c, ec) && !ec)
        return c;
    }

    // Fallback: read runtime output dir from cache (if set)
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

        fs::path c = base / exeName;
        std::error_code ec;
        if (fs::exists(c, ec) && !ec)
          return c;
      }
    }

    return buildDir / exeName;
  }

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

  int check_project(const Options &opt, const fs::path &projectDir)
  {
    apply_log_level_env_local(opt);

    // 1) configure (presets)
    if (has_presets(projectDir))
    {
      info("Checking project using CMake presets...");
      step("Project: " + projectDir.string());

      // 0) resolve preset from flags
      std::string preset = opt.preset;

      if (opt.enableSanitizers)
      {
        if (preset == "dev-ninja")
          preset = "dev-ninja-san";
      }
      else if (opt.enableUbsanOnly)
      {
        if (preset == "dev-ninja")
          preset = "dev-ninja-ubsan";
      }

      step("Preset: " + preset);

      // 1) build dir (from configure preset)
      fs::path buildDir = guess_build_dir_from_configure_preset(projectDir, preset);

      // Legacy fallback: older generated projects used build-dev-*
      if (!has_cmake_cache(buildDir) && preset.rfind("dev-", 0) == 0)
      {
        const fs::path legacy = projectDir / ("build-dev-" + preset.substr(4)); // dev-ninja -> build-dev-ninja
        if (has_cmake_cache(legacy))
          buildDir = legacy;
      }

      // 2) configure only if cache missing (for the chosen buildDir)
      if (!has_cmake_cache(buildDir))
      {
        info("No CMake cache detected for preset — configuring...");
        step("Build dir: " + buildDir.string());

        std::ostringstream conf;
#ifdef _WIN32
        conf << "cmd /C \"cd /D " << quote(projectDir.string())
             << " && cmake --preset " << quote(preset) << "\"";
        int code = 0;
        std::string confLog = run_and_capture_with_code(conf.str(), code);
        if (code != 0)
        {
          if (!confLog.empty())
            std::cout << confLog;
          error("CMake configure failed (preset '" + preset + "').");
          return code != 0 ? code : 2;
        }
#else
        conf << "cd " << quote(projectDir.string())
             << " && cmake --preset " << quote(preset);

        const int code = run_cmd_live_filtered(
            conf.str(),
            "Configuring project (preset \"" + preset + "\")");

        if (code != 0)
        {
          error("CMake configure failed (preset '" + preset + "').");
          hint("Run manually:");
          step("cd " + projectDir.string());
          step("cmake --preset " + preset);
          return code != 0 ? code : 2;
        }
#endif

        success("Configure OK.");
      }
      else
      {
        success("CMake cache detected — skipping configure.");
        step("Build dir: " + buildDir.string());
      }

      // 3) choose build preset
      const std::string buildPreset =
          pick_build_preset_smart(projectDir, preset, opt.buildPreset);

      // 4) build
      std::ostringstream b;
#ifdef _WIN32
      b << "cmd /C \"cd /D " << quote(projectDir.string())
        << " && cmake --build --preset " << quote(buildPreset);
      if (opt.jobs > 0)
        b << " -- -j " << opt.jobs;
      b << "\"";

      int code = 0;
      std::string buildLog = run_and_capture_with_code(b.str(), code);
      if (code != 0)
      {
        if (!buildLog.empty())
          vix::cli::ErrorHandler::printBuildErrors(buildLog, projectDir, "Project check failed (build, presets)");
        else
          error("Project check failed (build, presets).");
        return code != 0 ? code : 3;
      }
#else
      b << "cd " << quote(projectDir.string())
        << " && cmake --build --preset " << quote(buildPreset) << " --target all";
      if (opt.jobs > 0)
        b << " -- -j " << opt.jobs;

      const int code = run_cmd_live_filtered(
          b.str(),
          "Checking build (preset \"" + buildPreset + "\")");

      if (code != 0)
      {
        error("Project check failed (build, presets).");
        hint("Run manually:");
        step("cd " + projectDir.string());
        step("cmake --build --preset " + buildPreset);
        return code != 0 ? code : 3;
      }
#endif

      // 5) tests (prefer CTest preset if available, else fallback to build dir)
      if (opt.tests)
      {
#ifndef _WIN32
        const fs::path ctestPresets = projectDir / "CTestPresets.json";
        const bool hasCTestPresets = fs::exists(ctestPresets);

        auto wants_listing_only = [&]() -> bool
        {
          for (const auto &x : opt.ctestArgs)
          {
            if (x == "--show-only" || x == "-N" || x == "--show-only=json-v1")
              return true;
          }
          return false;
        };

        const bool listingOnly = wants_listing_only();

        auto append_ctest_args = [&](std::ostringstream &os)
        {
          for (const auto &x : opt.ctestArgs)
            os << " " << quote(x);
        };

        // 1) Try preset route if file exists (or user explicitly forced a ctest preset)
        if (hasCTestPresets || !opt.ctestPreset.empty())
        {
          const std::string ctp = opt.ctestPreset.empty()
                                      ? ("test-" + preset)
                                      : opt.ctestPreset;

          std::ostringstream cmd;
          cmd << "cd " << quote(projectDir.string())
              << " && ctest --preset " << quote(ctp);

          if (!listingOnly)
            cmd << " --output-on-failure";

          append_ctest_args(cmd);

          int tcode = run_cmd_live_filtered(cmd.str(), "Running tests");

          // If preset failed, fallback to build dir (common: preset doesn't exist)
          if (tcode != 0)
          {
            hint("CTest preset failed — falling back to build directory.");

            std::ostringstream fb;
            fb << "cd " << quote(buildDir.string())
               << " && ctest";

            if (!listingOnly)
              fb << " --output-on-failure";

            append_ctest_args(fb);

            tcode = run_cmd_live_filtered(fb.str(), "Running tests (fallback)");

            if (tcode != 0)
            {
              error("Tests failed (ctest).");
              return tcode != 0 ? tcode : 4;
            }
          }
        }
        else
        {
          // 2) No CTestPresets.json → run directly from build dir (works in your blog)
          std::ostringstream fb;
          fb << "cd " << quote(buildDir.string())
             << " && ctest";

          if (!listingOnly)
            fb << " --output-on-failure";

          append_ctest_args(fb);

          const int tcode = run_cmd_live_filtered(fb.str(), "Running tests");
          if (tcode != 0)
          {
            error("Tests failed (ctest).");
            return tcode != 0 ? tcode : 4;
          }
        }
#endif
      }

#ifndef _WIN32
      if (opt.runAfterBuild)
      {
        const std::string projectName = guess_project_name_from_dir(projectDir);
        const std::string configName = resolve_build_type_from_cache_or_default(buildDir, "Debug");
        const fs::path exePath = compute_runtime_executable_path(buildDir, projectName, configName);
        const int timeoutSec = (opt.runTimeoutSec > 0) ? opt.runTimeoutSec : 15;

        std::ostringstream r;
        r << "cd " << quote(buildDir.string()) << " && " << quote(exePath.string());

        auto rr = run_cmd_live_filtered_capture(
            r.str(),
            "Checking runtime (" + exePath.filename().string() + ")",
            /*passthroughRuntime*/ false,
            /*timeoutSec*/ timeoutSec);

        if (rr.exitCode != 0)
        {
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

          bool handled = false;
          if (!runtimeLog.empty())
          {
            handled = vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                runtimeLog, projectDir, "Project check failed (runtime sanitizers)");

            if (!handled &&
                vix::cli::errors::RawLogDetectors::handleKnownRunFailure(runtimeLog, projectDir))
              handled = true;
          }

          handle_runtime_exit_code(
              rr.exitCode,
              "Project check failed (runtime sanitizers)",
              /*alreadyHandled=*/handled);

          return rr.exitCode;
        }

        success("✔ Runtime check OK.");
      }
#endif

      success("Project check OK (built).");
      return 0;
    }

    // Fallback build/ (no presets)
    info("Checking project (fallback build/)...");

    fs::path buildDir = projectDir / "build";
    std::error_code ec;
    fs::create_directories(buildDir, ec);
    if (ec)
    {
      error("Unable to create build directory: " + ec.message());
      return 1;
    }

    if (!has_cmake_cache(buildDir))
    {
      std::ostringstream c;
#ifdef _WIN32
      c << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake ..\"";
      int ccode = 0;
      std::string clog = run_and_capture_with_code(c.str(), ccode);
      if (ccode != 0)
      {
        if (!clog.empty())
          std::cout << clog;
        error("CMake configure failed (fallback).");
        return ccode != 0 ? ccode : 2;
      }
#else
      c << "cd " << quote(buildDir.string()) << " && cmake ..";
      const int ccode = run_cmd_live_filtered(c.str(), "Configuring (fallback)");
      if (ccode != 0)
      {
        error("CMake configure failed (fallback).");
        return ccode != 0 ? ccode : 2;
      }
#endif
    }

    std::ostringstream b2;
#ifdef _WIN32
    b2 << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake --build .";
    if (opt.jobs > 0)
      b2 << " -- -j " << opt.jobs;
    b2 << "\"";
#else
    b2 << "cd " << quote(buildDir.string()) << " && cmake --build .";
    if (opt.jobs > 0)
      b2 << " -- -j " << opt.jobs;
#endif

    int code = 0;
    std::string log = run_and_capture_with_code(b2.str(), code);
    if (code != 0)
    {
      if (!log.empty())
      {
        vix::cli::ErrorHandler::printBuildErrors(
            log, buildDir, "Project check failed (fallback build/)");
      }
      else
      {
        error("Build failed (fallback).");
      }
      return code != 0 ? code : 3;
    }

    if (opt.tests)
    {
#ifndef _WIN32
      std::ostringstream t;
      t << "cd " << quote(buildDir.string()) << " && ctest --output-on-failure";

      for (const auto &x : opt.ctestArgs)
        t << " " << quote(x);

      const int tcode = run_cmd_live_filtered(t.str(), "Running tests");
      if (tcode != 0)
      {
        error("Tests failed.");
        return tcode != 0 ? tcode : 4;
      }
#endif
    }

    success("Project check OK (fallback configured + built).");
    return 0;
  }

} // namespace vix::commands::CheckCommand::detail
