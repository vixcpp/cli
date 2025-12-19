#include "vix/cli/commands/run/RunDetail.hpp"

#include <vix/cli/Style.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <errno.h>
#include <signal.h>     // sigaction, SIGINT, SIG_DFL, SIG_IGN
#include <sys/select.h> // select, fd_set, FD_SET, FD_ZERO, FD_ISSET
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid, WIFEXITED, WEXITSTATUS
#include <unistd.h>     // pipe, dup2, read, write, close
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{

#ifndef _WIN32

    // RAII: ignore SIGINT in the parent while the child runs.
    // This avoids double Ctrl+C handling when we forward SIGINT elsewhere.
    struct SigintGuard
    {
        struct sigaction oldAction{};
        bool installed = false;

        SigintGuard()
        {
            struct sigaction sa{};
            sa.sa_handler = SIG_IGN;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            if (sigaction(SIGINT, &sa, &oldAction) == 0)
                installed = true;
        }

        ~SigintGuard()
        {
            if (installed)
                sigaction(SIGINT, &oldAction, nullptr);
        }
    };

    // Best-effort write (no throwing, no retry loops).
    static inline void write_safe(int fd, const char *buf, ssize_t n)
    {
        if (n <= 0)
            return;
        const ssize_t written = ::write(fd, buf, static_cast<size_t>(n));
        (void)written;
    }

    static inline void close_safe(int &fd)
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }

    // Filters build output until runtime markers appear; then clears screen + prints header
    // and forwards runtime output only.
    class RuntimeOutputFilter
    {
    public:
        std::string process(const std::string &chunk, int fdForBuildAndRuntime)
        {
            if (clearedForRuntime_)
                return chunk;

            buffer_ += chunk;

            // detect runtime markers (earliest)
            std::size_t first = std::string::npos;
            auto update_min = [&](std::size_t candidate)
            {
                if (candidate != std::string::npos &&
                    (first == std::string::npos || candidate < first))
                {
                    first = candidate;
                }
            };

            update_min(buffer_.find("[I]"));
            update_min(buffer_.find("[W]"));
            update_min(buffer_.find("[E]"));
            update_min(buffer_.find("Using configuration file:"));

            if (first == std::string::npos)
            {
                // flush build prefix progressively (keep a small tail)
                std::string prefix = flush_prefix_if_needed();
                if (!prefix.empty())
                {
                    write_safe(fdForBuildAndRuntime,
                               prefix.data(),
                               static_cast<ssize_t>(prefix.size()));
                }
                return {};
            }

            // switch to runtime-only mode
            clearedForRuntime_ = true;

            buffer_.erase(0, first);

            // clear terminal
            const char *clearScreen = "\033[2J\033[H";
            write_safe(STDOUT_FILENO, clearScreen,
                       static_cast<ssize_t>(std::strlen(clearScreen)));

            // header (vite-ish)
            std::string header;
            header += "Vix.cpp runtime is ready 🚀\n\n";
            header += "Logs:\n\n";
            write_safe(STDOUT_FILENO, header.c_str(),
                       static_cast<ssize_t>(header.size()));

            std::string out = buffer_;
            buffer_.clear();
            return out;
        }

    private:
        static constexpr std::size_t KEEP_TAIL = 8;

        bool clearedForRuntime_ = false;
        std::string buffer_;

        std::string flush_prefix_if_needed()
        {
            if (buffer_.size() <= KEEP_TAIL)
                return {};

            const std::size_t flushLen = buffer_.size() - KEEP_TAIL;
            std::string out = buffer_.substr(0, flushLen);
            buffer_.erase(0, flushLen);
            return out;
        }
    };

    // helper: read from fd into a chunk, return false if fd should be considered closed.
    static inline bool read_into(int fd, std::string &outChunk)
    {
        char buf[4096];
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0)
        {
            outChunk.assign(buf, static_cast<std::size_t>(n));
            return true;
        }
        outChunk.clear();
        return false;
    }

    static inline bool should_drop_chunk_default(const std::string &chunk)
    {
        // avoid noisy stop messages when interrupted
        static const std::string ninjaStop = "ninja: build stopped: interrupted by user.";
        if (chunk.find(ninjaStop) != std::string::npos)
            return true;

        const bool hasInterrupt = (chunk.find("Interrupt") != std::string::npos);
        if (hasInterrupt &&
            (chunk.find("gmake") != std::string::npos ||
             chunk.find("make: ***") != std::string::npos ||
             chunk.find("gmake: ***") != std::string::npos))
        {
            return true;
        }

        return false;
    }

    static inline void spinner_draw(const std::string &label, std::size_t &frameIndex)
    {
        static const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        const std::size_t frameCount = sizeof(frames) / sizeof(frames[0]);

        std::string line = "\r┃   ";
        line += frames[frameIndex];
        line += " ";
        line += label;
        line += "   ";
        write_safe(STDOUT_FILENO, line.c_str(),
                   static_cast<ssize_t>(line.size()));

        frameIndex = (frameIndex + 1) % frameCount;
    }

    static inline void spinner_clear()
    {
        const char *clearLine =
            "\r                                                                                \r";
        write_safe(STDOUT_FILENO, clearLine,
                   static_cast<ssize_t>(std::strlen(clearLine)));
    }

    // Capturing version (declared in RunDetail.hpp under !WIN32)
    LiveRunResult run_cmd_live_filtered_capture(const std::string &cmd,
                                                const std::string &spinnerLabel,
                                                bool passthroughRuntime)
    {
        SigintGuard sigGuard;

        LiveRunResult result;

        int outPipe[2] = {-1, -1};
        int errPipe[2] = {-1, -1};

        if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0)
        {
            result.exitCode = std::system(cmd.c_str());
            return result;
        }

        pid_t pid = ::fork();
        if (pid < 0)
        {
            close_safe(outPipe[0]);
            close_safe(outPipe[1]);
            close_safe(errPipe[0]);
            close_safe(errPipe[1]);
            result.exitCode = std::system(cmd.c_str());
            return result;
        }

        if (pid == 0)
        {
            struct sigaction saChild{};
            saChild.sa_handler = SIG_DFL;
            sigemptyset(&saChild.sa_mask);
            saChild.sa_flags = 0;
            ::sigaction(SIGINT, &saChild, nullptr);

            ::setenv("ASAN_OPTIONS",
                     "abort_on_error=1:"
                     "detect_leaks=1:"
                     "symbolize=1:"
                     "allocator_may_return_null=1:"
                     "fast_unwind_on_malloc=0:"
                     "strict_init_order=1:"
                     "check_initialization_order=1:"
                     "color=never",
                     1);

            ::setenv("UBSAN_OPTIONS",
                     "halt_on_error=1:print_stacktrace=1:color=never",
                     1);

            ::close(outPipe[0]);
            ::close(errPipe[0]);

            ::dup2(outPipe[1], STDOUT_FILENO);
            ::dup2(errPipe[1], STDERR_FILENO);

            ::close(outPipe[1]);
            ::close(errPipe[1]);

            ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
            _exit(127);
        }

        // ===== Parent =====
        close_safe(outPipe[1]);
        close_safe(errPipe[1]);

        const bool useSpinner = !spinnerLabel.empty();
        bool spinnerActive = useSpinner;
        std::size_t frameIndex = 0;

        RuntimeOutputFilter runtimeFilter;

        // ---- Sanitizer suppressor (stateful) ----
        struct SanitizerSuppressor
        {
            // Drop ONLY sanitizer report regions (keep normal runtime output).
            bool inReport = false;
            std::string carry;

            static bool is_all_equals_line(std::string_view line) noexcept
            {
                std::size_t eq = 0;
                for (char c : line)
                {
                    if (c == '=')
                        ++eq;
                    else if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
                        continue;
                    else
                        return false;
                }
                return eq >= 20;
            }

            static bool is_vix_runtime_line(std::string_view line) noexcept
            {
                return (line.find("[I]") != std::string_view::npos) ||
                       (line.find("[W]") != std::string_view::npos) ||
                       (line.find("[E]") != std::string_view::npos) ||
                       (line.find("Vix.cpp runtime") != std::string_view::npos) ||
                       (line.find("Logs:") != std::string_view::npos) ||
                       (line.find("Using configuration file:") != std::string_view::npos);
            }

            static bool is_sanitizer_keyword_line(std::string_view line) noexcept
            {
                if (line.find("AddressSanitizer") != std::string_view::npos)
                    return true;
                if (line.find("LeakSanitizer") != std::string_view::npos)
                    return true;
                if (line.find("ThreadSanitizer") != std::string_view::npos)
                    return true;
                if (line.find("MemorySanitizer") != std::string_view::npos)
                    return true;

                if (line.find("ASAN_OPTIONS") != std::string_view::npos)
                    return true;
                if (line.find("UBSAN_OPTIONS") != std::string_view::npos)
                    return true;
                if (line.find("LSAN_OPTIONS") != std::string_view::npos)
                    return true;
                if (line.find("TSAN_OPTIONS") != std::string_view::npos)
                    return true;
                if (line.find("MSAN_OPTIONS") != std::string_view::npos)
                    return true;

                if (line.find("Shadow bytes around the buggy address") != std::string_view::npos)
                    return true;
                if (line.find("Shadow byte legend") != std::string_view::npos)
                    return true;
                if (line.find("READ of size") != std::string_view::npos)
                    return true;
                if (line.find("WRITE of size") != std::string_view::npos)
                    return true;
                if (line.find("freed by thread") != std::string_view::npos)
                    return true;
                if (line.find("previously allocated by thread") != std::string_view::npos)
                    return true;
                if (line.find("is located") != std::string_view::npos &&
                    line.find("inside of") != std::string_view::npos)
                    return true;
                if (line.find("is located in stack of thread") != std::string_view::npos)
                    return true;

                return false;
            }

            static bool is_report_start(std::string_view line) noexcept
            {
                if (is_all_equals_line(line))
                    return true;

                // Typical "==PID==ERROR/HINT/WARNING: ..."
                if (line.rfind("==", 0) == 0 &&
                    (line.find("ERROR:") != std::string_view::npos ||
                     line.find("HINT:") != std::string_view::npos ||
                     line.find("WARNING:") != std::string_view::npos))
                {
                    // Start only if it looks sanitizer-related
                    if (is_sanitizer_keyword_line(line) || line.find("Sanitizer") != std::string_view::npos)
                        return true;
                }

                // Some lines appear without the ==pid== prefix
                if (is_sanitizer_keyword_line(line))
                    return true;

                // UBSan sometimes prints: "runtime error: ..."
                if (line.find("runtime error:") != std::string_view::npos)
                    return true;

                return false;
            }

            static bool is_report_end(std::string_view line) noexcept
            {
                // ASan often ends with SUMMARY, but not always.
                if (line.rfind("SUMMARY:", 0) == 0)
                    return true;

                // If sanitizer report ended and normal runtime starts again.
                if (is_vix_runtime_line(line))
                    return true;

                return false;
            }

            std::string filter_for_print(const std::string &chunk)
            {
                std::string data = carry;
                data += chunk;
                carry.clear();

                std::string out;
                out.reserve(data.size());

                std::size_t start = 0;
                while (true)
                {
                    const std::size_t nl = data.find('\n', start);
                    if (nl == std::string::npos)
                    {
                        carry = data.substr(start);
                        break;
                    }

                    std::string_view line(&data[start], (nl - start) + 1);
                    start = nl + 1;

                    if (!inReport)
                    {
                        if (is_report_start(line))
                        {
                            inReport = true;
                            continue; // drop
                        }
                        out.append(line.data(), line.size());
                    }
                    else
                    {
                        // We are inside sanitizer report: drop everything.
                        // We stop dropping when we reach SUMMARY or we see our own runtime output again.
                        if (is_report_end(line))
                        {
                            inReport = false;
                            continue; // drop end line too
                        }
                    }
                }

                return out;
            }
        };

        SanitizerSuppressor sanitizer;

        bool running = true;

        int finalStatus = 0; // RAW wait-status (waitpid format)
        bool haveStatus = false;

        while (running)
        {
            fd_set fds;
            FD_ZERO(&fds);

            int maxfd = -1;
            if (outPipe[0] >= 0)
            {
                FD_SET(outPipe[0], &fds);
                maxfd = std::max(maxfd, outPipe[0]);
            }
            if (errPipe[0] >= 0)
            {
                FD_SET(errPipe[0], &fds);
                maxfd = std::max(maxfd, errPipe[0]);
            }

            // If both pipes are closed, just wait for process end.
            if (maxfd < 0)
            {
                int status = 0;
                pid_t r = ::waitpid(pid, &status, 0);
                if (r == pid)
                {
                    finalStatus = status;
                    haveStatus = true;
                    running = false;
                }
                break;
            }

            struct timeval tv;
            struct timeval *tv_ptr = nullptr;
            if (spinnerActive)
            {
                tv.tv_sec = 0;
                tv.tv_usec = 100000;
                tv_ptr = &tv;
            }

            const int ready = ::select(maxfd + 1, &fds, nullptr, nullptr, tv_ptr);

            if (ready < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }

            if (ready == 0)
            {
                if (spinnerActive)
                    spinner_draw(spinnerLabel, frameIndex);
            }
            else
            {
                if (spinnerActive)
                {
                    spinner_clear();
                    spinnerActive = false;
                }

                // stdout
                if (outPipe[0] >= 0 && FD_ISSET(outPipe[0], &fds))
                {
                    std::string chunk;
                    if (read_into(outPipe[0], chunk))
                    {
                        result.stdoutText += chunk;

                        if (!should_drop_chunk_default(chunk))
                        {
                            std::string printable = sanitizer.filter_for_print(chunk);
                            if (!printable.empty())
                            {
                                std::string filtered;
                                if (passthroughRuntime)
                                {
                                    filtered = printable;
                                }
                                else
                                {
                                    filtered = runtimeFilter.process(printable, STDOUT_FILENO);
                                }

                                if (!filtered.empty())
                                    write_safe(STDOUT_FILENO, filtered.data(),
                                               static_cast<ssize_t>(filtered.size()));
                            }
                        }
                    }
                    else
                    {
                        close_safe(outPipe[0]);
                    }
                }

                // stderr
                if (errPipe[0] >= 0 && FD_ISSET(errPipe[0], &fds))
                {
                    std::string chunk;
                    if (read_into(errPipe[0], chunk))
                    {
                        // Always capture full stderr for detectors
                        result.stderrText += chunk;

                        if (!should_drop_chunk_default(chunk))
                        {
                            std::string printable = sanitizer.filter_for_print(chunk);
                            if (!printable.empty())
                            {
                                std::string filtered;
                                if (passthroughRuntime)
                                {
                                    filtered = printable; // script mode: don't wait for markers
                                }
                                else
                                {
                                    filtered = runtimeFilter.process(printable, STDERR_FILENO);
                                }

                                if (!filtered.empty())
                                    write_safe(STDERR_FILENO, filtered.data(),
                                               static_cast<ssize_t>(filtered.size()));
                            }
                        }
                    }
                    else
                    {
                        close_safe(errPipe[0]);
                    }
                }
            }

            // Check child status without blocking
            int status = 0;
            pid_t r = ::waitpid(pid, &status, WNOHANG);
            if (r == pid)
            {
                finalStatus = status;
                haveStatus = true;
                running = false;
            }
        }

        if (useSpinner)
            spinner_clear();

        close_safe(outPipe[0]);
        close_safe(errPipe[0]);

        // If we never captured status, wait once
        if (!haveStatus)
        {
            int status = 0;
            if (::waitpid(pid, &status, 0) == pid)
            {
                finalStatus = status;
                haveStatus = true;
            }
        }

        // IMPORTANT: store RAW wait-status here.
        result.exitCode = haveStatus ? finalStatus : 1;
        return result;
    }

#endif // !_WIN32

    // Non-capturing legacy API (declared in RunDetail.hpp)
    int run_cmd_live_filtered(const std::string &cmd,
                              const std::string &spinnerLabel)
    {
#ifdef _WIN32
        (void)spinnerLabel;
        return std::system(cmd.c_str());
#else
        LiveRunResult r = run_cmd_live_filtered_capture(cmd, spinnerLabel, false);
        return normalize_exit_code(r.exitCode); // ✅

#endif
    }

} // namespace vix::commands::RunCommand::detail
