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
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

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

  static volatile sig_atomic_t g_sigint_requested = 0;

  static void on_sigint(int) { g_sigint_requested = 1; }

  struct SigintGuard
  {
    struct sigaction oldAction{};
    bool installed = false;

    SigintGuard()
    {
      g_sigint_requested = 0;

      struct sigaction sa{};
      sa.sa_handler = on_sigint;
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

  static inline bool is_vix_error_tip_line(std::string_view line) noexcept
  {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
      line.remove_prefix(1);

    return (line.rfind("error:", 0) == 0) || (line.rfind("tip:", 0) == 0);
  }

  static inline std::string drop_vix_error_tip_lines(const std::string &chunk)
  {
    std::string out;
    out.reserve(chunk.size());

    std::size_t start = 0;
    while (start < chunk.size())
    {
      const std::size_t nl = chunk.find('\n', start);
      const std::size_t end = (nl == std::string::npos) ? chunk.size() : (nl + 1);

      std::string_view line(&chunk[start], end - start);

      if (!is_vix_error_tip_line(line))
        out.append(line.data(), line.size());

      start = end;
    }

    return out;
  }

  static inline std::size_t utf8_safe_prefix_len(const std::string &s, std::size_t want)
  {
    if (want >= s.size())
      return s.size();
    if (want == 0)
      return 0;

    std::size_t cut = want;

    while (cut > 0 && (static_cast<unsigned char>(s[cut - 1]) & 0xC0) == 0x80)
      --cut;

    if (cut == 0)
      return 0;

    const unsigned char lead = static_cast<unsigned char>(s[cut - 1]);
    std::size_t need = 1;

    if ((lead & 0x80) == 0x00)
      need = 1;
    else if ((lead & 0xE0) == 0xC0)
      need = 2;
    else if ((lead & 0xF0) == 0xE0)
      need = 3;
    else if ((lead & 0xF8) == 0xF0)
      need = 4;

    if ((cut - 1) + need > want)
      return cut - 1;

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

      if (std::strcmp(mode, "auto") == 0)
        return ::isatty(STDOUT_FILENO) != 0;

      return false;
    }

    static std::size_t find_first_vix_marker(const std::string &text)
    {
      static const char *priority[] = {
          "● Vix.cpp   READY",
          "Vix.cpp   READY",
          "● VIX.cpp   READY",
          "VIX.cpp   READY",
          "● VIX   READY",
          "VIX   READY",
          nullptr};

      for (int i = 0; priority[i] != nullptr; ++i)
      {
        const std::size_t pos = text.find(priority[i]);
        if (pos != std::string::npos)
          return pos;
      }

      static const char *fallback[] = {
          "› HTTP:",
          "› WS:",
          "i Threads:",
          "i Mode:",
          "i Status:",
          "i Hint:",
          "Using configuration file:",
          "Vix.cpp runtime",
          "Vix.cpp v",
          "● Vix.cpp",
          "● VIX.cpp",
          "● VIX",
          nullptr};

      std::size_t firstPos = std::string::npos;

      for (int i = 0; fallback[i] != nullptr; ++i)
      {
        const std::size_t pos = text.find(fallback[i]);
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
      const std::size_t lastNl = buffer_.rfind('\n');
      if (lastNl == std::string::npos)
      {
        if (buffer_.size() > TAIL_BUFFER_SIZE)
        {
          const std::size_t flushLen = buffer_.size() - TAIL_BUFFER_SIZE;
          const std::size_t safeLen = utf8_safe_prefix_len(buffer_, flushLen);
          std::string out = buffer_.substr(0, safeLen);
          buffer_.erase(0, safeLen);
          return out;
        }
        return {};
      }

      std::string out = buffer_.substr(0, lastNl + 1);
      buffer_.erase(0, lastNl + 1);

      if (buffer_.size() > TAIL_BUFFER_SIZE)
      {
        const std::size_t trimLen = buffer_.size() - TAIL_BUFFER_SIZE;
        const std::size_t safeTrim = utf8_safe_prefix_len(buffer_, trimLen);
        buffer_.erase(0, safeTrim);
      }

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
    const bool isCmake = (cmd.find("cmake") != std::string::npos);
    const bool isBuild = (cmd.find("--build") != std::string::npos);
    const bool isPreset = (cmd.find("--preset") != std::string::npos);
    const bool isDotDot = (cmd.find("cmake ..") != std::string::npos) ||
                          (cmd.find("cmake  ..") != std::string::npos);

    return isCmake && !isBuild && (isPreset || isDotDot);
  }

  static inline bool looks_like_error_or_warning(std::string_view line) noexcept
  {
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

    static const std::string ninjaNoWork = "ninja: no work to do.";
    if (chunk.find(ninjaNoWork) != std::string::npos)
      return true;

    if (!chunk.empty() && chunk[0] == '[')
    {
      const auto rb = chunk.find(']');
      if (rb != std::string::npos)
      {
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
    const char *clearLine = "\r\033[2K\r";
    write_all(STDOUT_FILENO, clearLine, std::strlen(clearLine));

    printedSomething = true;
    lastPrintedChar = '\r';
  }

  static bool is_sanitizer_abort_banner_line(std::string_view line) noexcept
  {
    // Trim left
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
      line.remove_prefix(1);

    // ==12345==ABORTING
    if (line.size() >= 4 && line.rfind("==", 0) == 0 &&
        line.find("==ABORTING") != std::string_view::npos)
      return true;

    return false;
  }

  static inline std::string drop_sanitizer_abort_banner_lines(const std::string &chunk)
  {
    std::string out;
    out.reserve(chunk.size());

    std::size_t start = 0;
    while (start < chunk.size())
    {
      const std::size_t nl = chunk.find('\n', start);
      const std::size_t end = (nl == std::string::npos) ? chunk.size() : (nl + 1);

      std::string_view line(&chunk[start], end - start);

      if (!is_sanitizer_abort_banner_line(line))
        out.append(line.data(), line.size());

      start = end;
    }

    return out;
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

    if (::pipe(outPipe) != 0)
    {
      result.exitCode = std::system(cmd.c_str());
      return result;
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
      close_safe(outPipe[0]);
      close_safe(outPipe[1]);

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

      ::setpgid(0, 0);

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

      ::dup2(outPipe[1], STDOUT_FILENO);
      ::dup2(outPipe[1], STDERR_FILENO);

      ::close(outPipe[1]);

      if (::getenv("VIX_MODE") == nullptr)
        ::setenv("VIX_MODE", "run", 1);

      ::execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
      _exit(127);
    }

    close_safe(outPipe[1]);
    ::setpgid(pid, pid);

    const bool useSpinner = !spinnerLabel.empty();
    const bool captureOnly = (!passthroughRuntime && spinnerLabel.empty());
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

    struct UncaughtExceptionSuppressor
    {
      std::string carry;

      static bool is_noise_line(std::string_view line) noexcept
      {
        auto has = [&](std::string_view s) noexcept
        { return line.find(s) != std::string_view::npos; };

        // libstdc++ / libc++ terminate noise
        if (has("terminate called after throwing an instance of"))
          return true;
        if (has("terminating with uncaught exception"))
          return true;
        if (has("libc++abi: terminating with uncaught exception"))
          return true;
        if (has("std::terminate"))
          return true;

        // the missing line in your output
        // libstdc++ prints: "  what():  Weird!"
        if (has("what():"))
          return true;

        // common endings (optional)
        if (has("Aborted (core dumped)"))
          return true;
        if (has("core dumped"))
          return true;
        if (has("SIGABRT"))
          return true;

        return false;
      }

      static bool is_whitespace_only(std::string_view s) noexcept
      {
        for (char c : s)
        {
          // ignore line endings
          if (c == '\n' || c == '\r')
            continue;

          // any non-space/tab means it's not whitespace-only
          if (c != ' ' && c != '\t')
            return false;
        }
        return true;
      }

      // Remove noise from what we PRINT, but keep it in result.stdoutText capture.
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

          if (is_noise_line(line))
            continue;

          if (is_whitespace_only(line))
            continue;

          out.append(line.data(), line.size());
        }

        return out;
      }
    };

    SanitizerSuppressor sanitizer;
    UncaughtExceptionSuppressor uncaught;

    bool running = true;
    bool printedSomething = false;
    bool printedRealOutput = false;
    char lastPrintedChar = '\n';

    int finalStatus = 0;
    bool haveStatus = false;

    const bool enableTimeout = (timeoutSec > 0);
    const auto startTime = std::chrono::steady_clock::now();
    bool didTimeout = false;

    bool sentInt = false;
    bool sentTerm = false;
    bool sentKill = false;

    auto intTime = startTime;
    auto termTime = startTime;

    auto int_elapsed_ms = [&]() -> long long
    {
      using namespace std::chrono;
      return duration_cast<milliseconds>(steady_clock::now() - intTime).count();
    };

    auto term_elapsed_ms = [&]() -> long long
    {
      using namespace std::chrono;
      return duration_cast<milliseconds>(steady_clock::now() - termTime).count();
    };

    auto elapsed_sec = [&]() -> long long
    {
      using namespace std::chrono;
      return duration_cast<seconds>(steady_clock::now() - startTime).count();
    };

    auto icontains_sv = [](std::string_view hay, std::string_view needle) -> bool
    {
      if (needle.empty())
        return true;
      if (hay.size() < needle.size())
        return false;

      for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
      {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j)
        {
          unsigned char a = static_cast<unsigned char>(hay[i + j]);
          unsigned char b = static_cast<unsigned char>(needle[j]);
          a = static_cast<unsigned char>(std::tolower(a));
          b = static_cast<unsigned char>(std::tolower(b));
          if (a != b)
          {
            ok = false;
            break;
          }
        }
        if (ok)
          return true;
      }
      return false;
    };

    bool suppress_known_failure_output = false;
    bool userInterrupted = false;

    auto is_known_runtime_port_in_use = [&](std::string_view s) -> bool
    {
      return icontains_sv(s, "address already in use") ||
             icontains_sv(s, "eaddrinuse") ||
             (icontains_sv(s, "bind") && icontains_sv(s, "acceptor") && icontains_sv(s, "address already in use"));
    };

    auto kill_group_or_pid = [&](int sig)
    {
      if (::kill(-pid, sig) != 0)
      {
        ::kill(pid, sig);
      }
    };

    while (running)
    {
      if (!sentInt && g_sigint_requested)
      {
        userInterrupted = true;
        kill_group_or_pid(SIGINT);
        sentInt = true;
        intTime = std::chrono::steady_clock::now();
      }

      if (sentInt && !sentTerm)
      {
        if (int_elapsed_ms() >= 300)
        {
          ::kill(-pid, SIGTERM);
          sentTerm = true;
          termTime = std::chrono::steady_clock::now();
        }
      }

      if ((sentTerm || sentInt) && !sentKill)
      {
        const long long ms = sentTerm ? term_elapsed_ms() : int_elapsed_ms();
        if (ms >= 1200)
        {
          ::kill(-pid, SIGKILL);
          sentKill = true;
        }
      }

      if (!didTimeout && enableTimeout && elapsed_sec() >= timeoutSec)
      {
        didTimeout = true;

        const std::string msg =
            "\n[vix] runtime timeout (" + std::to_string(timeoutSec) + "s)\n";
        result.stderrText += msg;

        ::kill(-pid, SIGTERM);
        sentTerm = true;
        termTime = std::chrono::steady_clock::now();
      }

      fd_set fds;
      FD_ZERO(&fds);

      int maxfd = -1;
      if (outPipe[0] >= 0)
      {
        FD_SET(outPipe[0], &fds);
        maxfd = outPipe[0];
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
        if (spinnerActive && !captureOnly)
          spinner_draw(spinnerLabel, frameIndex, printedSomething, lastPrintedChar);
      }
      else
      {
        if (spinnerActive)
        {
          spinner_clear(printedSomething, lastPrintedChar);
          spinnerActive = false;
        }

        if (outPipe[0] >= 0 && FD_ISSET(outPipe[0], &fds))
        {
          std::string chunk;
          if (read_into(outPipe[0], chunk))
          {
            result.stdoutText += chunk;

            if (!suppress_known_failure_output && is_known_runtime_port_in_use(chunk))
              suppress_known_failure_output = true;

            if (suppress_known_failure_output)
              continue;

            if (!should_drop_chunk_default(chunk))
            {
              std::string printable = chunk;

              if (cmakeConfigure)
                printable = cmakeNoise.filter(printable);

              if (!printable.empty())
                printable = sanitizer.filter_for_print(printable);

              if (!printable.empty())
                printable = uncaught.filter_for_print(printable);

              if (!printable.empty())
              {
                std::string filtered =
                    passthroughRuntime ? printable : runtimeFilter.process(printable);

                if (!filtered.empty())
                {
                  std::string toPrint = drop_vix_error_tip_lines(filtered);
                  if (!toPrint.empty())
                    toPrint = drop_sanitizer_abort_banner_lines(toPrint);

                  if (!toPrint.empty() && !captureOnly)
                  {
                    write_all(STDOUT_FILENO, toPrint.data(), toPrint.size());
                    printedSomething = true;
                    printedRealOutput = true;
                    result.printed_live = true;
                    lastPrintedChar = toPrint.back();
                  }
                }
              }
            }
          }
          else
          {
            close_safe(outPipe[0]);
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

    if (spinnerActive && !captureOnly)
      spinner_clear(printedSomething, lastPrintedChar);

    close_safe(outPipe[0]);

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

    if (!captureOnly &&
        printedRealOutput &&
        lastPrintedChar != '\n' &&
        ::isatty(STDOUT_FILENO) != 0)
    {
      const char nl = '\n';
      write_all(STDOUT_FILENO, &nl, 1);
    }

    if (userInterrupted)
    {
      result.exitCode = 130;
    }

    return result;
  }

#endif

  int run_cmd_live_filtered(const std::string &cmd,
                            const std::string &spinnerLabel)
  {
#ifdef _WIN32
    (void)spinnerLabel;
    return std::system(cmd.c_str());
#else
    LiveRunResult r = run_cmd_live_filtered_capture(cmd, spinnerLabel, false, /*timeoutSec=*/0);
    return r.exitCode;
#endif
  }

} // namespace vix::commands::RunCommand::detail
