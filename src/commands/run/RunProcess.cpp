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
#include <vix/cli/commands/replay/ReplayCapture.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <iomanip>
#include <sstream>

#ifndef _WIN32
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef _WIN32
#include <termios.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
#ifdef _WIN32

  namespace
  {
    std::string win_last_error_message(DWORD err)
    {
      LPWSTR buf = nullptr;
      const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                          FORMAT_MESSAGE_FROM_SYSTEM |
                          FORMAT_MESSAGE_IGNORE_INSERTS;

      const DWORD len = FormatMessageW(
          flags,
          nullptr,
          err,
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPWSTR)&buf,
          0,
          nullptr);

      std::string out;
      if (len && buf)
      {
        const int n = WideCharToMultiByte(CP_UTF8, 0, buf, (int)len, nullptr, 0, nullptr, nullptr);
        out.resize(n > 0 ? (size_t)n : 0);
        if (n > 0)
          WideCharToMultiByte(CP_UTF8, 0, buf, (int)len, out.data(), n, nullptr, nullptr);

        LocalFree(buf);
      }

      if (out.empty())
        out = "unknown Windows error";

      return out;
    }
  } // namespace

  LiveRunResult run_cmd_live_filtered_capture(
      const std::string &cmd,
      const std::string &spinnerLabel,
      bool passthroughRuntime,
      int timeoutSec,
      bool useSan,
      bool captureOnly,
      vix::commands::replay::ReplayCapture *replayCapture)
  {
    (void)spinnerLabel;
    (void)passthroughRuntime;
    (void)timeoutSec;
    (void)useSan;

    LiveRunResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;

    if (!CreatePipe(&outRead, &outWrite, &sa, 0))
    {
      result.exitCode = 1;
      result.stderrText = "CreatePipe failed: " + win_last_error_message(GetLastError());
      return result;
    }

    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::string cmdline = "cmd /C " + cmd;

    if (!CreateProcessA(
            nullptr,
            cmdline.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi))
    {
      CloseHandle(outRead);
      CloseHandle(outWrite);

      result.exitCode = 1;
      result.stderrText = "CreateProcess failed: " + win_last_error_message(GetLastError());
      return result;
    }

    CloseHandle(outWrite);

    char buffer[4096];
    DWORD n = 0;
    while (true)
    {
      const BOOL ok = ReadFile(outRead, buffer, (DWORD)sizeof(buffer), &n, nullptr);
      if (!ok || n == 0)
        break;

      result.stdoutText.append(buffer, buffer + n);

      if (replayCapture && n > 0)
        replayCapture->capture_stdout_noexcept(std::string(buffer, buffer + n));
    }

    CloseHandle(outRead);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    result.rawStatus = (int)code;
    result.exitCode = normalize_exit_code((int)code);
    return result;
  }

