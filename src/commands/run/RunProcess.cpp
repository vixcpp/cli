#include "vix/cli/commands/run/RunDetail.hpp"
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

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
    struct SigintGuard
    {
        struct sigaction oldAction{};
        bool installed = false;

        SigintGuard()
        {
            struct sigaction sa{};
            sa.sa_handler = SIG_IGN; // ignorer SIGINT dans le parent
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;

            if (sigaction(SIGINT, &sa, &oldAction) == 0)
                installed = true;
        }

        ~SigintGuard()
        {
            if (installed)
            {
                sigaction(SIGINT, &oldAction, nullptr); // restaurer le handler d’avant
            }
        }
    };
#endif
    inline void write_safe(int fd, const char *buf, ssize_t n)
    {
        if (n > 0)
        {
            const ssize_t written = ::write(fd, buf, static_cast<size_t>(n));
            (void)written;
        }
    }

    int run_cmd_live_filtered(const std::string &cmd)
    {
#ifdef _WIN32
        // Windows: on garde std::system, pas de filtrage avancé.
        return std::system(cmd.c_str());
#else
        SigintGuard sigGuard; // le parent ignore SIGINT pendant le build/run

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
            // ----- Child -----
            // On remet SIGINT par défaut dans l’enfant
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

        // ----- Parent -----
        close(outPipe[1]);
        close(errPipe[1]);

        fd_set fds;
        bool running = true;
        int exitCode = 0;

        const std::string ninjaStop = "ninja: build stopped: interrupted by user.";

        while (running)
        {
            FD_ZERO(&fds);
            FD_SET(outPipe[0], &fds);
            FD_SET(errPipe[0], &fds);

            int maxfd = std::max(outPipe[0], errPipe[0]) + 1;
            int ready = select(maxfd, &fds, nullptr, nullptr, nullptr);
            if (ready <= 0)
                continue;

            // stdout → live, mais on filtre la phrase ninja si elle tombe ici
            if (FD_ISSET(outPipe[0], &fds))
            {
                char buf[4096];
                ssize_t n = read(outPipe[0], buf, sizeof(buf));
                if (n > 0)
                {
                    std::string chunk(buf, static_cast<std::size_t>(n));
                    if (chunk.find(ninjaStop) == std::string::npos)
                    {
                        write_safe(STDOUT_FILENO, chunk.data(), static_cast<ssize_t>(chunk.size()));
                    }
                    // sinon: on jette ce morceau (ligne ninja)
                }
            }

            // stderr → idem, on filtre la phrase ninja
            if (FD_ISSET(errPipe[0], &fds))
            {
                char buf[4096];
                ssize_t n = read(errPipe[0], buf, sizeof(buf));
                if (n > 0)
                {
                    std::string chunk(buf, static_cast<std::size_t>(n));
                    if (chunk.find(ninjaStop) == std::string::npos)
                    {
                        write_safe(STDERR_FILENO, chunk.data(), static_cast<ssize_t>(chunk.size()));
                    }
                }
            }

            // vérifier si le process enfant est terminé
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
                        exitCode = 130; // convention: 128 + SIGINT
                    else
                        exitCode = 128 + sig;
                }
                else
                {
                    exitCode = 1;
                }
            }
        }

        close(outPipe[0]);
        close(errPipe[0]);

        return exitCode;
#endif
    }

} // namespace vix::commands::RunCommand::detail
