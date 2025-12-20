#include "vix/cli/commands/run/RunScriptHelpers.hpp"
#include <vix/cli/Style.hpp>

#include <iostream>
#include <cstdlib>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
    void print_watch_restart_banner(const fs::path &script)
    {
#ifdef _WIN32
        std::system("cls");
#else
        std::cout << "\x1b[2J\x1b[H" << std::flush;
#endif
        info(std::string("Watcher Restarting! File change detected: \"") + script.string() + "\"");
    }

    std::string sanitizer_mode_string(bool /*enableSanitizers*/, bool enableUbsanOnly)
    {
        return enableUbsanOnly ? "ubsan" : "asan_ubsan";
    }

    bool want_sanitizers(bool enableSanitizers, bool enableUbsanOnly)
    {
        return enableSanitizers || enableUbsanOnly;
    }

    std::string make_script_config_signature(bool useVixRuntime,
                                             bool enableSanitizers,
                                             bool enableUbsanOnly)
    {
        std::string sig;
        sig.reserve(64);
        sig += "useVix=" + std::string(useVixRuntime ? "1" : "0");
        sig += ";san=" + std::string(want_sanitizers(enableSanitizers, enableUbsanOnly) ? "1" : "0");
        sig += ";mode=" + sanitizer_mode_string(enableSanitizers, enableUbsanOnly);
        return sig;
    }

#ifndef _WIN32
    void apply_sanitizer_env_if_needed(bool enableSanitizers, bool enableUbsanOnly)
    {
        if (!want_sanitizers(enableSanitizers, enableUbsanOnly))
            return;

        ::setenv("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1:color=never", 1);

        if (!enableUbsanOnly)
        {
            ::setenv(
                "ASAN_OPTIONS",
                "abort_on_error=1:"
                "detect_leaks=1:"
                "symbolize=1:"
                "allocator_may_return_null=1:"
                "fast_unwind_on_malloc=0:"
                "strict_init_order=1:"
                "check_initialization_order=1:"
                "color=never:"
                "quiet=1",
                1);
        }
    }
#endif
}
