#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vix/cli/ErrorHandler.hpp"

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace vix::commands::RunCommand::detail
{
    namespace fs = std::filesystem;

    struct Options
    {
        std::string appName;
        std::string preset = "dev-ninja";
        std::string runPreset;
        std::string dir;
        int jobs = 0;

        bool quiet = false;
        bool verbose = false;
        std::string logLevel;
        std::string logFormat;
        std::string logColor; // --log-color (auto|always|never)
        bool noColor = false; // --no-color

        std::string exampleName;

        // Single .cpp mode
        bool singleCpp = false;
        std::filesystem::path cppFile;
        bool watch = false;

        bool forceServerLike = false;  // --force-server
        bool forceScriptLike = false;  // --force-script
        bool enableSanitizers = false; // --san  (ASan+UBSan)
        bool enableUbsanOnly = false;  // --ubsan (UBSan only)

        std::string clearMode = "auto";
        std::vector<std::string> scriptFlags;

        // .vix manifest mode (vix run app.vix)
        bool manifestMode = false;
        std::filesystem::path manifestFile;

        // Run extras from manifest (V1)
        std::vector<std::string> runArgs; // [run] args = ["--port","8080"]
        std::vector<std::string> runEnv;  // [run] env  = ["K=V","X=1"]
        int timeoutSec = 0;               // [run] timeout_sec = 15
    };

    // Process / IO
    int run_cmd_live_filtered(const std::string &cmd,
                              const std::string &spinnerLabel = {});
    std::filesystem::path manifest_entry_cpp(const std::filesystem::path &manifestFile);

    inline int normalize_exit_code(int code) noexcept
    {
#ifdef _WIN32
        return code;
#else
        if (code < 0)
            return 1;

        if (WIFEXITED(code))
            return WEXITSTATUS(code);

        if (WIFSIGNALED(code))
            return 128 + WTERMSIG(code);

        return 1;
#endif
    }

#ifndef _WIN32
    struct LiveRunResult
    {
        int rawStatus = 0; // waitpid status
        int exitCode = 0;  // normalized 0..255 or 128+signal
        std::string stdoutText;
        std::string stderrText;
    };

    LiveRunResult run_cmd_live_filtered_capture(const std::string &cmd,
                                                const std::string &spinnerLabel,
                                                bool passthroughRuntime,
                                                int timeoutSec = 0);
#endif

    // Script mode (vix run foo.cpp)
    std::filesystem::path get_scripts_root();

    /// Detect whether a .cpp script depends on Vix runtime
    bool script_uses_vix(const std::filesystem::path &cppPath);

    std::string make_script_cmakelists(
        const std::string &exeName,
        const fs::path &cppPath,
        bool useVixRuntime,
        const std::vector<std::string> &scriptFlags);

    int run_single_cpp(const Options &opt);
    int run_single_cpp_watch(const Options &opt);
    int run_project_watch(const Options &opt, const fs::path &projectDir);

    // CLI parsing
    Options parse(const std::vector<std::string> &args);

    // Build / run flow helpers
    std::string quote(const std::string &s);
    void handle_runtime_exit_code(int code, const std::string &context);

    bool has_presets(const fs::path &projectDir);
    std::string choose_run_preset(const fs::path &dir,
                                  const std::string &configurePreset,
                                  const std::string &userRunPreset);

    bool has_cmake_cache(const fs::path &buildDir);
    std::optional<fs::path> choose_project_dir(const Options &opt,
                                               const fs::path &cwd);

    void apply_log_level_env(const Options &opt);

    // Execution helpers (capturing output)
    std::string run_and_capture_with_code(const std::string &cmd, int &exitCode);
    std::string run_and_capture(const std::string &cmd);

    // Build log analysis
    bool has_real_build_work(const std::string &log);

    void apply_log_level_env(const Options &opt);
    void apply_log_format_env(const Options &opt);
    void apply_log_color_env(const Options &opt);

    inline void apply_log_env(const Options &opt)
    {
        apply_log_level_env(opt);
        apply_log_format_env(opt);
        apply_log_color_env(opt);
    }

} // namespace vix::commands::RunCommand::detail
