/**
 *
 *  @file RunProcess.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/run/RunDetail.hpp>

#include <vix/cli/Style.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>

#ifndef _WIN32
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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

  class RuntimeOutputFilter
  {
  public:
    std::string process(const std::string &chunk, int fdForBuildAndRuntime)
    {
      if (clearedForRuntime_)
        return chunk;

      buffer_ += chunk;

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
      update_min(buffer_.find("● VIX"));
      update_min(buffer_.find("VIX.cpp   READY"));
      update_min(buffer_.find("› HTTP:"));
      update_min(buffer_.find("› WS:"));
      update_min(buffer_.find("i Threads:"));
      update_min(buffer_.find("i Mode:"));
      update_min(buffer_.find("i Status:"));
      update_min(buffer_.find("Vix.cpp v"));

      if (first == std::string::npos)
      {
        std::string prefix = flush_prefix_if_needed();
        if (!prefix.empty())
        {
          write_safe(fdForBuildAndRuntime,
                     prefix.data(),
                     static_cast<ssize_t>(prefix.size()));
        }
        return {};
      }

      clearedForRuntime_ = true;
      buffer_.erase(0, first);

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

  static inline bool is_cmake_configure_cmd(const std::string &cmd) noexcept
  {
    // Configure = cmake ..  OR  cmake --preset <x>
    // Build = cmake --build ...
    const bool isCmake = (cmd.find("cmake") != std::string::npos);
    const bool isBuild = (cmd.find("--build") != std::string::npos);
    const bool isPreset = (cmd.find("--preset") != std::string::npos);
    const bool isDotDot = (cmd.find("cmake ..") != std::string::npos) || (cmd.find("cmake  ..") != std::string::npos);

    return isCmake && !isBuild && (isPreset || isDotDot);
  }

  static inline bool looks_like_error_or_warning(std::string_view line) noexcept
  {
    // (CMake Error:, CMake Warning:, error:, warning:, etc.)
    auto has = [&](std::string_view s)
    { return line.find(s) != std::string_view::npos; };

    if (has("CMake Error") || has("CMake Warning"))
      return true;
    if (has("error:") || has("Error:") || has("ERROR:"))
      return true;
    if (has("warning:") || has("Warning:") || has("WARNING:"))
      return true;

    return false;
  }

  struct CMakeNoiseFilter
  {
    std::string carry;

    std::string filter(const std::string &chunk)
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

        if (looks_like_error_or_warning(line))
        {
          out.append(line.data(), line.size());
          continue;
        }

        if (line.rfind("-- ", 0) == 0)
        {
          if (line.find("Configuring done") != std::string_view::npos ||
              line.find("Generating done") != std::string_view::npos ||
              line.find("Build files have been written to:") != std::string_view::npos)
          {
            out.append(line.data(), line.size());
          }
          continue;
        }

        if (line.find("Preset CMake variables:") != std::string_view::npos)
          continue;

        if (line.rfind("  CMAKE_", 0) == 0)
          continue;

        out.append(line.data(), line.size());
      }

      return out;
    }
  };

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
    static const std::string ninjaStop = "ninja: build stopped: interrupted by user.";
    if (chunk.find(ninjaStop) != std::string::npos)
      return true;

    // Drop Ninja command echo lines like:
    // [0/1] cd ... && /path/to/bin
    // [1/2] Linking CXX executable ...
    if (!chunk.empty() && chunk[0] == '[')
    {
      const auto rb = chunk.find(']');
      if (rb != std::string::npos)
      {
        // cheap heuristic: "[digits/...]" then space
        bool ok = true;
        for (std::size_t i = 1; i < rb; ++i)
        {
          const char c = chunk[i];
          if (!(c >= '0' && c <= '9') && c != '/')
          {
            ok = false;
            break;
          }
        }

        if (ok)
          return true;
      }
    }

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

  LiveRunResult run_cmd_live_filtered_capture(
      const std::string &cmd,
      const std::string &spinnerLabel,
      bool passthroughRuntime,
      int timeoutSec)
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

      if (::getenv("VIX_MODE") == nullptr)
      {
        ::setenv("VIX_MODE", "run", 1);
      }
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

    const bool cmakeConfigure = is_cmake_configure_cmd(cmd);
    CMakeNoiseFilter cmakeNoise;

    struct SanitizerSuppressor
    {
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
          if (is_sanitizer_keyword_line(line) || line.find("Sanitizer") != std::string_view::npos)
            return true;
        }

        if (is_sanitizer_keyword_line(line))
          return true;

        // UBSan sometimes prints: "runtime error: ..."
        if (line.find("runtime error:") != std::string_view::npos)
          return true;

        return false;
      }

      static bool is_report_end(std::string_view line) noexcept
      {
        if (line.rfind("SUMMARY:", 0) == 0)
          return true;

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
              continue;
            }
            out.append(line.data(), line.size());
          }
          else
          {
            if (is_report_end(line))
            {
              inReport = false;
              continue;
            }
          }
        }

        return out;
      }
    };

    SanitizerSuppressor sanitizer;

    bool running = true;
    bool printedSomething = false;
    char lastPrintedChar = '\n';

    int finalStatus = 0;
    bool haveStatus = false;

    const bool enableTimeout = (timeoutSec > 0);
    const auto startTime = std::chrono::steady_clock::now();
    bool didTimeout = false;

    bool sentTerm = false;
    bool sentKill = false;
    auto termTime = startTime;

    auto elapsed_sec = [&]() -> long long
    {
      using namespace std::chrono;
      return duration_cast<seconds>(steady_clock::now() - startTime).count();
    };

    auto term_elapsed_ms = [&]() -> long long
    {
      using namespace std::chrono;
      return duration_cast<milliseconds>(steady_clock::now() - termTime).count();
    };

    while (running)
    {
      if (!didTimeout && enableTimeout && elapsed_sec() >= timeoutSec)
      {
        didTimeout = true;

        const std::string msg =
            "\n[vix] runtime timeout (" + std::to_string(timeoutSec) + "s)\n";
        result.stderrText += msg;

        ::kill(pid, SIGTERM);
        sentTerm = true;
        termTime = std::chrono::steady_clock::now();
      }

      if (didTimeout && sentTerm && !sentKill)
      {
        if (term_elapsed_ms() >= 500)
        {
          ::kill(pid, SIGKILL);
          sentKill = true;
        }
      }

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

      if (spinnerActive || enableTimeout)
      {
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
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
              if (!printable.empty() && cmakeConfigure)
                printable = cmakeNoise.filter(printable);

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
                {
                  write_safe(STDOUT_FILENO, filtered.data(),
                             static_cast<ssize_t>(filtered.size()));

                  printedSomething = true;
                  lastPrintedChar = filtered.back();
                }
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
            result.stderrText += chunk;

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
                  filtered = runtimeFilter.process(printable, STDERR_FILENO);
                }

                if (!filtered.empty())
                {
                  write_safe(STDERR_FILENO, filtered.data(),
                             static_cast<ssize_t>(filtered.size()));

                  printedSomething = true;
                  lastPrintedChar = filtered.back();
                }
              }
            }
          }
          else
          {
            close_safe(errPipe[0]);
          }
        }
      }

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

    if (!haveStatus)
    {
      int status = 0;
      if (::waitpid(pid, &status, 0) == pid)
      {
        finalStatus = status;
        haveStatus = true;
      }
    }

    result.rawStatus = haveStatus ? finalStatus : 0;

    if (didTimeout)
    {
      result.exitCode = 124;
      return result;
    }

    result.exitCode = haveStatus ? normalize_exit_code(finalStatus) : 1;
#ifndef _WIN32
    if (printedSomething &&
        lastPrintedChar != '\n' &&
        lastPrintedChar != '\r' &&
        ::isatty(STDOUT_FILENO) != 0)
    {
      const char nl = '\n';
      write_safe(STDOUT_FILENO, &nl, 1);
    }
#endif

    return result;
  }

#endif // !_WIN32

  int run_cmd_live_filtered(const std::string &cmd,
                            const std::string &spinnerLabel)
  {
#ifdef _WIN32
    (void)spinnerLabel;
    return std::system(cmd.c_str());
#else
    LiveRunResult r = run_cmd_live_filtered_capture(cmd, spinnerLabel, false);
    return r.exitCode;

#endif
  }

} // namespace vix::commands::RunCommand::detail