#else

  namespace
  {
    static volatile sig_atomic_t g_signal_requested = 0;
    static volatile sig_atomic_t g_requested_signal = 0;

    void on_runtime_signal(int sig)
    {
      g_signal_requested = 1;
      g_requested_signal = sig;
    }

    bool is_runtime_crash_noise_line(std::string_view line) noexcept
    {
      while (!line.empty() &&
             (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
      {
        line.remove_prefix(1);
      }

      return line.find("free(): double free detected") != std::string_view::npos ||
             line.find("double free detected in tcache") != std::string_view::npos ||
             line.find("malloc(): corrupted") != std::string_view::npos ||
             line.find("munmap_chunk(): invalid pointer") != std::string_view::npos ||
             line.find("free(): invalid pointer") != std::string_view::npos ||
             line.find("Aborted (core dumped)") != std::string_view::npos;
    }

    std::string drop_runtime_crash_noise_lines(const std::string &chunk)
    {
      std::string out;
      out.reserve(chunk.size());

      std::size_t start = 0;
      while (start < chunk.size())
      {
        const std::size_t nl = chunk.find('\n', start);
        const std::size_t end = (nl == std::string::npos) ? chunk.size() : (nl + 1);

        std::string_view line(&chunk[start], end - start);

        if (!is_runtime_crash_noise_line(line))
          out.append(line.data(), line.size());

        start = end;
      }

      return out;
    }

    struct RuntimeSignalGuard
    {
      struct sigaction oldInt{};
      struct sigaction oldTerm{};
      struct sigaction oldHup{};

      bool installedInt = false;
      bool installedTerm = false;
      bool installedHup = false;

      RuntimeSignalGuard()
      {
        g_signal_requested = 0;
        g_requested_signal = 0;

        struct sigaction sa{};
        sa.sa_handler = on_runtime_signal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        if (sigaction(SIGINT, &sa, &oldInt) == 0)
        {
          installedInt = true;
        }

        if (sigaction(SIGTERM, &sa, &oldTerm) == 0)
        {
          installedTerm = true;
        }

        if (sigaction(SIGHUP, &sa, &oldHup) == 0)
        {
          installedHup = true;
        }
      }

      ~RuntimeSignalGuard()
      {
        if (installedInt)
        {
          sigaction(SIGINT, &oldInt, nullptr);
        }

        if (installedTerm)
        {
          sigaction(SIGTERM, &oldTerm, nullptr);
        }

        if (installedHup)
        {
          sigaction(SIGHUP, &oldHup, nullptr);
        }
      }
    };

    void write_all(int fd, const char *buf, std::size_t n)
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

    std::size_t terminal_width() noexcept
    {
      struct winsize ws{};

      if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return static_cast<std::size_t>(ws.ws_col);

      return 120;
    }

    std::string format_seconds(long long milliseconds)
    {
      const double seconds = static_cast<double>(milliseconds) / 1000.0;

      std::ostringstream out;
      out.setf(std::ios::fixed);
      out.precision(seconds >= 10.0 ? 1 : 2);
      out << seconds << "s";

      return out.str();
    }

    std::string truncate_terminal_line(const std::string &line, std::size_t width)
    {
      if (width == 0)
        return "";

      if (line.size() <= width)
        return line;

      if (width <= 3)
        return std::string(width, '.');

      return line.substr(0, width - 3) + "...";
    }

    std::size_t utf8_safe_prefix_len(const std::string &s, std::size_t want)
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

    void close_safe(int &fd)
    {
      if (fd >= 0)
      {
        ::close(fd);
        fd = -1;
      }
    }

    bool is_vix_error_tip_line(std::string_view line) noexcept
    {
      while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);

      return (line.rfind("error:", 0) == 0) || (line.rfind("tip:", 0) == 0);
    }

    std::string drop_vix_error_tip_lines(const std::string &chunk)
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

    bool is_sanitizer_abort_banner_line(std::string_view line) noexcept
    {
      while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
        line.remove_prefix(1);

      if (line.size() >= 4 &&
          line.rfind("==", 0) == 0 &&
          line.find("==ABORTING") != std::string_view::npos)
      {
        return true;
      }

      return false;
    }

    std::string drop_sanitizer_abort_banner_lines(const std::string &chunk)
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

    bool is_cmake_configure_cmd(const std::string &cmd) noexcept
    {
      const bool hasCmake =
          cmd.find("cmake") != std::string::npos;

      if (!hasCmake)
        return false;

      if (cmd.find("--build") != std::string::npos)
        return false;

      if (cmd.find("--install") != std::string::npos)
        return false;

      if (cmd.find("--preset") != std::string::npos)
        return true;

      if (cmd.find("cmake ..") != std::string::npos ||
          cmd.find("cmake  ..") != std::string::npos)
        return true;

      const bool hasSource =
          cmd.find(" -S ") != std::string::npos ||
          cmd.find(" -S.") != std::string::npos ||
          cmd.find(" -S\"") != std::string::npos ||
          cmd.find(" -S'") != std::string::npos;

      const bool hasBuild =
          cmd.find(" -B ") != std::string::npos ||
          cmd.find(" -B\"") != std::string::npos ||
          cmd.find(" -B'") != std::string::npos;

      return hasSource && hasBuild;
    }

    bool looks_like_error_or_warning(std::string_view line) noexcept
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

    bool should_drop_chunk_default(const std::string &chunk)
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

    void spinner_draw(
        const std::string &label,
        std::size_t &frameIndex,
        bool &printedSomething,
        char &lastPrintedChar)
    {
      (void)label;
      (void)frameIndex;
      (void)printedSomething;
      (void)lastPrintedChar;
    }

    void spinner_clear(bool &printedSomething, char &lastPrintedChar)
    {
      (void)printedSomething;
      (void)lastPrintedChar;
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
          const char *env = vix::utils::vix_getenv("VIX_DEBUG_FILTER");
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
        const char *mode = vix::utils::vix_getenv("VIX_CLI_CLEAR");
        if (!mode)
          return false;

        return std::strcmp(mode, "1") == 0;
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

    std::string rtrim_copy(std::string s)
    {
      while (!s.empty() &&
             (s.back() == ' ' || s.back() == '\t' ||
              s.back() == '\n' || s.back() == '\r'))
      {
        s.pop_back();
      }
      return s;
    }

    bool looks_like_prompt_fragment(const std::string &chunk)
    {
      if (chunk.empty())
        return false;

      if (chunk.find('\n') != std::string::npos ||
          chunk.find('\r') != std::string::npos)
      {
        return false;
      }

      const std::string trimmed = rtrim_copy(chunk);

      if (trimmed.empty())
        return false;

      if (trimmed == ">" ||
          trimmed == "$" ||
          trimmed == "#" ||
          trimmed == ">>>" ||
          trimmed == "...")
      {
        return true;
      }

      if (trimmed.size() <= 160)
      {
        const char last = trimmed.back();

        if (last == ':' ||
            last == '?' ||
            last == '>' ||
            last == '$' ||
            last == '#')
        {
          return true;
        }
      }

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

          /*
           * During CMake configure, Vix must never dump normal CMake traces.
           * Only real errors and warnings are allowed to reach the terminal.
           */
          if (looks_like_error_or_warning(line))
            out.append(line.data(), line.size());
        }

        return out;
      }
    };

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

            if (!inReport && looks_like_prompt_fragment(carry))
            {
              out += carry;
              carry.clear();
            }

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

        if (has("terminate called after throwing an instance of"))
          return true;
        if (has("terminate called without an active exception"))
          return true;
        if (has("terminating with uncaught exception"))
          return true;
        if (has("libc++abi: terminating with uncaught exception"))
          return true;
        if (has("std::terminate"))
          return true;
        if (has("what():"))
          return true;
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
          if (c == '\n' || c == '\r')
            continue;

          if (c != ' ' && c != '\t')
            return false;
        }

        return true;
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

            if (looks_like_prompt_fragment(carry))
            {
              out += carry;
              carry.clear();
            }

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

    struct PtySession
    {
      int masterFd = -1;
      int slaveFd = -1;

      bool open()
      {
        return ::openpty(&masterFd, &slaveFd, nullptr, nullptr, nullptr) == 0;
      }

      ~PtySession()
      {
        close_safe(masterFd);
        close_safe(slaveFd);
      }
    };

    bool read_from_pty(int fd, std::string &outChunk)
    {
      char buf[4096];
      const ssize_t n = ::read(fd, buf, sizeof(buf));
      if (n > 0)
      {
        outChunk.assign(buf, static_cast<std::size_t>(n));
        return true;
      }

      outChunk.clear();

      if (n == 0)
        return false;
      if (n < 0 && errno == EINTR)
        return true;
      if (n < 0 && errno == EIO)
        return false;

      return false;
    }

    bool icontains_sv(std::string_view hay, std::string_view needle)
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
    }

    bool is_known_runtime_port_in_use(std::string_view s)
    {
      return icontains_sv(s, "address already in use") ||
             icontains_sv(s, "eaddrinuse") ||
             (icontains_sv(s, "bind") &&
              icontains_sv(s, "acceptor") &&
              icontains_sv(s, "address already in use"));
    }

    void kill_group_or_pid(pid_t pid, int sig)
    {
      if (::kill(-pid, sig) != 0)
        ::kill(pid, sig);
    }

    void child_exec_shell(const std::string &cmd, int masterFd, int slaveFd, bool useSan, bool inheritParentStdin)
    {
      struct sigaction saChild{};
      saChild.sa_handler = SIG_DFL;
      sigemptyset(&saChild.sa_mask);
      saChild.sa_flags = 0;
      ::sigaction(SIGINT, &saChild, nullptr);

      ::setsid();

      if (::isatty(slaveFd))
      {
        struct termios tty{};
        if (::tcgetattr(slaveFd, &tty) == 0)
        {
          tty.c_lflag &= static_cast<tcflag_t>(~ECHO);
          ::tcsetattr(slaveFd, TCSANOW, &tty);
        }
      }

      if (useSan)
      {
        ::setenv("ASAN_OPTIONS",
                 "abort_on_error=1:"
                 "halt_on_error=1:"
                 "print_stacktrace=1:"
                 "detect_leaks=1:"
                 "symbolize=1:"
                 "fast_unwind_on_malloc=0:"
                 "strict_init_order=1:"
                 "check_initialization_order=1:"
                 "color=never",
                 1);

        ::setenv("UBSAN_OPTIONS",
                 "halt_on_error=1:print_stacktrace=1:color=never",
                 1);
      }

      ::close(masterFd);

      if (!inheritParentStdin)
        ::dup2(slaveFd, STDIN_FILENO);

      ::dup2(slaveFd, STDOUT_FILENO);
      ::dup2(slaveFd, STDERR_FILENO);

      if (slaveFd > STDERR_FILENO)
        ::close(slaveFd);

      ::setvbuf(stdout, nullptr, _IOLBF, 0);
      ::setvbuf(stderr, nullptr, _IONBF, 0);

      if (::getenv("VIX_MODE") == nullptr)
        ::setenv("VIX_MODE", "run", 1);

#ifdef RLIMIT_CORE
      struct rlimit rl;
      rl.rlim_cur = 0;
      rl.rlim_max = 0;
      ::setrlimit(RLIMIT_CORE, &rl);
#endif

      ::execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char *>(nullptr));
      _exit(127);
    }

    void process_printable_chunk(
        const std::string &chunk,
        bool cmakeConfigure,
        bool passthroughRuntime,
        bool captureOnly,
        bool useSan,
        RuntimeOutputFilter &runtimeFilter,
        CMakeNoiseFilter &cmakeNoise,
        SanitizerSuppressor &sanitizer,
        UncaughtExceptionSuppressor &uncaught,
        LiveRunResult &result,
        bool &printedSomething,
        bool &printedRealOutput,
        char &lastPrintedChar)
    {
      if (chunk.empty())
        return;

      if (should_drop_chunk_default(chunk))
        return;

      std::string printable = chunk;

      if (cmakeConfigure)
      {
        printable = cmakeNoise.filter(printable);

        if (printable.empty())
          return;

        if (captureOnly || useSan)
          return;

        write_all(STDOUT_FILENO, printable.data(), printable.size());
        printedSomething = true;
        printedRealOutput = true;
        result.printed_live = true;
        lastPrintedChar = printable.back();
        return;
      }

      if (printable.empty())
        return;

      if (useSan)
        return;

      std::string filtered;

      printable = sanitizer.filter_for_print(printable);

      if (!printable.empty())
        printable = uncaught.filter_for_print(printable);

      if (printable.empty())
        return;

      filtered = passthroughRuntime ? printable : runtimeFilter.process(printable);

      if (filtered.empty())
        return;

      std::string toPrint = drop_vix_error_tip_lines(filtered);

      if (!toPrint.empty())
        toPrint = drop_sanitizer_abort_banner_lines(toPrint);

      if (!toPrint.empty())
        toPrint = drop_runtime_crash_noise_lines(toPrint);

      if (toPrint.empty() || captureOnly)
        return;

      write_all(STDOUT_FILENO, toPrint.data(), toPrint.size());
      printedSomething = true;
      printedRealOutput = true;
      result.printed_live = true;
      lastPrintedChar = toPrint.back();
    }

  } // namespace

  LiveRunResult run_cmd_live_filtered_capture(
      const std::string &cmd,
      const std::string &spinnerLabel,
      bool passthroughRuntime,
      int timeoutSec,
      bool useSan,
      bool captureOnly,
      vix::commands::replay::ReplayCapture *replayCapture)
  {
    RuntimeSignalGuard signalGuard;
    LiveRunResult result;

    PtySession pty;
    if (!pty.open())
    {
      const int st = std::system(cmd.c_str());
      result.rawStatus = st;
      result.exitCode = normalize_exit_code(st);
      return result;
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
      const int st = std::system(cmd.c_str());
      result.rawStatus = st;
      result.exitCode = normalize_exit_code(st);
      return result;
    }

    const bool stdinIsTty = (::isatty(STDIN_FILENO) != 0);
    const bool inheritParentStdin = passthroughRuntime && !stdinIsTty;
    bool forwardStdin = passthroughRuntime && stdinIsTty;

    if (pid == 0)
      child_exec_shell(
          cmd,
          pty.masterFd,
          pty.slaveFd,
          useSan,
          inheritParentStdin);

    close_safe(pty.slaveFd);
    ::setpgid(pid, pid);

    bool spinnerActive = false;
    std::size_t frameIndex = 0;

    RuntimeOutputFilter runtimeFilter;
    const bool cmakeConfigure = is_cmake_configure_cmd(cmd);
    CMakeNoiseFilter cmakeNoise;
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

    auto lastOutputTime = startTime;
    auto lastHeartbeatTime = startTime;
    bool heartbeatVisible = false;

    auto heartbeat_env_enabled = [&]() -> bool
    {
      if (captureOnly)
        return false;

      if (passthroughRuntime)
        return false;

      if (!cmakeConfigure)
        return false;

      const char *runValue = vix::utils::vix_getenv("VIX_RUN_HEARTBEAT");
      const char *buildValue = vix::utils::vix_getenv("VIX_BUILD_HEARTBEAT");

      const char *value = runValue && *runValue ? runValue : buildValue;

      if (!value || !*value)
        return true;

      std::string s(value);
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      if (s == "0" || s == "false" || s == "no" || s == "off")
        return false;

      return true;
    }();

    auto clear_heartbeat_line = [&]() -> void
    {
      if (!heartbeatVisible)
        return;

      const std::size_t width = terminal_width();

      std::string clear;
      clear += "\r";
      clear.append(width, ' ');
      clear += "\r";

      write_all(STDOUT_FILENO, clear.data(), clear.size());
      heartbeatVisible = false;
    };

    auto render_heartbeat_line = [&](long long elapsedMs) -> void
    {
      const std::size_t width = terminal_width();

      std::string line;
      line += "  ";
      line += CYAN;
      line += "configure";
      line += RESET;
      line += " still running... ";
      line += GRAY;
      line += "(" + format_seconds(elapsedMs) + ", checking/downloading dependencies)";
      line += RESET;

      std::string out;
      out += "\r";
      out.append(width, ' ');
      out += "\r";
      out += truncate_terminal_line(line, width);

      write_all(STDOUT_FILENO, out.data(), out.size());
      heartbeatVisible = true;
    };

    auto finish_heartbeat_line = [&]() -> void
    {
      if (!heartbeatVisible)
        return;

      clear_heartbeat_line();
    };

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

    bool suppress_known_failure_output = false;
    bool userInterrupted = false;

    while (running)
    {
      if (!sentInt && g_signal_requested)
      {
        const int requestedSignal =
            g_requested_signal > 0 ? static_cast<int>(g_requested_signal) : SIGTERM;

        userInterrupted = requestedSignal == SIGINT;

        kill_group_or_pid(pid, requestedSignal);

        sentInt = true;
        intTime = std::chrono::steady_clock::now();
      }

      if (sentInt && !sentTerm && int_elapsed_ms() >= 300)
      {
        ::kill(-pid, SIGTERM);
        sentTerm = true;
        termTime = std::chrono::steady_clock::now();
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
        result.stderrText += "\n[vix] runtime timeout (" + std::to_string(timeoutSec) + "s)\n";

        ::kill(-pid, SIGTERM);
        sentTerm = true;
        termTime = std::chrono::steady_clock::now();
      }

      fd_set fds;
      FD_ZERO(&fds);

      int maxfd = -1;

      if (pty.masterFd >= 0)
      {
        FD_SET(pty.masterFd, &fds);
        maxfd = pty.masterFd;
      }

      if (forwardStdin)
      {
        FD_SET(STDIN_FILENO, &fds);
        if (STDIN_FILENO > maxfd)
          maxfd = STDIN_FILENO;
      }

      if (maxfd < 0)
      {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, 0);
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

      if (spinnerActive || enableTimeout || heartbeat_env_enabled)
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

        if (forwardStdin && FD_ISSET(STDIN_FILENO, &fds) && pty.masterFd >= 0)
        {
          char inbuf[4096];
          const ssize_t n = ::read(STDIN_FILENO, inbuf, sizeof(inbuf));

          if (n > 0)
          {
            write_all(pty.masterFd, inbuf, static_cast<std::size_t>(n));
          }
          else if (n == 0)
          {
            forwardStdin = false;
          }
        }

        if (pty.masterFd >= 0 && FD_ISSET(pty.masterFd, &fds))
        {
          std::string chunk;
          if (read_from_pty(pty.masterFd, chunk))
          {
            if (!chunk.empty())
            {
              const bool outputMayBePrinted =
                  !cmakeConfigure || looks_like_error_or_warning(std::string_view(chunk));

              if (outputMayBePrinted)
              {
                lastOutputTime = std::chrono::steady_clock::now();

                if (heartbeatVisible && !captureOnly)
                  clear_heartbeat_line();
              }

              result.stdoutText += chunk;

              if (replayCapture)
                replayCapture->capture_stdout_noexcept(chunk);

              if (!suppress_known_failure_output && is_known_runtime_port_in_use(chunk))
                suppress_known_failure_output = true;

              if (!suppress_known_failure_output)
              {
                process_printable_chunk(
                    chunk,
                    cmakeConfigure,
                    passthroughRuntime,
                    captureOnly,
                    useSan,
                    runtimeFilter,
                    cmakeNoise,
                    sanitizer,
                    uncaught,
                    result,
                    printedSomething,
                    printedRealOutput,
                    lastPrintedChar);
              }
            }
          }
          else
          {
            close_safe(pty.masterFd);
          }
        }
      }

      if (heartbeat_env_enabled)
      {
        const auto now = std::chrono::steady_clock::now();

        const auto silenceMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastOutputTime)
                .count();

        const auto heartbeatMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastHeartbeatTime)
                .count();

        if (silenceMs >= 2000 && heartbeatMs >= 1000)
        {
          lastHeartbeatTime = now;

          const auto elapsedMs =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - startTime)
                  .count();

          render_heartbeat_line(elapsedMs);
        }
      }

      int status = 0;
      const pid_t r = ::waitpid(pid, &status, WNOHANG);
      if (r == pid)
      {
        finalStatus = status;
        haveStatus = true;
        running = false;
      }
    }

    if (spinnerActive && !captureOnly)
      spinner_clear(printedSomething, lastPrintedChar);

    close_safe(pty.masterFd);

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
      if (!captureOnly)
        finish_heartbeat_line();

      result.exitCode = 124;
      return result;
    }

    if (haveStatus && WIFSIGNALED(finalStatus))
    {
      const int sig = WTERMSIG(finalStatus);
      result.terminatedBySignal = true;
      result.termSignal = sig;
      result.exitCode = 128 + sig;
    }
    else
    {
      result.exitCode = haveStatus ? normalize_exit_code(finalStatus) : 1;
    }

    if (!captureOnly &&
        !passthroughRuntime &&
        printedRealOutput &&
        lastPrintedChar != '\n' &&
        ::isatty(STDOUT_FILENO) != 0)
    {
      const char nl = '\n';
      write_all(STDOUT_FILENO, &nl, 1);
    }

    if (!captureOnly)
      finish_heartbeat_line();

    if (userInterrupted)
      result.exitCode = 130;

    return result;
  }
#endif

  int run_cmd_live_filtered(
      const std::string &cmd,
      const std::string &spinnerLabel)
  {
#ifdef _WIN32
    (void)spinnerLabel;
    return std::system(cmd.c_str());
#else
    LiveRunResult r = run_cmd_live_filtered_capture(
        cmd,
        spinnerLabel,
        false,
        0,
        false,
        false,
        nullptr);
    return r.exitCode;
#endif
  }

} // namespace vix::commands::RunCommand::detail
