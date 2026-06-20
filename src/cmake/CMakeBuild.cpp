/**
 *
 *  @file CMakeBuild.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/cmake/CMakeBuild.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

#include <vix/cli/Style.hpp>
#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Strings.hpp>

#include <vix/utils/Env.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef _WIN32
#include <cerrno>
#include <cstddef>

#ifndef _WIN32
#include <sys/ioctl.h>
#endif

namespace
{
  inline void write_all_fd(int fd, const char *data, std::size_t len) noexcept
  {
    while (len > 0)
    {
      const ssize_t w = ::write(fd, data, len);
      if (w > 0)
      {
        data += static_cast<std::size_t>(w);
        len -= static_cast<std::size_t>(w);
        continue;
      }

      if (w < 0 && errno == EINTR)
        continue;

      break;
    }
  }
} // namespace
#endif

#ifndef _WIN32
namespace
{
  std::size_t terminal_width() noexcept
  {
    struct winsize ws{};

    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
      return static_cast<std::size_t>(ws.ws_col);

    return 120;
  }

  std::string truncate_progress_text(const std::string &line, std::size_t maxWidth)
  {
    if (maxWidth == 0)
      return "";

    if (line.size() <= maxWidth)
      return line;

    if (maxWidth <= 3)
      return std::string(maxWidth, '.');

    const std::size_t keep = maxWidth - 3;
    return line.substr(0, keep) + "...";
  }
}
#endif

namespace vix::cli::build
{
  namespace util = vix::cli::util;
  namespace style = vix::cli::style;

  bool is_cmake_configure_summary_line(std::string_view line)
  {
    return (line.rfind("-- Configuring done", 0) == 0) ||
           (line.rfind("-- Generating done", 0) == 0) ||
           (line.find("Build files have been written to:") != std::string_view::npos);
  }

  bool is_configure_cmd(const std::vector<std::string> &argv)
  {
    bool hasS = false;
    bool hasB = false;

    for (const auto &a : argv)
    {
      if (a == "-S")
        hasS = true;
      if (a == "-B")
        hasB = true;
    }

    return !argv.empty() && argv[0] == "cmake" && hasS && hasB;
  }

  int default_jobs()
  {
    unsigned int hc = std::thread::hardware_concurrency();
    if (hc == 0)
      return 4;
    if (hc > 64)
      hc = 64;
    return static_cast<int>(hc);
  }

  std::vector<std::string> cmake_configure_argv(
      const process::Plan &plan,
      const process::Options &opt)
  {
    std::vector<std::string> argv;
    argv.reserve(32);

    const fs::path cmakeSourceDir =
        !plan.cmakeSourceDir.empty()
            ? plan.cmakeSourceDir
            : plan.projectDir;

    argv.push_back("cmake");
    argv.push_back(opt.cmakeVerbose ? "--log-level=VERBOSE" : "--log-level=WARNING");

    argv.push_back("-S");
    argv.push_back(cmakeSourceDir.string());
    argv.push_back("-B");
    argv.push_back(plan.buildDir.string());
    argv.push_back("-G");
    argv.push_back(plan.preset.generator);

    for (const auto &kv : plan.cmakeVars)
      argv.push_back("-D" + kv.first + "=" + kv.second);

    for (const auto &a : opt.cmakeArgs)
      argv.push_back(a);

    return argv;
  }

  std::vector<std::string> cmake_build_argv(
      const process::Plan &plan,
      const process::Options &opt)
  {
    std::vector<std::string> argv;
    argv.reserve(18);

    argv.push_back("cmake");
    argv.push_back("--build");
    argv.push_back(plan.buildDir.string());

    int jobs = opt.jobs;
    if (jobs <= 0)
      jobs = default_jobs();

    if (!opt.buildTarget.empty() && opt.buildTarget != "all")
    {
      argv.push_back("--target");
      argv.push_back(opt.buildTarget);
    }

    argv.push_back("--");
    argv.push_back("-j");
    argv.push_back(std::to_string(jobs));

    return argv;
  }

  std::vector<std::string> ninja_dry_run_argv(
      const process::Plan &plan,
      const process::Options &opt)
  {
    (void)opt;
    return {"ninja", "-C", plan.buildDir.string(), "-n"};
  }

  std::vector<std::pair<std::string, std::string>>
  ninja_env(const process::Options &opt, const process::Plan &plan)
  {
    std::vector<std::pair<std::string, std::string>> env;

    if (!opt.status)
      return env;

    if (plan.preset.generator == "Ninja")
      env.emplace_back("NINJA_STATUS", "[%f/%t] ");

    return env;
  }

#ifndef _WIN32

  process::ExecResult run_process_capture(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      std::string &outText)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
      r.exitCode = 127;
      return r;
    }

    pid_t pid = ::fork();
    if (pid == 0)
    {
      ::dup2(pipefd[1], STDOUT_FILENO);
      ::dup2(pipefd[1], STDERR_FILENO);
      ::close(pipefd[0]);
      ::close(pipefd[1]);

      for (const auto &kv : extraEnv)
        ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

      std::vector<char *> cargv;
      cargv.reserve(argv.size() + 1);
      for (const auto &s : argv)
        cargv.push_back(const_cast<char *>(s.c_str()));
      cargv.push_back(nullptr);

      ::execvp(cargv[0], cargv.data());
      _exit(127);
    }

    ::close(pipefd[1]);

    std::string buf(8 * 1024, '\0');
    while (true)
    {
      const ssize_t n = ::read(pipefd[0], &buf[0], buf.size());
      if (n > 0)
        outText.append(buf.data(), static_cast<std::size_t>(n));
      else
        break;
    }

    ::close(pipefd[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0)
    {
      r.exitCode = 127;
      return r;
    }

    r.exitCode = process::normalize_exit_code(status);
    return r;
  }

  process::ExecResult run_process_live_to_log(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      const fs::path &logPath,
      bool quiet,
      bool cmakeVerbose,
      bool progressOnly)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    const bool filterCMakeSummary = is_configure_cmd(argv) && !cmakeVerbose;

    const bool isConfigure = is_configure_cmd(argv);

    const bool heartbeatEnabled = [&]() -> bool
    {
      if (quiet)
        return false;

      /*
       * Configure can stay silent for a long time when CMake FetchContent
       * downloads dependencies. Keep a heartbeat enabled by default for
       * configure, while still allowing explicit control through the env var.
       */
      const char *v = vix::utils::vix_getenv("VIX_BUILD_HEARTBEAT");
      if (!v || !*v)
        return isConfigure;

      std::string s(v);
      s = util::trim(s);
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      if (s == "0" || s == "false" || s == "no" || s == "off")
        return false;

      return (s == "1" || s == "true" || s == "yes" || s == "on");
    }();

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
      r.exitCode = 127;
      return r;
    }

    int logfd = ::open(logPath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (logfd < 0)
    {
      ::close(pipefd[0]);
      ::close(pipefd[1]);
      r.exitCode = 127;
      return r;
    }

    pid_t pid = ::fork();
    if (pid == 0)
    {
      ::dup2(pipefd[1], STDOUT_FILENO);
      ::dup2(pipefd[1], STDERR_FILENO);

      ::close(pipefd[0]);
      ::close(pipefd[1]);

      for (const auto &kv : extraEnv)
        ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

      std::vector<char *> cargv;
      cargv.reserve(argv.size() + 1);
      for (const auto &s : argv)
        cargv.push_back(const_cast<char *>(s.c_str()));
      cargv.push_back(nullptr);

      ::execvp(cargv[0], cargv.data());
      _exit(127);
    }

    ::close(pipefd[1]);

    std::string firstLine;
    bool gotFirstLine = false;

    std::string consoleBuf;
    consoleBuf.reserve(8192);

    std::string currentProgressLine;
    bool progressVisible = false;
    bool heartbeatVisible = false;
    std::size_t lastRenderedWidth = 0;
    std::string lastRenderedProgressLine;
    auto lastProgressRenderTs = std::chrono::steady_clock::now();

    auto is_progress_line = [](const std::string &line) -> bool
    {
      if (line.empty())
        return false;
      if (line[0] != '[')
        return false;

      const auto rb = line.find(']');
      if (rb == std::string::npos)
        return false;

      const auto slash = line.find('/', 1);
      if (slash == std::string::npos || slash > rb)
        return false;

      return true;
    };

    auto should_skip_progress_line = [](const std::string &line) -> bool
    {
      return line.find("Copy compile_commands.json to project root") != std::string::npos ||
             line.find("Re-checking globbed directories") != std::string::npos;
    };

    auto clear_progress_line = [&]() -> void
    {
      if (quiet || !progressVisible)
        return;

      std::string clear;
      clear += "\r\033[2K";
      clear += "\n\r\033[2K";
      clear += "\033[1A\r";

      write_all_fd(STDOUT_FILENO, clear.data(), clear.size());

      progressVisible = false;
      lastRenderedWidth = 0;
    };

    auto clear_rendered_progress_lines = [&]() -> void
    {
      if (quiet || !progressVisible)
        return;

      std::string clear;

      clear += "\033[2A\r";
      clear += "\033[2K";
      clear += "\n\r";
      clear += "\033[2K";
      clear += "\033[1A\r";

      write_all_fd(STDOUT_FILENO, clear.data(), clear.size());
    };

    auto render_progress_line = [&](const std::string &line) -> void
    {
      if (quiet || line.empty())
        return;

      const auto now = std::chrono::steady_clock::now();
      const auto elapsedMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              now - lastProgressRenderTs)
              .count();

      if (line == lastRenderedProgressLine && progressVisible)
        return;

      if (progressVisible && elapsedMs < 120)
        return;

      lastProgressRenderTs = now;
      lastRenderedProgressLine = line;

      std::size_t slash = line.find('/');
      std::size_t rb = line.find(']');

      int current = 0;
      int total = 1;
      std::string rest = line;

      if (line[0] == '[' &&
          slash != std::string::npos &&
          rb != std::string::npos &&
          slash < rb)
      {
        try
        {
          current = std::stoi(line.substr(1, slash - 1));
          total = std::stoi(line.substr(slash + 1, rb - slash - 1));
          rest = line.size() > rb + 2 ? line.substr(rb + 2) : "";
        }
        catch (...)
        {
          current = 0;
          total = 1;
          rest = line;
        }
      }

      const std::size_t width = terminal_width();

      int barWidth = 28;

      if (width < 90)
        barWidth = 18;

      if (width < 60)
        barWidth = 10;

      if (width < 45)
        barWidth = 0;

      const int filled =
          total > 0 && barWidth > 0
              ? std::clamp(
                    static_cast<int>(
                        (static_cast<double>(current) / static_cast<double>(total)) *
                        static_cast<double>(barWidth)),
                    0,
                    barWidth)
              : 0;

      std::string bar;

      if (barWidth > 0)
      {
        bar += style::GRAY;
        bar += "[";
        bar += style::CYAN;
        bar.append(static_cast<std::size_t>(filled), '=');
        bar += style::GRAY;
        bar.append(static_cast<std::size_t>(barWidth - filled), '-');
        bar += "]";
        bar += style::RESET;
      }

      const std::string action =
          truncate_progress_text(
              rest,
              width > 8 ? width - 8 : width);

      std::ostringstream lineOut;

      lineOut << "  "
              << style::CYAN << "build " << style::RESET;

      if (!bar.empty())
        lineOut << bar << " ";

      lineOut << style::CYAN << current << "/" << total << style::RESET
              << "\n"
              << "  "
              << style::CYAN << "› " << style::RESET
              << action
              << "\n";

      if (progressVisible)
        clear_rendered_progress_lines();

      const std::string rendered = lineOut.str();
      write_all_fd(STDOUT_FILENO, rendered.data(), rendered.size());

      progressVisible = true;
      lastRenderedWidth = width;
    };

    auto keep_progress_line = [&]() -> void
    {
      if (quiet || !progressVisible)
        return;

      const std::size_t width = terminal_width();

      int barWidth = 28;

      if (width < 90)
        barWidth = 18;

      if (width < 60)
        barWidth = 10;

      if (width < 45)
        barWidth = 0;

      clear_rendered_progress_lines();

      std::string out;
      out += "  ";
      out += style::CYAN;
      out += "build ";
      if (barWidth > 0)
      {
        out += "[";
        out.append(static_cast<std::size_t>(barWidth), '=');
        out += "] ";
      }

      out += "done";
      out += style::RESET;
      out += "\n";

      write_all_fd(STDOUT_FILENO, out.data(), out.size());

      progressVisible = false;
      lastRenderedWidth = 0;
      currentProgressLine.clear();
      lastRenderedProgressLine.clear();
    };

    auto clear_progress_line_for_error = [&]() -> void
    {
      if (quiet || !progressVisible)
        return;

      clear_rendered_progress_lines();

      progressVisible = false;
      lastRenderedWidth = 0;
      currentProgressLine.clear();
      lastRenderedProgressLine.clear();
    };

    auto finish_heartbeat_line = [&]() -> void
    {
      if (quiet || !heartbeatVisible)
        return;

      const std::string newline = "\n";
      write_all_fd(STDOUT_FILENO, newline.data(), newline.size());

      heartbeatVisible = false;
    };

    auto should_echo_line = [&](const std::string &line) -> bool
    {
      if (quiet)
        return false;

      if (filterCMakeSummary)
        return false;

      if (!progressOnly)
        return true;

      if (line == "ninja: build stopped: subcommand failed.")
        return false;

      if (line == "ninja: no work to do.")
        return false;

      if (line.find("Re-running CMake...") != std::string::npos)
        return false;

      if (line.find("Copy compile_commands.json to project root") != std::string::npos)
        return false;

      /*
       * In progress-only mode, keep raw compiler/linker errors out of the live
       * console. They are already written to build.log and will be rendered by
       * ErrorHandler with the unified Vix diagnostic style after the process exits.
       */
      return false;
    };

    std::string buf(16 * 1024, '\0');

    const auto startTs = std::chrono::steady_clock::now();
    auto lastOutputTs = startTs;
    auto lastHeartbeatTs = startTs;
    bool heartbeatPrinted = false;

    while (true)
    {
      struct pollfd pfd;
      pfd.fd = pipefd[0];
      pfd.events = POLLIN | POLLHUP | POLLERR;
      pfd.revents = 0;

      const int pr = ::poll(&pfd, 1, 250);

      if (pr < 0)
      {
        if (errno == EINTR)
          continue;
        break;
      }

      if (pr > 0 && (pfd.revents & POLLIN))
      {
        const ssize_t n = ::read(pipefd[0], &buf[0], buf.size());
        if (n <= 0)
          break;

        r.producedOutput = true;
        lastOutputTs = std::chrono::steady_clock::now();

        write_all_fd(logfd, buf.data(), static_cast<std::size_t>(n));

        if (!gotFirstLine)
        {
          for (ssize_t i = 0; i < n; ++i)
          {
            const char c = buf[static_cast<std::size_t>(i)];
            if (c == '\n')
            {
              gotFirstLine = true;
              break;
            }
            if (firstLine.size() < 200)
              firstLine.push_back(c);
          }
        }

        if (!quiet)
        {
          consoleBuf.append(buf.data(), static_cast<std::size_t>(n));

          std::size_t start = 0;
          while (true)
          {
            const std::size_t nl = consoleBuf.find('\n', start);
            if (nl == std::string::npos)
              break;

            std::string line = consoleBuf.substr(start, nl - start);

            if (progressOnly && is_progress_line(line))
            {
              if (!should_skip_progress_line(line))
              {
                currentProgressLine = line;
                render_progress_line(currentProgressLine);
              }
            }
            else if (should_echo_line(line))
            {
              line.push_back('\n');
              write_all_fd(STDOUT_FILENO, line.data(), line.size());
              heartbeatVisible = true;
            }

            start = nl + 1;
          }

          if (start > 0)
            consoleBuf.erase(0, start);
        }

        continue;
      }

      if (pr > 0 && (pfd.revents & (POLLHUP | POLLERR)))
        break;

      if (heartbeatEnabled)
      {
        const auto now = std::chrono::steady_clock::now();
        const auto silenceMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastOutputTs).count();
        const auto hbMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeatTs).count();

        if (silenceMs >= 5000 && hbMs >= 5000)
        {
          lastHeartbeatTs = now;
          const auto elapsedMs =
              std::chrono::duration_cast<std::chrono::milliseconds>(now - startTs).count();

          if (progressVisible)
            clear_progress_line();

          std::string msg;

          if (isConfigure)
          {
            msg =
                "  " +
                std::string(style::CYAN) +
                "configure" +
                style::RESET +
                " still running... " +
                style::GRAY +
                "(" + util::format_seconds(elapsedMs) + ", checking/downloading dependencies)" +
                style::RESET;
          }
          else
          {
            msg =
                "  " +
                std::string(style::CYAN) +
                "build" +
                style::RESET +
                " still running... " +
                style::GRAY +
                "(" + util::format_seconds(elapsedMs) + ")" +
                style::RESET;
          }

          const std::size_t width = terminal_width();

          std::string line;
          line += "\r";
          line.append(width, ' ');
          line += "\r";
          line += truncate_progress_text(msg, width);

          write_all_fd(STDOUT_FILENO, line.data(), line.size());
          heartbeatVisible = true;

          if (!currentProgressLine.empty())
            render_progress_line(currentProgressLine);

          heartbeatPrinted = true;
        }
      }
    }

    if (!quiet && !consoleBuf.empty())
    {
      if (progressOnly && is_progress_line(consoleBuf))
      {
        if (!should_skip_progress_line(consoleBuf))
        {
          currentProgressLine = consoleBuf;
          render_progress_line(currentProgressLine);
        }
      }
      else if (should_echo_line(consoleBuf))
      {
        std::string tail = consoleBuf + "\n";
        write_all_fd(STDOUT_FILENO, tail.data(), tail.size());
      }
    }

    ::close(pipefd[0]);
    ::close(logfd);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0)
    {
      if (!quiet && progressVisible)
        clear_progress_line_for_error();

      r.exitCode = 127;
      return r;
    }

    r.exitCode = process::normalize_exit_code(status);

    if (!quiet && progressVisible)
    {
      if (r.exitCode == 0)
        keep_progress_line();
      else
        clear_progress_line_for_error();
    }

    if (heartbeatEnabled && heartbeatPrinted)
    {
      // nothing else to clear because heartbeat is now printed on its own line
    }

    r.capturedFirstLine = util::trim(firstLine);
    finish_heartbeat_line();
    return r;
  }

