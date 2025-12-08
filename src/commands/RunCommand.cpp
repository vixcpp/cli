#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/Style.hpp>

#include "vix/cli/commands/run/RunDetail.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace vix::cli::style;
namespace fs = std::filesystem;

// Small helper to show a modern multi-step progress (like Vue CLI / Deno)
namespace
{
    struct RunProgress
    {
        int total;
        int current = 0;

        explicit RunProgress(int totalSteps)
            : total(totalSteps)
        {
        }

        void phase_start(const std::string &label)
        {
            ++current;
            // Blank line to visually separate phases
            std::cout << "\n";
            info("┏ [" + std::to_string(current) + "/" + std::to_string(total) + "] " + label);
        }

        void phase_done(const std::string &label, const std::string &extra = {})
        {
            std::string msg = "┗ [" + std::to_string(current) + "/" + std::to_string(total) + "] " + label;
            if (!extra.empty())
                msg += " — " + extra;
            success(msg);
        }
    };
}

namespace vix::commands::RunCommand
{
    using namespace detail;

    int run(const std::vector<std::string> &args)
    {
        const Options opt = parse(args);

        if (opt.singleCpp)
        {
            return run_single_cpp(opt);
        }

        const fs::path cwd = fs::current_path();

        auto projectDirOpt = choose_project_dir(opt, cwd);
        if (!projectDirOpt)
        {
            error("Unable to determine the project folder.");
            hint("Try: vix run --dir <path> or run the command from a Vix project directory.");
            return 1;
        }

        const fs::path projectDir = *projectDirOpt;

        apply_log_level_env(opt);

        info("Using project directory:");
        step(projectDir.string());
        std::cout << "\n";

        // ----- CAS 1 : Presets -----
        if (has_presets(projectDir))
        {
            // We have two main phases with presets: configure + build&run
            RunProgress progress(/*totalSteps=*/2);

            // 1) Configure
            {
                progress.phase_start("Configure project (preset: " + opt.preset + ")");

                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && cmake --preset " << quote(opt.preset) << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --preset " << quote(opt.preset);
#endif
                const std::string cmd = oss.str();

                int code = 0;
                std::string configureLog = run_and_capture_with_code(cmd, code);

                if (code != 0)
                {
                    if (!configureLog.empty())
                        std::cout << configureLog;

                    error("CMake configure failed with preset '" + opt.preset + "'.");
                    hint("Run the same command manually to inspect the error:");
#ifdef _WIN32
                    step("cmake --preset " + opt.preset);
#else
                    step("cd " + projectDir.string());
                    step("cmake --preset " + opt.preset);
#endif
                    return code != 0 ? code : 2;
                }

                if (opt.verbose && !configureLog.empty())
                    std::cout << configureLog;

                progress.phase_done("Configure project", "completed");
                std::cout << "\n";
            }

            // 2) run preset
            const std::string runPreset =
                choose_run_preset(projectDir, opt.preset, opt.runPreset);

            {
                progress.phase_start("Build & run (preset: " + runPreset + ")");

                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(runPreset) << " --target run";
                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs;
                oss << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(runPreset) << " --target run";
                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs;
#endif
                const std::string cmd = oss.str();

                const int code = run_cmd_live_filtered(cmd);
                if (code != 0)
                {
                    handle_runtime_exit_code(
                        code,
                        "Execution failed (run preset '" + runPreset + "')");
                    return code;
                }

                progress.phase_done("Build & run", "application started");
            }

            success("🏃 Application started (preset: " + runPreset + ").");
            return 0;
        }

        // ----- CAS 2 : Fallback build/ + cmake .. -----
        fs::path buildDir = projectDir / "build";

        // 3 main phases: configure (if needed) + build + run
        RunProgress progress(/*totalSteps=*/3);

        {
            std::error_code ec;
            fs::create_directories(buildDir, ec);
            if (ec)
            {
                error("Unable to create build directory: " + ec.message());
                return 1;
            }
        }

        if (!has_cmake_cache(buildDir))
        {
            progress.phase_start("Configure project (fallback)");

            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake ..\"";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake ..";
#endif
            const std::string cmd = oss.str();

            const int code = run_cmd_live_filtered(cmd);
            if (code != 0)
            {
                error("CMake configure failed (fallback build/, code " + std::to_string(code) + ").");
                hint("Check your CMakeLists.txt or run the command manually.");
                return code != 0 ? code : 4;
            }

            progress.phase_done("Configure project", "completed (fallback)");
            std::cout << "\n";
        }
        else
        {
            info("CMake cache detected in build/ — skipping configure step (fallback).");
            // Mark config phase as "already done"
            progress.phase_start("Configure project (fallback)");
            progress.phase_done("Configure project", "cache already present");
        }

        {
            progress.phase_start("Build project (fallback)");

            std::ostringstream oss;
#ifdef _WIN32
            oss << "cd /D " << quote(buildDir.string()) << " && cmake --build .";
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
#endif
            const std::string cmd = oss.str();

            int code = 0;
            std::string buildLog = run_and_capture_with_code(cmd, code);

            if (code != 0)
            {
                std::cout << buildLog;
                error("Build failed (fallback build/, code " + std::to_string(code) + ").");
                hint("Check the build logs or run the command manually.");
                return code != 0 ? code : 5;
            }

            if (has_real_build_work(buildLog))
            {
                std::cout << buildLog;
                progress.phase_done("Build project", "completed (fallback)");
            }
            else
            {
                progress.phase_done("Build project", "up to date");
                success("Nothing to build — everything is up to date.");
            }

            std::cout << "\n";
        }

        const std::string exeName = projectDir.filename().string();
        fs::path exePath = buildDir / exeName;
#ifdef _WIN32
        exePath += ".exe";
#endif

        if (exeName == "vix")
        {
            success("Build completed (fallback).");

            if (opt.appName == "example")
            {
                if (opt.exampleName.empty())
                {
                    error("No example name provided.");
                    hint("Usage: vix run example <name>");
                    hint("For instance: vix run example main");
                    return 1;
                }

                fs::path exampleExe = buildDir / opt.exampleName;
#ifdef _WIN32
                exampleExe += ".exe";
#endif

                if (!fs::exists(exampleExe))
                {
                    error("Example binary not found: " + exampleExe.string());
                    hint("Make sure the example exists and is enabled in CMake.");
                    hint("Existing examples are built in the build/ directory "
                         "(e.g. main, now_server, hello_routes...).");
                    return 1;
                }

                info("Running example: " + opt.exampleName);
                std::string cmd =
#ifdef _WIN32
                    "\"" + exampleExe.string() + "\"";
#else
                    quote(exampleExe.string());
#endif
                const int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    handle_runtime_exit_code(
                        code,
                        "Example returned non-zero exit code");
                }
                return code;
            }

            hint("Detected the Vix umbrella repository.");
            hint("The CLI binary 'vix' and umbrella examples were built in the build/ directory.");
            hint("To run an example from here, use:");
            step("  vix run example main");
            step("  vix run example now_server");
            step("  vix run example hello_routes");
            return 0;
        }

