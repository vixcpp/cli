#pragma once

#include <filesystem>
#include <string>

namespace vix::commands::RunCommand::detail
{
    namespace fs = std::filesystem;

    // UI
    void print_watch_restart_banner(const fs::path &script);
    // script/sanitizer helpers (shared between Run + Check)
    std::string sanitizer_mode_string(bool enableSanitizers, bool enableUbsanOnly);
    bool want_sanitizers(bool enableSanitizers, bool enableUbsanOnly);
    std::string make_script_config_signature(bool useVixRuntime,
                                             bool enableSanitizers,
                                             bool enableUbsanOnly);
#ifndef _WIN32
    void apply_sanitizer_env_if_needed(bool enableSanitizers, bool enableUbsanOnly);
#endif
} // namespace vix::commands::RunCommand::detail
