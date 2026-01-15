/**
 *
 *  @file CheckScript.cpp
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
#include <vix/cli/commands/helpers/TextHelpers.hpp>
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/commands/run/RunScriptHelpers.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/ErrorHandler.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

using namespace vix::cli::style;

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;

  namespace text = vix::cli::commands::helpers;
  using vix::cli::commands::helpers::quote;

  namespace run = vix::commands::RunCommand::detail;

#ifndef _WIN32
  static inline const char *null_redirect() noexcept
  {
    return " >/dev/null 2>&1";
  }
#else
  static inline const char *null_redirect() noexcept
  {
    return " >nul 2>nul";
  }
#endif

  static bool write_file_or_report(const fs::path &path, const std::string &content)
  {
    std::ofstream ofs(path);
    if (!ofs)
    {
      error("Failed to write file: " + path.string());
      return false;
    }
    ofs << content;
    if (!ofs)
    {
      error("Failed to write file (I/O error): " + path.string());
      return false;
    }
    return true;
  }

  int check_single_cpp(const Options &opt)
  {
    const fs::path script = opt.cppFile;

    if (script.empty())
    {
      error("No C++ file provided.");
      return 1;
    }
    if (!fs::exists(script))
    {
      error("C++ file not found: " + script.string());
      return 1;
    }

    const std::string exeName = script.stem().string();

    fs::path scriptsRoot = run::get_scripts_root();
    fs::create_directories(scriptsRoot);
    fs::path projectDir = scriptsRoot / exeName;
    fs::create_directories(projectDir);
    fs::path cmakeLists = projectDir / "CMakeLists.txt";
    const bool useVixRuntime = run::script_uses_vix(script);

    if (!write_file_or_report(cmakeLists, run::make_script_cmakelists(exeName, script, useVixRuntime, /*scriptFlags=*/{})))
      return 1;

    fs::path buildDir = projectDir / "build";
    const fs::path sigFile = projectDir / ".vix-config.sig";
    const bool enableSan = opt.enableSanitizers;
    const bool enableUbsanOnly = opt.enableUbsanOnly;

    const std::string sig =
        run::make_script_config_signature(useVixRuntime, enableSan, enableUbsanOnly, /*scriptFlags=*/{});
    ;

    bool needConfigure = true;
    {
      std::error_code ec{};
      if (fs::exists(buildDir / "CMakeCache.txt", ec) && !ec)
      {
        const std::string oldSig = text::read_text_file_or_empty(sigFile);
        if (!oldSig.empty() && oldSig == sig)
          needConfigure = false;
      }
    }

    if (needConfigure)
    {
      std::ostringstream oss;
      oss << "cd " << quote(projectDir.string()) << " && cmake -S . -B build";

      if (run::want_sanitizers(enableSan, enableUbsanOnly))
      {
        oss << " -DVIX_ENABLE_SANITIZERS=ON"
            << " -DVIX_SANITIZER_MODE=" << run::sanitizer_mode_string(enableSan, enableUbsanOnly);
      }
      else
      {
        oss << " -DVIX_ENABLE_SANITIZERS=OFF";
      }

      oss << null_redirect();

      const int code = run::normalize_exit_code(std::system(oss.str().c_str()));
      if (code != 0)
      {
        error("Script configure failed.");
        hint("Try running the configure command manually inside: " + projectDir.string());
        return code;
      }

      (void)text::write_text_file(sigFile, sig);
    }

    fs::path logPath = projectDir / "build.log";

    std::ostringstream b;
#ifndef _WIN32
    b << "cd " << quote(projectDir.string())
      << " && cmake --build build --target " << exeName;
    if (opt.jobs > 0)
      b << " -- -j " << opt.jobs;
    b << " >" << quote(logPath.string()) << " 2>&1";
#else
    b << "cd " << quote(projectDir.string())
      << " && cmake --build build --target " << exeName
      << " >" << quote(logPath.string()) << " 2>&1";
#endif

    const int buildCode = run::normalize_exit_code(std::system(b.str().c_str()));
    if (buildCode != 0)
    {
      const std::string log = text::read_text_file_or_empty(logPath);

      if (!log.empty())
      {
        vix::cli::ErrorHandler::printBuildErrors(
            log, script, "Script check failed (build)");
      }
      else
      {
        error("Script check failed (no compiler log captured).");
        hint("No build log found at: " + logPath.string());
      }

      return buildCode;
    }

#ifndef _WIN32
    // If sanitizers requested, run the binary to actually trigger UBSan/ASan
    if (run::want_sanitizers(enableSan, enableUbsanOnly))
    {
      fs::path exePath = buildDir / exeName;

      if (!fs::exists(exePath))
      {
        error("Script binary not found: " + exePath.string());
        hint("Try rebuilding: cmake --build build --target " + exeName);
        return 1;
      }

      // Apply runtime env (UBSAN_OPTIONS / ASAN_OPTIONS)
      run::apply_sanitizer_env_if_needed(enableSan, enableUbsanOnly);

      const std::string cmdRun =
          "VIX_STDOUT_MODE=line " + run::quote(exePath.string());

      auto rr = run::run_cmd_live_filtered_capture(
          cmdRun,
          "Checking runtime (sanitizers)",
          /*passthroughRuntime*/ false);

      // rr.exitCode is already normalized in run_cmd_live_filtered_capture()
      const int code = rr.exitCode;

      if (code != 0)
      {
        const std::string runtimeLog = rr.stdoutText + "\n" + rr.stderrText;

        vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
            runtimeLog, script, "Script check failed (runtime sanitizers)");

        run::handle_runtime_exit_code(code, "Script check failed (runtime sanitizers)");
        return code;
      }

      success("Script check OK (compiled + runtime sanitizers passed).");
      return 0;
    }
#endif

    success("Script check OK (compiled successfully).");
    return 0;
  }

} // namespace vix::commands::CheckCommand::detail