        // Phase 3: run
        if (fs::exists(exePath))
        {
            progress.phase_start("Run application");

            info("Running executable: " + exePath.string());
            std::string cmd =
#ifdef _WIN32
                "\"" + exePath.string() + "\"";
#else
                quote(exePath.string());
#endif
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                handle_runtime_exit_code(
                    code,
                    "Executable returned non-zero exit code");
                return code;
            }

            progress.phase_done("Run application", "completed");
        }
        else
        {
            progress.phase_start("Run application");
            progress.phase_done("Run application", "no explicit run target");
            success("Build completed (fallback). No explicit 'run' target found.");
            hint("If you want to run a specific example or binary, "
                 "execute it manually from the build/ directory.");
        }

        return 0;
    }

    int help()
    {
        std::ostream &out = std::cout;

        out << "Usage:\n";
        out << "  vix run [name] [options] [-- app-args...]\n\n";

        out << "Description:\n";
        out << "  Configure, build and run a Vix.cpp application using CMake presets.\n";
        out << "  The command ensures CMake is configured, then builds the 'run' target\n";
        out << "  with the selected preset, and finally executes the resulting binary.\n\n";

        out << "Options:\n";
        out << "  -d, --dir <path>        Explicit project directory\n";
        out << "  --preset <name>         Configure preset (CMakePresets.json), default: dev-ninja\n";
        out << "  --run-preset <name>     Build preset used to build target 'run'\n";
        out << "  -j, --jobs <n>          Number of parallel build jobs\n\n";

        out << "Global flags (from `vix`):\n";
        out << "  --verbose               Show debug logs from the runtime (log-level=debug)\n";
        out << "  -q, --quiet             Only show warnings and errors (log-level=warn)\n";
        out << "  --log-level <level>     Set runtime log-level for the app process\n\n";

        out << "Examples:\n";
        out << "  vix run\n";
        out << "  vix run api -- --port 8080\n";
        out << "  vix run --dir ./examples/blog\n";
        out << "  vix run api --preset dev-ninja --run-preset run-ninja\n";
        out << "  vix run example main              # in the umbrella repo, run ./build/main\n\n";
        out << "  vix --log-level debug run api     # run with debug logs from runtime\n";
        out << "  vix --quiet run api               # minimal logs from runtime\n";

        return 0;
    }

} // namespace vix::commands::RunCommand
