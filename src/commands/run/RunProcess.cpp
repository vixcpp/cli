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

  static inline void write_all(int fd, const char *buf, std::size_t n)
  {
    while (n > 0)
    {
      const ssize_t r = ::write(fd, buf, n);
      if (r > 0)
      {
        buf += static_cast<std::size_t>(r);
        n -= static_cast<std::size_t>(r);
        continue;
      }

      if (r < 0 && errno == EINTR)
        continue;

      break;
    }
  }

  static inline std::size_t utf8_safe_prefix_len(const std::string &s, std::size_t want)
  {
    if (want >= s.size())
      return s.size();
    std::size_t cut = want;

    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
      --cut;

    if (cut == 0)
      return 0;

    const unsigned char lead = static_cast<unsigned char>(s[cut]);
    std::size_t need = 1;

    if ((lead & 0x80) == 0x00)
      need = 1;
    else if ((lead & 0xE0) == 0xC0)
      need = 2;
    else if ((lead & 0xF0) == 0xE0)
      need = 3;
    else if ((lead & 0xF8) == 0xF0)
      need = 4;

    if (cut + need > want)
      return cut;

    return want;
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
    RuntimeOutputFilter()
        : startTime_(std::chrono::steady_clock::now())
    {
    }

    std::string process(const std::string &chunk)
    {
      static const bool debugMode = []()
      {
        const char *env = std::getenv("VIX_DEBUG_FILTER");
        return env && std::strcmp(env, "1") == 0;
      }();
      if (debugMode)
        return chunk;

      if (passthrough_)
        return chunk;

      buffer_ += chunk;

      const auto now = std::chrono::steady_clock::now();
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
      if (elapsed >= FORCE_PASSTHROUGH_TIMEOUT_SEC)
      {
        std::string out = flush_all_buffer_as_is();
        passthrough_ = true;

        if (should_clear() && !emittedAnything_)
          out = std::string("\033[2J\033[H") + out;

        emittedAnything_ = true;
        return out;
      }

      const std::size_t first = find_first_vix_marker(buffer_);
      if (first == std::string::npos)
      {
        std::string out = flush_lines_keep_tail();
        if (!out.empty())
          emittedAnything_ = true;
        return out;
      }

      runtimeDetected_ = true;
      passthrough_ = true;

      std::string out = buffer_.substr(first);
      buffer_.clear();

      if (should_clear() && !emittedAnything_)
        out = std::string("\033[2J\033[H") + out;

      emittedAnything_ = true;
      return out;
    }

    void reset()
    {
      buffer_.clear();
      emittedAnything_ = false;
      runtimeDetected_ = false;
      passthrough_ = false;
      startTime_ = std::chrono::steady_clock::now();
    }

    bool isRuntimeMode() const { return runtimeDetected_; }

  private:
    static constexpr std::size_t TAIL_BUFFER_SIZE = 1024;

    static constexpr int FORCE_PASSTHROUGH_TIMEOUT_SEC = 10;

    bool runtimeDetected_ = false;
    bool passthrough_ = false;
    bool emittedAnything_ = false;
    std::string buffer_;
    std::chrono::steady_clock::time_point startTime_;

    static bool should_clear()
    {
      const char *mode = std::getenv("VIX_CLI_CLEAR");
      if (!mode || !*mode)
        mode = "auto";

      if (std::strcmp(mode, "never") == 0)
        return false;

      if (std::strcmp(mode, "always") == 0)
        return true;

#ifndef _WIN32
      if (std::strcmp(mode, "auto") == 0)
        return ::isatty(STDOUT_FILENO) != 0;
#endif

      return false;
    }

    static std::size_t find_first_vix_marker(const std::string &text)
    {
      static const char *markers[] = {
          "● VIX",
          "VIX.cpp   READY",
          "Vix.cpp   READY",
          "VIX   READY",
          "› HTTP:",
          "› WS:",
          "i Threads:",
          "i Mode:",
          "i Status:",
          "i Hint:",
          "Using configuration file:",
          "Vix.cpp runtime",
          "Vix.cpp v",
          nullptr};

      std::size_t firstPos = std::string::npos;

      for (int i = 0; markers[i] != nullptr; ++i)
      {
        const std::size_t pos = text.find(markers[i]);
        if (pos != std::string::npos)
        {
          if (firstPos == std::string::npos || pos < firstPos)
            firstPos = pos;
        }
      }

      return firstPos;
    }

    std::string flush_lines_keep_tail()
    {
      if (buffer_.size() <= TAIL_BUFFER_SIZE)
        return {};

      const std::size_t keepFrom = buffer_.size() - TAIL_BUFFER_SIZE;

      const std::size_t lastNl = buffer_.rfind('\n', keepFrom);
      if (lastNl == std::string::npos)
        return {};

      const std::size_t flushLen = lastNl + 1;
      std::string out = buffer_.substr(0, flushLen);
      buffer_.erase(0, flushLen);
      return out;
    }

    std::string flush_all_buffer_as_is()
    {
      std::string out = buffer_;
      buffer_.clear();
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

  static inline void spinner_draw(
      const std::string &label,
      std::size_t &frameIndex,
      bool &printedSomething,
      char &lastPrintedChar)
  {
    static const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const std::size_t frameCount = sizeof(frames) / sizeof(frames[0]);

    std::string line = "\r┃   ";
    line += frames[frameIndex];
    line += " ";
    line += label;
    line += "   ";

    write_all(STDOUT_FILENO, line.c_str(), line.size());

    printedSomething = true;
    lastPrintedChar = '\r';

    frameIndex = (frameIndex + 1) % frameCount;
  }

  static inline void spinner_clear(bool &printedSomething, char &lastPrintedChar)
  {
    const char *clearLine = "\r                                                                                \r";
    write_all(STDOUT_FILENO, clearLine, std::strlen(clearLine));

    printedSomething = true;
    lastPrintedChar = '\r';
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
          spinner_draw(spinnerLabel, frameIndex, printedSomething, lastPrintedChar);
      }
      else
      {
        if (spinnerActive)
        {
          spinner_clear(printedSomething, lastPrintedChar);
          spinnerActive = false;

          if (::isatty(STDOUT_FILENO) != 0)
          {
            const char cr = '\r';
            write_all(STDOUT_FILENO, &cr, 1);
          }
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
                  filtered = runtimeFilter.process(printable);
                }

                if (!filtered.empty())
                {
                  write_all(STDOUT_FILENO, filtered.data(), filtered.size());

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
                  filtered = runtimeFilter.process(printable);
                }

                if (!filtered.empty())
                {
                  write_all(STDERR_FILENO, filtered.data(), filtered.size());

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

    if (spinnerActive)
      spinner_clear(printedSomething, lastPrintedChar);

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
        ::isatty(STDOUT_FILENO) != 0)
    {
      const char nl = '\n';
      write_all(STDOUT_FILENO, &nl, 1);
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
