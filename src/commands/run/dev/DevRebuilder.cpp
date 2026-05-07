/**
 *
 *  @file DevRebuilder.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Dev mode project rebuilder
 *
 */

#include <vix/cli/commands/run/dev/DevRebuilder.hpp>

#include <vix/cli/ErrorHandler.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/commands/run/RunScriptHelpers.hpp>

#include <sstream>
#include <string>
#include <system_error>

using namespace vix::cli::style;

namespace vix::commands::RunCommand::dev
{
  namespace
  {
    std::string build_label(const fs::path &buildDir)
    {
      const std::string name = buildDir.filename().string();

      if (!name.empty())
        return name + "/";

      return buildDir.string();
    }
  } // namespace

  DevRebuilder::DevRebuilder(DevRebuilderOptions options)
      : options_(std::move(options))
  {
  }

  const DevRebuilderOptions &DevRebuilder::options() const
  {
    return options_;
  }

  DevRebuilderResult DevRebuilder::ensure_configured() const
  {
    if (has_cmake_cache())
    {
      DevRebuilderResult result;
      result.ok = true;
      result.configured = false;
      result.exitCode = 0;
      result.message = "CMake cache already exists.";
      return result;
    }

    return run_configure_command();
  }

  DevRebuilderResult DevRebuilder::rebuild() const
  {
    DevRebuilderResult configured = ensure_configured();
    if (!configured.ok)
      return configured;

    return run_build_command();
  }

  DevRebuilderResult DevRebuilder::reconfigure_and_rebuild() const
  {
    DevRebuilderResult configured = run_configure_command();
    if (!configured.ok)
      return configured;

    return run_build_command();
  }

  bool DevRebuilder::has_cmake_cache() const
  {
    std::error_code ec;
    return fs::exists(options_.buildDir / "CMakeCache.txt", ec) && !ec;
  }

  std::string DevRebuilder::configure_command() const
  {
    const detail::Options &opt = options_.runOptions;

    std::ostringstream oss;

    oss << "cmake"
        << " -S " << detail::quote(options_.projectDir.string())
        << " -B " << detail::quote(options_.buildDir.string())
        << " -G Ninja"
        << " -DCMAKE_BUILD_TYPE=Debug"
        << " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";

    if (opt.withSqlite)
    {
      oss << " -DVIX_ENABLE_DB=ON"
          << " -DVIX_DB_USE_SQLITE=ON";
    }

    if (opt.withMySql)
    {
      oss << " -DVIX_ENABLE_DB=ON"
          << " -DVIX_DB_USE_MYSQL=ON"
          << " -DVIX_DB_REQUIRE_MYSQL=ON";
    }

    if (detail::want_any_sanitizer(
            opt.enableSanitizers,
            opt.enableUbsanOnly,
            opt.enableThreadSanitizer))
    {
      oss << " -DVIX_ENABLE_SANITIZERS=ON"
          << " -DVIX_SANITIZER_MODE="
          << detail::sanitizer_mode_string(
                 opt.enableSanitizers,
                 opt.enableUbsanOnly,
                 opt.enableThreadSanitizer);
    }
    else
    {
      oss << " -DVIX_ENABLE_SANITIZERS=OFF";
    }

    return oss.str();
  }

  std::string DevRebuilder::build_command() const
  {
    const detail::Options &opt = options_.runOptions;

    std::ostringstream oss;

    oss << "cmake --build " << detail::quote(options_.buildDir.string())
        << " --target " << detail::quote(options_.targetName);

    if (fs::exists(options_.buildDir / "build.ninja"))
    {
      oss << " --";

      if (opt.jobs > 0)
        oss << " -j " << opt.jobs;

      oss << " --quiet";
    }
    else if (opt.jobs > 0)
    {
      oss << " -j " << opt.jobs;
    }

    return oss.str();
  }

  DevRebuilderResult DevRebuilder::run_configure_command() const
  {
    DevRebuilderResult result;
    result.configured = true;

    if (!options_.quiet)
      info("Configuring project for dev mode (" + build_label(options_.buildDir) + ").");

    const std::string cmd = configure_command();

    const int code = detail::run_cmd_live_filtered(
        cmd,
        "Configuring project");

    result.exitCode = detail::normalize_exit_code(code);
    result.ok = result.exitCode == 0;

    if (!result.ok)
    {
      result.message =
          "CMake configure failed for dev mode (" +
          build_label(options_.buildDir) +
          ", code " + std::to_string(result.exitCode) + ").";

      error(result.message);
      hint("Check your CMakeLists.txt or run the command manually:");
      step("  " + cmd);
      return result;
    }

    result.message = "CMake configure completed.";
    return result;
  }

  DevRebuilderResult DevRebuilder::run_build_command() const
  {
    DevRebuilderResult result;
    result.built = true;

    detail::watch_spinner_start("Rebuilding project...");

    const std::string cmd = build_command();

    int rawCode = 0;
    result.output = detail::run_and_capture_with_code(cmd + " 2>&1", rawCode);
    result.exitCode = detail::normalize_exit_code(rawCode);
    result.ok = result.exitCode == 0;

    detail::watch_spinner_pause_for_output();

    if (!result.ok)
    {
      const std::string label = build_label(options_.buildDir);

      if (!result.output.empty())
      {
        (void)vix::cli::ErrorHandler::printBuildErrors(
            result.output,
            options_.buildDir,
            "Build failed in dev mode (" + label + ")");
      }
      else
      {
        error("Build failed in dev mode (" +
              label +
              ", code " +
              std::to_string(result.exitCode) +
              ").");
      }

      result.message = "Build failed.";
      return result;
    }

    result.message = "Build completed.";
    return result;
  }

} // namespace vix::commands::RunCommand::dev
