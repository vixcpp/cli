#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/Style.hpp>

#include "vix/cli/commands/run/RunDetail.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <cstdlib>

using namespace vix::cli::style;
namespace fs = std::filesystem;

namespace
{
    struct Spinner
    {
        std::atomic<bool> running{false};
        std::thread worker;

        explicit Spinner(const std::string &text)
        {
            start(text);
        }

        void start(const std::string &text)
        {
            bool expected = false;
            // Si déjà en cours → ne pas relancer un 2e thread
            if (!running.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel))
                return;

            worker = std::thread(
                [this, text]()
                {
                    // Frames style Vite / npm
                    static const char *frames[] = {
                        "⠋", "⠙", "⠹", "⠸",
                        "⠼", "⠴", "⠦", "⠧",
                        "⠇", "⠏"};
                    const std::size_t frameCount = sizeof(frames) / sizeof(frames[0]);

                    std::size_t idx = 0;

                    while (running.load(std::memory_order_relaxed))
                    {
                        // Ligne unique, réécrite avec \r
                        std::cout << "\r┃   " << frames[idx] << " " << text << "   "
                                  << std::flush;

                        idx = (idx + 1) % frameCount;
                        std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    }

                    // On ne met pas de \n ici, stop() s'en chargera.
                });
        }

        void stop()
        {
            bool expected = true;
            if (!running.compare_exchange_strong(expected, false))
                return;

            if (worker.joinable())
                worker.join();

            // efface proprement + remet au début + saute UNE ligne
            std::cout << "\r" << std::string(80, ' ') << "\r" << std::flush;
        }

        ~Spinner()
        {
            stop();
        }
    };

    struct RunProgress
    {
        using Clock = std::chrono::steady_clock;

        int total;
        int current = 0;

        std::string currentLabel;
        Clock::time_point phaseStart{};

        explicit RunProgress(int totalSteps)
            : total(totalSteps)
        {
        }

        void phase_start(const std::string &label)
        {
            ++current;
            currentLabel = label;
            phaseStart = Clock::now();

            // Un seul saut de ligne juste avant CHAQUE phase
            std::cout << std::endl;

            info("┏ [" + std::to_string(current) + "/" +
                 std::to_string(total) + "] " + label);
        }

        void phase_log(const std::string &msg)
        {
            step("┃   " + msg);
        }

        void phase_done(const std::string &label, const std::string &extra = {})
        {
            const auto end = Clock::now();

            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                end - phaseStart)
                                .count();

            std::string msg =
                "┗ [" + std::to_string(current) + "/" +
                std::to_string(total) + "] " + label;

            if (!extra.empty())
                msg += " — " + extra;

            const double seconds = static_cast<double>(ms) / 1000.0;

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << seconds;
            msg += " (" + oss.str() + "s)";

            // plus de std::cout << "\n" ici
            success(msg);
        }
    };

    void enable_line_buffered_stdout_for_apps()
    {
#ifndef _WIN32
        ::setenv("VIX_STDOUT_MODE", "line", 1);
#endif
    }

} // namespace

namespace vix::commands::RunCommand
{
    using namespace detail;

    static void ensure_mode_env_for_run(const Options &opt)
    {
        // If DevCommand already set VIX_MODE, keep it.
        // Otherwise: watch => dev, else run.
        const char *cur = std::getenv("VIX_MODE");
        if (cur && *cur)
            return;

#ifdef _WIN32
        _putenv_s("VIX_MODE", opt.watch ? "dev" : "run");
#else
        ::setenv("VIX_MODE", opt.watch ? "dev" : "run", 1);
#endif
    }

    int run(const std::vector<std::string> &args)
    {
        const Options opt = parse(args);

        ensure_mode_env_for_run(opt);
        enable_line_buffered_stdout_for_apps();

#ifndef _WIN32
        ::setenv("VIX_CLI_CLEAR", opt.clearMode.c_str(), 1);
#else
        _putenv_s("VIX_CLI_CLEAR", opt.clearMode.c_str());
#endif

        // 1) Mode single .cpp (scripts)
        if (opt.singleCpp && opt.watch)
        {
            return detail::run_single_cpp_watch(opt);
        }

        if (opt.singleCpp)
        {
            return detail::run_single_cpp(opt);
        }

        // 2) Mode projet (apps)
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

        // Project mode + watch → we go DIRECTLY through run_project_watch,
        // even if CMakePresets.json exists (we use a dedicated dev build).
        if (!opt.singleCpp && opt.watch)
        {
#ifndef _WIN32
            return detail::run_project_watch(opt, projectDir);
#else
            hint("Project watch mode is not yet implemented on Windows; running once without auto-reload.");
#endif
        }

        // 3) If we are not in watch mode → continue with the existing logic

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

                Spinner spinner("Configuring project with preset \"" + opt.preset + "\"");

                int code = 0;
                std::string configureLog = run_and_capture_with_code(cmd, code);

                spinner.stop();

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
            }

