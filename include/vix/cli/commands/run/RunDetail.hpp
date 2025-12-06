#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

        std::string exampleName;

        // Single .cpp mode
        bool singleCpp = false;
        std::filesystem::path cppFile;
    };

    // --- Process / IO ---
    int run_cmd_live_filtered(const std::string &cmd);

    // --- Script mode (vix run foo.cpp) ---
    std::filesystem::path get_scripts_root();
    std::string make_script_cmakelists(const std::string &exeName,
                                       const std::filesystem::path &cppPath,
                                       bool useVixRuntime);
    int run_single_cpp(const Options &opt);

    // --- CLI parsing ---
    Options parse(const std::vector<std::string> &args);

    // --- Build / run flow helpers ---
    std::string quote(const std::string &s);
    void handle_runtime_exit_code(int code, const std::string &context);

    bool has_presets(const fs::path &projectDir);
    std::string choose_run_preset(const fs::path &dir,
                                  const std::string &configurePreset,
                                  const std::string &userRunPreset);

    bool has_cmake_cache(const fs::path &buildDir);
    std::optional<fs::path> choose_project_dir(const Options &opt, const fs::path &cwd);

    void apply_log_level_env(const Options &opt);

    // Execution helpers (capturing output)
    std::string run_and_capture_with_code(const std::string &cmd, int &exitCode);
    std::string run_and_capture(const std::string &cmd);

    // Build log analysis
    bool has_real_build_work(const std::string &log);

} // namespace vix::commands::RunCommand::detail
