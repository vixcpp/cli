#include "vix/cli/commands/run/RunScriptHelpers.hpp"
#include <vix/cli/Style.hpp>

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
    namespace
    {
        struct WatchSpinner
        {
            std::atomic<bool> running{false};
            std::thread worker;

            void start(std::string label)
            {
                bool expected = false;
                if (!running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    return;

                worker = std::thread([this, label = std::move(label)]()
                                     {
                    static const char *frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
                    constexpr std::size_t frameCount = sizeof(frames) / sizeof(frames[0]);

                    std::size_t i = 0;
                    while (running.load(std::memory_order_relaxed))
                    {
                        std::cout << "\r┃   " << frames[i] << " " << label << "   " << std::flush;
                        i = (i + 1) % frameCount;
                        std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    } });
            }

            void stop()
            {
                bool expected = true;
                if (!running.compare_exchange_strong(expected, false))
                    return;

                if (worker.joinable())
                    worker.join();

                std::cout << "\r" << std::string(120, ' ') << "\r" << std::flush;
            }

            ~WatchSpinner()
            {
                stop();
            }
        };

        WatchSpinner g_watch_spinner;
    }

    void watch_spinner_start(std::string label)
    {
        g_watch_spinner.start(std::move(label));
    }

    void watch_spinner_stop()
    {
        g_watch_spinner.stop();
    }

    void watch_spinner_pause_for_output()
    {
        watch_spinner_stop();
    }

    namespace
    {
        struct WatchSpinnerScope
        {
            explicit WatchSpinnerScope(std::string label)
            {
                watch_spinner_start(std::move(label));
            }
            ~WatchSpinnerScope()
            {
                watch_spinner_stop();
            }
        };
    }

    void print_watch_restart_banner(const fs::path &path, std::string_view label)
    {
#ifdef _WIN32
        std::system("cls");
#else
        std::cout << "\x1b[2J\x1b[H" << std::flush;
#endif

        info(std::string("Watcher Restarting! File change detected: \"") + path.string() + "\"");

        std::cout << "\n"
                  << std::flush;

        if (label.empty())
            label = "Rebuilding & restarting...";

        watch_spinner_start(std::string(label));
    }

    std::string sanitizer_mode_string(bool /*enableSanitizers*/, bool enableUbsanOnly)
    {
        return enableUbsanOnly ? "ubsan" : "asan_ubsan";
    }

    bool want_sanitizers(bool enableSanitizers, bool enableUbsanOnly)
    {
        return enableSanitizers || enableUbsanOnly;
    }

    std::string make_script_config_signature(
        bool useVixRuntime,
        bool enableSanitizers,
        bool enableUbsanOnly,
        const std::vector<std::string> &scriptFlags)
    {
        std::string sig;
        sig.reserve(128);

        sig += "useVix=";
        sig += useVixRuntime ? "1" : "0";

        sig += ";san=";
        sig += want_sanitizers(enableSanitizers, enableUbsanOnly) ? "1" : "0";

        sig += ";mode=";
        sig += sanitizer_mode_string(enableSanitizers, enableUbsanOnly);

        sig += ";flags=";
        for (const auto &f : scriptFlags)
        {
            sig += f;
            sig += ",";
        }

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

} // namespace vix::commands::RunCommand::detail