            // 2) run preset
            const std::string runPreset =
                choose_run_preset(projectDir, opt.preset, opt.runPreset);

            {
                progress.phase_start("Build & run (preset: " + runPreset + ")");
                const std::string mode = opt.watch ? "dev" : "run";

                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && set VIX_STDOUT_MODE=line"
                    << " && set VIX_MODE=" << mode
                    << " && cmake --build --preset " << quote(runPreset) << " --target run";

                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs;

                oss << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && VIX_STDOUT_MODE=line"
                    << " VIX_MODE=" << mode
                    << " cmake --build --preset " << quote(runPreset) << " --target run";

                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs;
#endif
                const std::string cmd = oss.str();

                const int code = run_cmd_live_filtered(
                    cmd,
                    "Building & running with preset \"" + runPreset + "\"");

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

            Spinner spinner("Configuring project (fallback)");

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
            std::string buildLog;

            if (opt.quiet)
            {
                Spinner spinner("Building project (fallback)");
                buildLog = run_and_capture_with_code(cmd, code);
            }
            else
            {
                code = run_cmd_live_filtered(cmd, "Building project (fallback)");
            }

            if (code != 0)
            {
                if (buildLog.empty())
                {
                    int captureCode = 0;
                    buildLog = run_and_capture_with_code(cmd, captureCode);
                    (void)captureCode;
                }

                if (!buildLog.empty())
                {
                    vix::cli::ErrorHandler::printBuildErrors(
                        buildLog,
                        buildDir,
                        "Build failed (fallback build/)");
                }
                else
                {
                    error("Build failed (fallback build/, code " + std::to_string(code) + ").");
                    hint("Check the build command manually in your terminal.");
                }

                return code != 0 ? code : 5;
            }

            if (!buildLog.empty() && has_real_build_work(buildLog))
                std::cout << buildLog;

            progress.phase_done("Build project", "completed (fallback)");
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
        out << "  -j, --jobs <n>          Number of parallel build jobs\n";
        out << "  --clear <auto|always|never>  Control terminal clearing before runtime output (default: auto)\n";
        out << "  --clear=auto|always|never    Same as above\n";
        out << "  --no-clear                   Alias for --clear=never\n";

        out << "Watch / process mode:\n";
        out << "  --watch, --reload       Rebuild & restart on file changes (hot reload)\n";
        out << "  --force-server          Force server-like mode (long-lived process)\n";
        out << "  --force-script          Force script-like mode (short-lived process)\n\n";

        out << "Sanitizers (script mode only):\n";
        out << "  --san                   Enable ASan + UBSan for single-file .cpp scripts\n";
        out << "  --ubsan                 Enable UBSan only for single-file .cpp scripts\n\n";

        out << "Global flags (from `vix`):\n";
        out << "  --verbose               Show debug logs from the runtime (log-level=debug)\n";
        out << "  -q, --quiet             Only show warnings and errors (log-level=warn)\n";
        out << "  --log-level <level>     Set runtime log-level for the app process\n\n";

        out << "Examples:\n";
        out << "  vix run\n";
        out << "  vix run api -- --port 8080\n";
        out << "  vix run --dir ./examples/blog\n";
        out << "  vix run api --preset dev-ninja --run-preset run-ninja\n";
        out << "  vix run --watch api\n";
        out << "  vix run --force-server --watch api\n";
        out << "  vix run example main              # in the umbrella repo, run ./build/main\n\n";

        out << "  vix run main.cpp                  # compile & run single-file script\n";
        out << "  vix run main.cpp --san            # script with ASan+UBSan\n";
        out << "  vix run main.cpp --ubsan          # script with UBSan only\n\n";

        out << "  vix --log-level debug run api     # run with debug logs from runtime\n";
        out << "  vix --quiet run api               # minimal logs from runtime\n";

        return 0;
    }

} // namespace vix::commands::RunCommand
