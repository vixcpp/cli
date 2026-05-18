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
#include <vix/cli/cmake/CMakeBuild.hpp>
#include <vix/cli/ErrorHandler.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/commands/run/RunScriptHelpers.hpp>

#include <sstream>
#include <string>
#include <system_error>
#include <fstream>
#include <thread>
#include <vector>

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

    std::string read_text_file_or_empty(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};

      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }

    std::string make_vix_dev_build_command(
        const DevRebuilderOptions &options)
    {
      const detail::Options &opt = options.runOptions;

      std::ostringstream oss;

#ifndef _WIN32
      oss << "cd "
          << detail::quote(options.projectDir.string())
          << " && vix build"
          << " --build-target all"
          << " --fast";
#else
      oss << "cd /D "
          << detail::quote(options.projectDir.string())
          << " && vix build"
          << " --build-target all"
          << " --fast";
#endif

      if (opt.jobs > 0)
        oss << " -j " << opt.jobs;

      if (opt.verbose)
        oss << " -v";

      if (opt.withSqlite)
        oss << " --with-sqlite";

      if (opt.withMySql)
        oss << " --with-mysql";

      return oss.str();
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
    DevRebuilderResult result;
    result.built = true;

    std::string buildCmd = make_vix_dev_build_command(options_);

#ifndef _WIN32
    std::vector<std::string> argv = {
        "sh",
        "-lc",
        buildCmd};
#else
    std::vector<std::string> argv = {
        "cmd",
        "/C",
        buildCmd};
#endif

    {
      std::error_code ec;
      fs::create_directories(options_.buildDir, ec);
    }

    const fs::path logPath = options_.buildDir / "dev-build.log";

    const auto r = vix::cli::build::run_process_live_to_log(
        argv,
        {},
        logPath,
        options_.quiet,
        false,
        true);

    result.exitCode = r.exitCode;
    result.ok = r.exitCode == 0;

    if (!result.ok)
    {
      const std::string log = read_text_file_or_empty(logPath);

      bool handled = false;

      if (!log.empty())
      {
        handled = vix::cli::ErrorHandler::printBuildErrors(
            log,
            options_.buildDir,
            "Build failed in dev mode");
      }

      if (!handled)
        error("Build failed in dev mode.");

      return result;
    }

    result.message = "Build completed.";
    return result;
  }
  DevRebuilderResult DevRebuilder::reconfigure_and_rebuild() const
  {
    if (!options_.quiet)
      info("Configuration change detected.");

    return rebuild();
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

    if (!options_.quiet && options_.runOptions.verbose)
    {
      std::cout << CYAN << BOLD << "Configuring " << RESET
                << CYAN << BOLD << options_.targetName << RESET
                << GRAY << " (dev)" << RESET
                << "\n";
    }

    const std::string cmd = configure_command();

    int rawCode = 0;
    result.output = detail::run_and_capture_with_code(cmd + " 2>&1", rawCode);
    result.exitCode = detail::normalize_exit_code(rawCode);
    result.ok = result.exitCode == 0;

    if (!result.ok)
    {
      result.message =
          "CMake configure failed for dev mode (" +
          build_label(options_.buildDir) +
          ", code " + std::to_string(result.exitCode) + ").";

      error(result.message);

      if (!result.output.empty())
        std::cerr << result.output << "\n";

      hint("Check your CMakeLists.txt or run the command manually:");
      step("  " + cmd);
      return result;
    }

    result.message = "CMake configure completed.";

    if (!options_.quiet)
    {
      std::cout << "  "
                << GREEN << "✔" << RESET
                << " Configured"
                << "\n";
    }

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
