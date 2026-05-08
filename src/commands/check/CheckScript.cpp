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
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/build/BuildStyle.hpp>

#include <chrono>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;
  namespace text = vix::cli::commands::helpers;
  namespace run = vix::commands::RunCommand::detail;
  namespace ui = vix::cli::util;
  namespace style = vix::cli::style;
  namespace build = vix::cli::build;

  using vix::cli::commands::helpers::quote;

  namespace
  {
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

    struct ScriptCheckSummary
    {
      bool configured = false;
      bool built = false;
      bool runtimeRan = false;
      bool sanitizersEnabled = false;
      bool ubsanOnly = false;
    };

    static bool write_file_or_report(const fs::path &path, const std::string &content)
    {
      std::ofstream ofs(path);
      if (!ofs)
      {
        style::error("Failed to write file: " + path.string());
        return false;
      }

      ofs << content;
      if (!ofs)
      {
        style::error("Failed to write file (I/O error): " + path.string());
        return false;
      }

      return true;
    }

    static std::string make_summary(const ScriptCheckSummary &summary)
    {
      std::vector<std::string> parts;

      if (summary.built)
        parts.push_back("build");

      if (summary.runtimeRan)
        parts.push_back("runtime");

      if (summary.sanitizersEnabled)
      {
        if (summary.ubsanOnly)
          parts.push_back("ubsan");
        else
          parts.push_back("asan+ubsan");
      }

      if (parts.empty())
        return "nothing";

      std::ostringstream os;
      for (std::size_t i = 0; i < parts.size(); ++i)
      {
        if (i > 0)
          os << ", ";
        os << parts[i];
      }

      return os.str();
    }

    static std::string profile_name(bool san, bool ubsanOnly)
    {
      if (san)
        return "asan+ubsan";
      if (ubsanOnly)
        return "ubsan";
      return "default";
    }

    static std::string build_dir_name(bool san, bool ubsanOnly)
    {
      if (san)
        return "build-san";
      if (ubsanOnly)
        return "build-ubsan";
      return "build";
    }

    static int effective_jobs(const Options &opt)
    {
      if (opt.jobs > 0)
        return opt.jobs;

      unsigned int hc = std::thread::hardware_concurrency();

      if (hc == 0)
        return 4;

      if (hc > 64)
        hc = 64;

      return static_cast<int>(hc);
    }

    static std::string script_display_name(const fs::path &script)
    {
      std::string name = script.filename().string();

      if (name.empty())
        return "script";

      return name;
    }

    static bool script_runtime_enabled(
        bool enableSan,
        bool enableUbsanOnly,
        bool enableThreadSanitizer)
    {
#ifndef _WIN32
      return run::want_any_sanitizer(
          enableSan,
          enableUbsanOnly,
          enableThreadSanitizer);
#else
      (void)enableSan;
      (void)enableUbsanOnly;
      (void)enableThreadSanitizer;
      return false;
#endif
    }

    static void print_script_check_header(
        const Options &opt,
        const fs::path &script,
        bool runtimeEnabled)
    {
      if (opt.quiet)
        return;

      std::vector<std::pair<std::string, std::string>> meta;
      meta.emplace_back("profile", profile_name(opt.enableSanitizers, opt.enableUbsanOnly));
      meta.emplace_back("runtime", runtimeEnabled ? "on" : "off");
      meta.emplace_back("jobs", std::to_string(effective_jobs(opt)));

      build::print_task_header_full(
          std::cout,
          "Checking",
          script_display_name(script),
          "script",
          meta);
    }

    static void print_script_check_progress(
        const std::string &label,
        bool success)
    {
      const std::string status = success ? "done" : "failed";
      const char *lineColor = success ? style::CYAN : style::RED;
      const char *statusColor = success ? style::GREEN : style::RED;

      std::cout << "  "
                << lineColor
                << label
                << style::RESET
                << " "
                << lineColor
                << "[============================]"
                << style::RESET
                << " "
                << statusColor
                << status
                << style::RESET
                << "\n";
    }

    static long long elapsed_ms_since(std::chrono::steady_clock::time_point start)
    {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - start)
          .count();
    }

    static void print_verbose_script_context(
        const Options &opt,
        const fs::path &script,
        const fs::path &projectDir,
        const fs::path &buildDir,
        bool runtimeEnabled)
    {
      if (opt.quiet || !opt.verbose)
        return;

      ui::one_line_spacer(std::cout);
      ui::section(std::cout, "Check details");
      ui::kv(std::cout, "script", script.string());
      ui::kv(std::cout, "project dir", projectDir.string());
      ui::kv(std::cout, "build dir", buildDir.string());
      ui::kv(std::cout, "profile", profile_name(opt.enableSanitizers, opt.enableUbsanOnly));
      ui::kv(std::cout, "runtime", runtimeEnabled ? "enabled" : "disabled");
      ui::one_line_spacer(std::cout);
    }

    static void print_header(
        const Options &opt,
        const fs::path &script,
        const fs::path &projectDir,
        const fs::path &buildDir,
        bool runtimeEnabled)
    {
      print_script_check_header(opt, script, runtimeEnabled);
      print_verbose_script_context(opt, script, projectDir, buildDir, runtimeEnabled);
    }
  } // namespace

  int check_single_cpp(const Options &opt)
  {
    const fs::path script = opt.cppFile;
    const auto checkStart = std::chrono::steady_clock::now();

    if (script.empty())
    {
      style::error("No C++ file provided.");
      return 1;
    }

    if (!fs::exists(script))
    {
      style::error("C++ file not found: " + script.string());
      return 1;
    }

    ScriptCheckSummary summary;
    summary.sanitizersEnabled = run::want_any_sanitizer(
        opt.enableSanitizers,
        opt.enableUbsanOnly,
        opt.enableThreadSanitizer);
    summary.ubsanOnly = opt.enableUbsanOnly;

    const bool enableSan = opt.enableSanitizers;
    const bool enableUbsanOnly = opt.enableUbsanOnly;
    const bool enableThreadSanitizer = opt.enableThreadSanitizer;
    const std::string exeName = script.stem().string();

    fs::path scriptsRoot = run::get_scripts_root(opt.localCache);
    std::error_code ec;
    fs::create_directories(scriptsRoot, ec);
    if (ec)
    {
      style::error("Failed to create scripts root: " + scriptsRoot.string());
      style::hint(ec.message());
      return 1;
    }

    fs::path projectDir = scriptsRoot / exeName;
    fs::create_directories(projectDir, ec);
    if (ec)
    {
      style::error("Failed to create script project dir: " + projectDir.string());
      style::hint(ec.message());
      return 1;
    }

    const fs::path cmakeLists = projectDir / "CMakeLists.txt";
    const bool useVixRuntime = run::script_uses_vix(script);
    const fs::path buildDir = projectDir / build_dir_name(enableSan, enableUbsanOnly);
    const fs::path sigFile = projectDir / (build_dir_name(enableSan, enableUbsanOnly) + ".vix-config.sig");
    const fs::path logPath = projectDir / (build_dir_name(enableSan, enableUbsanOnly) + ".build.log");

    const bool runtimeEnabled =
        script_runtime_enabled(enableSan, enableUbsanOnly, enableThreadSanitizer);

    print_header(opt, script, projectDir, buildDir, runtimeEnabled);

    if (!write_file_or_report(
            cmakeLists,
            run::make_script_cmakelists(
                exeName,
                script,
                useVixRuntime,
                /*scriptFlags=*/{},
                false,
                false)))
    {
      return 1;
    }

    const std::string sig =
        run::make_script_config_signature(
            useVixRuntime,
            enableSan,
            enableUbsanOnly,
            enableThreadSanitizer,
            /*scriptFlags=*/{},
            opt.withSqlite,
            opt.withMySql);

    bool needConfigure = true;
    {
      std::error_code cacheEc;
      if (fs::exists(buildDir / "CMakeCache.txt", cacheEc) && !cacheEc)
      {
        const std::string oldSig = text::read_text_file_or_empty(sigFile);
        if (!oldSig.empty() && oldSig == sig)
          needConfigure = false;
      }
    }

    if (needConfigure)
    {
      if (!opt.quiet && opt.verbose)
      {
        ui::info_line(std::cout, "No matching cache found for this script profile.");
        ui::kv(std::cout, "action", "configure");
        ui::one_line_spacer(std::cout);
      }

      std::ostringstream conf;
      conf << "cd " << quote(projectDir.string())
           << " && cmake -S . -B " << quote(buildDir.string());

      if (run::want_sanitizers(enableSan, enableUbsanOnly))
      {
        conf << " -DVIX_ENABLE_SANITIZERS=ON"
             << " -DVIX_SANITIZER_MODE="
             << run::sanitizer_mode_string(enableSan, enableUbsanOnly, enableThreadSanitizer);
      }
      else
      {
        conf << " -DVIX_ENABLE_SANITIZERS=OFF";
      }

      conf << null_redirect();

      const int code = run::normalize_exit_code(std::system(conf.str().c_str()));
      if (code != 0)
      {
        style::error("Script configure failed.");
        style::hint("Try running the configure command manually inside: " + projectDir.string());
        return code;
      }

      (void)text::write_text_file(sigFile, sig);
      summary.configured = true;

      if (!opt.quiet)
        print_script_check_progress("configure", true);
    }
    else
    {
      if (!opt.quiet && opt.verbose)
      {
        ui::ok_line(std::cout, "Matching cache detected for this script profile.");
        ui::kv(std::cout, "build dir", buildDir.string());
        ui::one_line_spacer(std::cout);
      }
    }

    if (!opt.quiet && opt.verbose)
    {
      ui::info_line(std::cout, "Starting build.");
      ui::kv(std::cout, "target", exeName);
      ui::one_line_spacer(std::cout);
    }

    std::ostringstream buildCmd;
#ifndef _WIN32
    buildCmd << "cd " << quote(projectDir.string())
             << " && cmake --build " << quote(buildDir.string())
             << " -- -j " << effective_jobs(opt)
             << " >" << quote(logPath.string()) << " 2>&1";
#else
    buildCmd << "cd " << quote(projectDir.string())
             << " && cmake --build " << quote(buildDir.string())
             << " -- -j " << effective_jobs(opt)
             << " >" << quote(logPath.string()) << " 2>&1";
#endif

    const int buildCode = run::normalize_exit_code(std::system(buildCmd.str().c_str()));
    if (buildCode != 0)
    {
      if (!opt.quiet)
        print_script_check_progress("build", false);

      const std::string log = text::read_text_file_or_empty(logPath);

      if (!log.empty())
      {
        vix::cli::ErrorHandler::printBuildErrors(
            log,
            script,
            "Script check failed (build)");
      }
      else
      {
        style::error("Script check failed (no compiler log captured).");
        style::hint("No build log found at: " + logPath.string());
      }

      return buildCode;
    }

    summary.built = true;

    if (!opt.quiet)
      print_script_check_progress("build", true);

#ifndef _WIN32
    if (run::want_any_sanitizer(
            enableSan,
            enableUbsanOnly,
            enableThreadSanitizer))
    {
      if (!opt.quiet && opt.verbose)
      {
        ui::one_line_spacer(std::cout);
        ui::info_line(std::cout, "Running runtime validation.");
      }

      const fs::path exePath = buildDir / exeName;

      if (!fs::exists(exePath))
      {
        style::error("Script binary not found: " + exePath.string());
        style::hint("Try rebuilding: cmake --build " + buildDir.string() + " --target " + exeName);
        return 1;
      }

      run::apply_sanitizer_env_if_needed(
          enableSan,
          enableUbsanOnly,
          enableThreadSanitizer);

      const std::string cmdRun =
          "VIX_STDOUT_MODE=line " + run::quote(exePath.string());

      auto rr = run::run_cmd_live_filtered_capture(
          cmdRun,
          "Checking runtime (sanitizers)",
          /*passthroughRuntime=*/false);

      const int code = rr.exitCode;

      if (code != 0)
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
              runtimeLog,
              script,
              "Script check failed (runtime sanitizers)");

          if (!handled &&
              vix::cli::errors::RawLogDetectors::handleKnownRunFailure(runtimeLog, script))
          {
            handled = true;
          }
        }

        run::handle_runtime_exit_code(
            code,
            "Script check failed (runtime sanitizers)",
            /*alreadyHandled=*/handled);

        return code;
      }

      summary.runtimeRan = true;

      if (!opt.quiet)
        print_script_check_progress("runtime", true);
    }
#endif

    const long long totalMs = elapsed_ms_since(checkStart);

    if (!opt.quiet)
    {
      build::print_task_success_timed(
          std::cout,
          "Script check OK (" + make_summary(summary) + ")",
          totalMs);
    }
    else
    {
      style::success("Script check OK (" + make_summary(summary) + ").");
    }

    return 0;
  }

} // namespace vix::commands::CheckCommand::detail
