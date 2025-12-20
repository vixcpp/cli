#include "vix/cli/commands/check/CheckDetail.hpp"
#include "vix/cli/commands/helpers/ProcessHelpers.hpp"
#include "vix/cli/commands/run/RunScriptHelpers.hpp"
#include "vix/cli/commands/run/RunDetail.hpp"
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
        // Format: KEY:TYPE=VALUE
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

    static std::string resolve_build_type_from_cache_or_default(const fs::path &buildDir,
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

    static fs::path compute_runtime_executable_path(const fs::path &buildDir,
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

        // Last resort: what we *expect* to exist (so error is readable)
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
            const fs::path buildDir = guess_build_dir_from_configure_preset(projectDir, preset);

            // 2) configure only if cache missing
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

            // 3) choose build preset (never run preset)
            std::string buildPreset;
            if (!opt.buildPreset.empty())
            {
                buildPreset = opt.buildPreset;
            }
            else
            {
                // map configure preset -> build preset names in your JSON
                if (preset == "dev-ninja")
                    buildPreset = "build-ninja";
                else if (preset == "dev-ninja-san")
                    buildPreset = "build-ninja-san";
                else if (preset == "dev-ninja-ubsan")
                    buildPreset = "build-ninja-ubsan";
                else
                    buildPreset = preset; // fallback if user uses custom presets/aliases
            }

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

                // 1) Try preset route if file exists (or user explicitly forced a ctest preset)
                if (hasCTestPresets || !opt.ctestPreset.empty())
                {
                    const std::string ctp = opt.ctestPreset.empty() ? ("test-" + preset) : opt.ctestPreset;

                    std::ostringstream cmd;
                    cmd << "cd " << quote(projectDir.string())
                        << " && ctest --preset " << quote(ctp) << " --output-on-failure";

                    int tcode = run_cmd_live_filtered(cmd.str(), "Running tests");

                    // If preset failed, fallback to build dir (common: preset doesn't exist)
                    if (tcode != 0)
                    {
                        hint("CTest preset failed — falling back to build directory.");
                        std::ostringstream fb;
                        fb << "cd " << quote(buildDir.string())
                           << " && ctest --output-on-failure";

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
                       << " && ctest --output-on-failure";

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
                // Important: appliquer env ASAN/UBSAN avant d'exécuter
                run::apply_sanitizer_env_if_needed(opt.enableSanitizers, opt.enableUbsanOnly);

                const std::string projectName = guess_project_name_from_dir(projectDir);
                const std::string configName = resolve_build_type_from_cache_or_default(buildDir, "Debug");
                const fs::path exePath = compute_runtime_executable_path(buildDir, projectName, configName);

                // timeout (seconds)
                const int timeoutSec = (opt.runTimeoutSec > 0) ? opt.runTimeoutSec : 15;

                // Run from buildDir (better for relative paths/resources)
                std::ostringstream r;
                r << "cd " << quote(buildDir.string()) << " && " << quote(exePath.string());

                auto rr = run_cmd_live_filtered_capture(
                    r.str(),
                    "Checking runtime (" + exePath.filename().string() + ")",
                    /*passthroughRuntime*/ false,
                    /*timeoutSec*/ timeoutSec);

                if (rr.exitCode != 0)
                {
                    const std::string runtimeLog = rr.stdoutText + "\n" + rr.stderrText;

                    vix::cli::errors::RawLogDetectors::handleRuntimeCrash(
                        runtimeLog, projectDir, "Project check failed (runtime sanitizers)");

                    handle_runtime_exit_code(rr.exitCode, "Project check failed (runtime sanitizers)");
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
#ifdef _WIN32
            int tcode = 0;
            std::string tlog = run_and_capture_with_code(
                "cmd /C \"cd /D " + quote(buildDir.string()) + " && ctest --output-on-failure\"",
                tcode);

            if (tcode != 0)
            {
                if (!tlog.empty())
                    std::cout << tlog;
                error("Tests failed.");
                return tcode != 0 ? tcode : 4;
            }
#else
            const int tcode = run_cmd_live_filtered(
                "cd " + quote(buildDir.string()) + " && ctest --output-on-failure",
                "Running tests");

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