#else // _WIN32

  process::ExecResult run_process_capture(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      std::string &outText)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    for (const auto &kv : extraEnv)
    {
      std::string e = kv.first + "=" + kv.second;
      (void)_putenv(e.c_str());
    }

    fs::path tmp = fs::temp_directory_path() / "vix_build_capture.tmp";

    std::ostringstream oss;
    for (std::size_t i = 0; i < argv.size(); ++i)
    {
      if (i)
        oss << " ";
      oss << "\"" << argv[i] << "\"";
    }

    const std::string cmd = oss.str() + " > \"" + tmp.string() + "\" 2>&1";
    const int raw = std::system(cmd.c_str());
    r.exitCode = process::normalize_exit_code(raw);

    outText = util::read_text_file_or_empty(tmp);
    std::error_code ec{};
    fs::remove(tmp, ec);
    return r;
  }

  process::ExecResult run_process_live_to_log(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      const fs::path &logPath,
      bool quiet,
      bool cmakeVerbose,
      bool progressOnly)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    (void)cmakeVerbose;
    (void)progressOnly;

    for (const auto &kv : extraEnv)
    {
      std::string e = kv.first + "=" + kv.second;
      (void)_putenv(e.c_str());
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < argv.size(); ++i)
    {
      if (i)
        oss << " ";
      oss << "\"" << argv[i] << "\"";
    }

    const std::string full = oss.str() + " > \"" + logPath.string() + "\" 2>&1";
    const int raw = std::system(full.c_str());
    r.exitCode = process::normalize_exit_code(raw);

    if (!quiet)
      std::cerr << util::read_text_file_or_empty(logPath);

    return r;
  }

#endif // _WIN32

  bool ninja_is_up_to_date(const process::Options &opt, const process::Plan &plan)
  {
    if (!opt.dryUpToDate)
      return false;
    if (plan.preset.generator != "Ninja")
      return false;

    std::string out;
    const process::ExecResult r = run_process_capture(
        ninja_dry_run_argv(plan, opt),
        ninja_env(opt, plan),
        out);

    if (r.exitCode != 0)
      return false;

    out = util::trim(out);
    return out.empty();
  }

  void print_preset_summary(const process::Options &opt, const process::Plan &plan)
  {
    if (opt.quiet)
      return;

    if (plan.launcher)
      style::step(std::string("compiler cache: ") + *plan.launcher);
    if (plan.fastLinkerFlag)
      style::step(std::string("fast linker: ") + *plan.fastLinkerFlag);

    for (const auto &kv : plan.cmakeVars)
      style::step(kv.first + "=" + kv.second);

    std::cout << "\n";
  }

} // namespace vix::cli::build
