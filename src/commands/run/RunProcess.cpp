#include "vix/cli/commands/run/RunDetail.hpp"
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>     // pipe, dup2, read, write, close
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid, WIFEXITED, WEXITSTATUS
#include <sys/select.h> // select, fd_set, FD_SET, FD_ZERO, FD_ISSET
#include <signal.h>     // sigaction, SIGINT
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{

#ifndef _WIN32
    /// RAII helper that temporarily ignores SIGINT in the parent process
    /// while the child build/run process is executing.
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
            {
                sigaction(SIGINT, &oldAction, nullptr);
            }
        }
    };

    /// Thin wrapper around write(2), best-effort only.
    inline void write_safe(int fd, const char *buf, ssize_t n)
    {
        if (n > 0)
        {
            const ssize_t written = ::write(fd, buf, static_cast<size_t>(n));
            (void)written;
        }
    }

    /// Output filter that:
    ///  - initially streams build logs (Ninja, gmake, etc.),
    ///  - as soon as it detects runtime logs (`[I]`/`[W]`/`[E]` or
    ///    "Using configuration file:"), it:
    ///      * clears the screen,
    ///      * prints a small header,
    ///      * then only shows runtime logs from that point on.
    class RuntimeOutputFilter
    {
    public:
        /// Processes a new chunk coming from stdout/stderr.
        /// Returns the part that should be written by the caller.
        /// May also write directly to STDOUT (for clear-screen + header).
        std::string process(const std::string &chunk, int fdForBuildAndRuntime)
        {
            if (clearedForRuntime_)
            {
                // Once in "runtime only" mode, we forward everything as-is.
                return chunk;
            }

            buffer_ += chunk;

            // Look for the first marker indicating that the runtime is speaking.
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
                // Still no runtime log detected → progressively flush
                // prefix of the buffer (build logs) to avoid unbounded growth.
                std::string prefix = flush_prefix_if_needed();
                if (!prefix.empty())
                {
                    write_safe(fdForBuildAndRuntime,
                               prefix.data(),
                               static_cast<ssize_t>(prefix.size()));
                }
                return {};
            }

            // 🔥 First time we detect a runtime log.
            clearedForRuntime_ = true;

            // Drop everything before the runtime marker.
            buffer_.erase(0, first);

            // Clear the terminal and move cursor to home.
            const char *clearScreen = "\033[2J\033[H";
            write_safe(STDOUT_FILENO, clearScreen,
                       static_cast<ssize_t>(std::strlen(clearScreen)));

            // Dev-server-like header (Vite-style).
            std::string header;
            header += "Vix.cpp runtime is ready 🚀\n\n";
            header += "Logs:\n\n";
            write_safe(STDOUT_FILENO,
                       header.c_str(),
                       static_cast<ssize_t>(header.size()));

            // Everything left in the buffer is now considered runtime output.
            std::string out = buffer_;
            buffer_.clear();
            return out;
        }

    private:
        // Keep a small tail in the buffer to avoid breaking markers
        // that may be split between read() calls.
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
#endif // _WIN32

    int run_cmd_live_filtered(const std::string &cmd,
                              const std::string &spinnerLabel)
    {
#ifdef _WIN32
        (void)spinnerLabel;
        return std::system(cmd.c_str());
#else
        SigintGuard sigGuard;

        int outPipe[2];
        int errPipe[2];

        if (pipe(outPipe) != 0 || pipe(errPipe) != 0)
        {
            return std::system(cmd.c_str());
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            close(outPipe[0]);
            close(outPipe[1]);
            close(errPipe[0]);
            close(errPipe[1]);
            return std::system(cmd.c_str());
        }

        if (pid == 0)
        {
            // ===== Child process =====
            struct sigaction saChild{};
            saChild.sa_handler = SIG_DFL;
            sigemptyset(&saChild.sa_mask);
            saChild.sa_flags = 0;
            sigaction(SIGINT, &saChild, nullptr);

            close(outPipe[0]);
            close(errPipe[0]);

            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);

            close(outPipe[1]);
            close(errPipe[1]);

            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
            _exit(127);
        }

        // ===== Parent process =====
        close(outPipe[1]);
        close(errPipe[1]);

        fd_set fds;
        bool running = true;
        int exitCode = 0;

        const std::string ninjaStop = "ninja: build stopped: interrupted by user.";

        auto should_drop_chunk = [&](const std::string &chunk) -> bool
        {
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
        };

        // --- Spinner state ---
        const bool useSpinner = !spinnerLabel.empty();
        bool spinnerActive = useSpinner;

        static const char *frames[] = {
            "⠋", "⠙", "⠹", "⠸",
            "⠼", "⠴", "⠦", "⠧",
            "⠇", "⠏"};
        const std::size_t frameCount = sizeof(frames) / sizeof(frames[0]);
        std::size_t frameIndex = 0;

        auto draw_spinner = [&]()
        {
            if (!spinnerActive)
                return;

            std::string line = "\r┃   ";
            line += frames[frameIndex];
            line += " ";
            line += spinnerLabel;
            line += "   ";
            write_safe(STDOUT_FILENO, line.c_str(),
                       static_cast<ssize_t>(line.size()));

            frameIndex = (frameIndex + 1) % frameCount;
        };

        auto clear_spinner = [&]()
        {
            if (!useSpinner)
                return;

            const char *clearLine =
                "\r                                                                                \r";
            write_safe(STDOUT_FILENO, clearLine,
                       static_cast<ssize_t>(std::strlen(clearLine)));
        };

        // Runtime filter (clear + header on first runtime log).
        RuntimeOutputFilter runtimeFilter;

        while (running)
        {
            FD_ZERO(&fds);
            FD_SET(outPipe[0], &fds);
            FD_SET(errPipe[0], &fds);

            int maxfd = std::max(outPipe[0], errPipe[0]) + 1;

            struct timeval tv;
            struct timeval *tv_ptr = nullptr;
            if (spinnerActive)
            {
                tv.tv_sec = 0;
                tv.tv_usec = 100000; // 100 ms
                tv_ptr = &tv;
            }

            int ready = select(maxfd, &fds, nullptr, nullptr, tv_ptr);

            if (ready < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }

            if (ready == 0)
            {
                // No output yet → update spinner.
                draw_spinner();
            }
            else
            {
                if (spinnerActive)
                {
                    clear_spinner();
                    spinnerActive = false;
                }

                // stdout
                if (FD_ISSET(outPipe[0], &fds))
                {
                    char buf[4096];
                    ssize_t n = read(outPipe[0], buf, sizeof(buf));
                    if (n > 0)
                    {
                        std::string chunk(buf, static_cast<std::size_t>(n));
                        if (!should_drop_chunk(chunk))
                        {
                            std::string filtered =
                                runtimeFilter.process(chunk, STDOUT_FILENO);

                            if (!filtered.empty())
                            {
                                write_safe(STDOUT_FILENO,
                                           filtered.data(),
                                           static_cast<ssize_t>(filtered.size()));
                            }
                        }
                    }
                }

                // stderr
                if (FD_ISSET(errPipe[0], &fds))
                {
                    char buf[4096];
                    ssize_t n = read(errPipe[0], buf, sizeof(buf));
                    if (n > 0)
                    {
                        std::string chunk(buf, static_cast<std::size_t>(n));
                        if (!should_drop_chunk(chunk))
                        {
                            std::string filtered =
                                runtimeFilter.process(chunk, STDERR_FILENO);

                            if (!filtered.empty())
                            {
                                write_safe(STDERR_FILENO,
                                           filtered.data(),
                                           static_cast<ssize_t>(filtered.size()));
                            }
                        }
                    }
                }
            }

            int status = 0;
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid)
            {
                running = false;

                if (WIFEXITED(status))
                {
                    exitCode = WEXITSTATUS(status);
                }
                else if (WIFSIGNALED(status))
                {
                    int sig = WTERMSIG(status);
                    if (sig == SIGINT)
                        exitCode = 130;
                    else
                        exitCode = 128 + sig;
                }
                else
                {
                    exitCode = 1;
                }
            }
        }

        clear_spinner();

        close(outPipe[0]);
        close(errPipe[0]);

        return exitCode;
#endif
    }

} // namespace vix::commands::RunCommand::detail
