/**
 *
 *  @file RawLogDetectors.cpp
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
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/errors/CodeFrame.hpp>
#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/rules/UncaughtExceptionRule.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
  namespace
  {
    static inline char toLowerAscii(unsigned char c) noexcept
    {
      return static_cast<char>(std::tolower(c));
    }

    static bool icontains(std::string_view haystack, std::string_view needle) noexcept
    {
      if (needle.empty())
        return true;
      if (haystack.size() < needle.size())
        return false;

      for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
      {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j)
        {
          const unsigned char a = static_cast<unsigned char>(haystack[i + j]);
          const unsigned char b = static_cast<unsigned char>(needle[j]);
          if (toLowerAscii(a) != toLowerAscii(b))
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

    static bool log_looks_sanitized(const std::string &log) noexcept
    {
      return icontains(log, "AddressSanitizer") ||
             icontains(log, "UndefinedBehaviorSanitizer") ||
             icontains(log, "LeakSanitizer") ||
             icontains(log, "ThreadSanitizer") ||
             icontains(log, "MemorySanitizer") ||
             icontains(log, "==") && icontains(log, "==ABORTING");
    }

    static std::string maybe_add_san_hint(std::string hint, const std::string &log)
    {
      if (log_looks_sanitized(log))
        return hint;

      hint += ". run with --san for exact location";
      return hint;
    }

    static std::string escape_regex(std::string s)
    {
      // escape regex metacharacters: . ^ $ | ( ) [ ] { } * + ? \ -
      std::string out;
      out.reserve(s.size() + 16);
      for (const char c : s)
      {
        switch (c)
        {
        case '.':
        case '^':
        case '$':
        case '|':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '*':
        case '+':
        case '?':
        case '\\':
        case '-':
          out.push_back('\\');
          out.push_back(c);
          break;
        default:
          out.push_back(c);
          break;
        }
      }
      return out;
    }

    static std::vector<std::string> splitLines(const std::string &text)
    {
      std::vector<std::string> lines;
      std::istringstream iss(text);
      std::string line;
      while (std::getline(iss, line))
        lines.push_back(line);
      return lines;
    }

    static std::string_view trim_view(std::string_view s) noexcept
    {
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0)
        s.remove_prefix(1);
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0)
        s.remove_suffix(1);
      return s;
    }

    static bool startsWith(std::string_view s, std::string_view p) noexcept
    {
      return s.size() >= p.size() && s.substr(0, p.size()) == p;
    }

    static bool is_system_path(std::string_view p) noexcept
    {
      return p.find("/usr/include/") != std::string_view::npos ||
             p.find("/usr/lib/") != std::string_view::npos ||
             p.find("/usr/local/include/") != std::string_view::npos ||
             p.find("/usr/local/lib/") != std::string_view::npos ||
             p.find("/lib/") != std::string_view::npos ||
             p.find("/lib64/") != std::string_view::npos ||
             p.find("libsanitizer") != std::string_view::npos;
    }

    static std::vector<std::string> excerptWindow(
        const std::vector<std::string> &lines,
        std::size_t center,
        std::size_t before,
        std::size_t after,
        std::size_t maxLines)
    {
      std::vector<std::string> out;
      if (lines.empty())
        return out;

      const std::size_t start = (center > before) ? (center - before) : 0;
      const std::size_t end = std::min(lines.size(), center + after + 1);

      for (std::size_t i = start; i < end && out.size() < maxLines; ++i)
        out.push_back(lines[i]);

      return out;
    }

    static void print_excerpt(const std::string &log, std::size_t maxLines = 16)
    {
      const auto lines = splitLines(log);

      // Find first interesting line to anchor.
      std::size_t firstHit = lines.size();
      for (std::size_t i = 0; i < lines.size(); ++i)
      {
        const std::string_view t = trim_view(std::string_view(lines[i]));
        const bool interesting =
            icontains(lines[i], "ERROR:") ||
            icontains(lines[i], "SUMMARY:") ||
            icontains(lines[i], "runtime error:") ||
            icontains(lines[i], "AddressSanitizer") ||
            icontains(lines[i], "UndefinedBehaviorSanitizer") ||
            icontains(lines[i], "LeakSanitizer") ||
            icontains(lines[i], "ThreadSanitizer") ||
            startsWith(t, "#") ||
            startsWith(t, "==") ||
            icontains(lines[i], "Segmentation fault") ||
            icontains(lines[i], "SIGSEGV") ||
            icontains(lines[i], "SIGABRT") ||
            icontains(lines[i], "Aborted") ||
            icontains(lines[i], "assert") ||
            icontains(lines[i], "terminate") ||
            icontains(lines[i], "what():");

        if (interesting)
        {
          firstHit = i;
          break;
        }
      }

      auto win = (firstHit != lines.size())
                     ? excerptWindow(lines, firstHit, 2, 12, maxLines)
                     : excerptWindow(lines, 0, 0, maxLines - 1, maxLines);

      if (win.empty())
        return;

      std::cerr << "\n";
      std::cerr << RED << "log:" << RESET << "\n";
      for (const auto &l : win)
      {
        // Drop ASan abort banner (==12345==ABORTING)
        if (startsWith(trim_view(l), "==") &&
            icontains(l, "ABORTING"))
          continue;

        std::cerr << "  " << l << "\n";
      }
      std::cerr << "\n";
    }

    // Location extraction
    static std::optional<vix::cli::errors::CompilerError>
    tryExtractFirstUserFrame(const std::string &log,
                             const std::filesystem::path &fallbackSource)
    {
      using vix::cli::errors::CompilerError;

      auto make = [](const std::string &file, int line, int col)
      {
        CompilerError e;
        e.file = file;
        e.line = line;
        e.column = (col > 0) ? col : 1;
        return e;
      };

      std::string abs;
      if (!fallbackSource.empty())
      {
        std::error_code ec;
        abs = std::filesystem::weakly_canonical(fallbackSource, ec).string();
        if (ec)
          abs = fallbackSource.string();
      }

      // 0) BEST+ : UBSan often emits "file:line:col: runtime error: ..."
      {
        static const std::regex re(R"((/[^:\n]+?\.(?:c|cc|cpp|cxx|h|hpp|hh)):(\d+):(\d+):\s*runtime error:)",
                                   std::regex::ECMAScript);
        std::smatch m;
        if (std::regex_search(log, m, re))
        {
          const std::string file = m[1].str();
          const int line = std::stoi(m[2].str());
          const int col = std::stoi(m[3].str());
          if (!is_system_path(file))
            return make(file, line, col);
        }
      }

      // 1) Exact script absolute path appears in stack
      if (!abs.empty())
      {
        const std::regex re(escape_regex(abs) + R"(:([0-9]+)(?::([0-9]+))?)");
        std::smatch m;
        if (std::regex_search(log, m, re))
        {
          const int line = std::stoi(m[1].str());
          const int col = (m.size() >= 3 && m[2].matched) ? std::stoi(m[2].str()) : 1;
          return make(abs, line, col);
        }
      }

      // 2) Fallback: scan stack frames and pick first non-system file
      {
        static const std::regex re(R"((/[^:\n]+?\.(?:c|cc|cpp|cxx|h|hpp|hh)):(\d+)(?::(\d+))?)");
        std::smatch m;
        std::string::const_iterator it = log.begin();

        while (std::regex_search(it, log.cend(), m, re))
        {
          const std::string file = m[1].str();
          const int line = std::stoi(m[2].str());
          const int col = (m.size() >= 4 && m[3].matched) ? std::stoi(m[3].str()) : 1;

          if (!is_system_path(file))
            return make(file, line, col);

          it = m.suffix().first;
        }
      }

      return std::nullopt;
    }

    // Unified printing (hint/at must be at bottom)
    static void print_header(std::string_view title)
    {
      std::cerr << RED << title << RESET << "\n";
    }

    static void print_hint_at_bottom(std::string_view hint, std::string_view at)
    {
      if (!hint.empty())
        std::cerr << YELLOW << "hint: " << RESET << hint << "\n";
      if (!at.empty())
        std::cerr << GREEN << "at: " << RESET << at << "\n";
    }

    static void print_codeframe_then_bottom(
        const vix::cli::errors::CompilerError &loc,
        const ErrorContext &ctx,
        const CodeFrameOptions &opt,
        std::string_view hint)
    {
      printCodeFrame(loc, ctx, opt);

      std::string at = loc.file + ":" + std::to_string(loc.line);
      print_hint_at_bottom(hint, at);
    }

    static void print_codeframe_then_bottom_default(
        const vix::cli::errors::CompilerError &loc,
        std::string_view hint)
    {
      ErrorContext ctx;
      CodeFrameOptions opt;
      opt.contextLines = 2;
      opt.maxLineWidth = 120;
      opt.tabWidth = 4;

      print_codeframe_then_bottom(loc, ctx, opt, hint);
    }

    // Runtime detectors (non-sanitizer)
    static bool handleRuntimePortAlreadyInUse(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "address already in use") ||
          icontains(runtimeLog, "eaddrinuse") ||
          (icontains(runtimeLog, "bind") && icontains(runtimeLog, "already in use"));

      if (!hit)
        return false;

      std::string port;
      {
        static const std::regex re1(R"(:([0-9]{2,5}))");
        std::smatch m;
        if (std::regex_search(runtimeLog, m, re1))
          port = m[1].str();

        if (port.empty())
        {
          static const std::regex re2(R"(port[^0-9]*([0-9]{2,5}))", std::regex::icase);
          if (std::regex_search(runtimeLog, m, re2))
            port = m[1].str();
        }
      }

      print_header("runtime error: address already in use");

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(
            *loc,
            port.empty() ? "this port is already in use (stop the other process or change the port)"
                         : ("port " + port + " is already in use (stop the other process or change the port)"));
      }
      else
      {
        print_hint_at_bottom(
            port.empty() ? "this port is already in use (stop the other process or change the port)"
                         : ("port " + port + " is already in use (stop the other process or change the port)"),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeSegfaultAbortTerminate(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool segv =
          icontains(runtimeLog, "segmentation fault") ||
          icontains(runtimeLog, "sigsegv");

      const bool abrt =
          icontains(runtimeLog, "sigabrt") ||
          icontains(runtimeLog, "aborted") ||
          icontains(runtimeLog, "abort()");

      const bool term =
          icontains(runtimeLog, "terminate called after") ||
          icontains(runtimeLog, "std::terminate") ||
          icontains(runtimeLog, "terminating") ||
          icontains(runtimeLog, "pure virtual method called");

      if (!(segv || abrt || term))
        return false;

      std::string title = "runtime error: crash";
      std::string hint = "program crashed";

      if (segv)
      {
        title = "runtime error: segmentation fault";
        hint = "invalid memory access (null/dangling pointer, out-of-bounds, use-after-free)";
      }
      else if (abrt)
      {
        title = "runtime error: aborted";
        hint = "the program aborted (assert/terminate/abort)";
      }
      else if (term)
      {
        title = "runtime error: terminate";
        hint = "std::terminate/pure-virtual/unhandled fatal error";
      }

      hint = maybe_add_san_hint(hint, runtimeLog);

      print_header(title);

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, maybe_add_san_hint(hint, runtimeLog));
      }
      else
      {
        print_hint_at_bottom(hint, !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeAssertionFailed(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "assertion") && icontains(runtimeLog, "failed");

      if (!hit)
        return false;

      print_header("runtime error: assertion failed");

      const std::string hint =
          "an assertion failed (a condition you expected to be true was false). check invariants and input validation";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(hint, !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeBadAllocOOM(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool badAlloc =
          icontains(runtimeLog, "std::bad_alloc") ||
          icontains(runtimeLog, "bad_alloc");

      const bool oom =
          icontains(runtimeLog, "cannot allocate memory") ||
          icontains(runtimeLog, "out of memory") ||
          icontains(runtimeLog, "killed") && icontains(runtimeLog, "oom");

      if (!(badAlloc || oom))
        return false;

      print_header("runtime error: out of memory");

      const std::string hint =
          "allocation failed (std::bad_alloc / OOM). reduce memory usage, check leaks, or increase available memory";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(hint, !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    // Runtime detectors (allocator/memory)
    static bool handleRuntimeDoubleFreeInvalidFree(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "free(): invalid pointer") ||
          icontains(runtimeLog, "munmap_chunk(): invalid pointer") ||
          (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "not malloc()-ed")) ||
          (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "attempting free"));

      if (!hit)
        return false;

      print_header("runtime error: invalid free");

      const std::string hint =
          "you freed a pointer that was not a valid heap allocation (or not owned)";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeHeapUseAfterFree(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "heap-use-after-free") ||
          (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "use-after-free"));

      if (!hit)
        return false;

      print_header("runtime error: use-after-free");

      const std::string hint =
          "you used memory after it was freed (dangling pointer/reference)";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeAllocDeallocMismatch(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hasAsan = icontains(runtimeLog, "AddressSanitizer");

      const bool explicitMismatch =
          icontains(runtimeLog, "alloc-dealloc-mismatch") ||
          icontains(runtimeLog, "operator new [] vs operator delete") ||
          icontains(runtimeLog, "operator new vs operator delete []") ||
          icontains(runtimeLog, "malloc vs operator delete") ||
          icontains(runtimeLog, "operator new vs free") ||
          icontains(runtimeLog, "malloc vs delete") ||
          icontains(runtimeLog, "malloc vs operator delete[]") ||
          icontains(runtimeLog, "operator new[] vs free") ||
          icontains(runtimeLog, "new-delete-type-mismatch");

      const bool fuzzyMismatch =
          icontains(runtimeLog, "new[]") &&
          icontains(runtimeLog, "delete") &&
          icontains(runtimeLog, "mismatch");

      const bool hit = explicitMismatch || (hasAsan && (explicitMismatch || fuzzyMismatch));

      if (!hit)
        return false;

      print_header("runtime error: alloc/dealloc mismatch");

      const std::string hint =
          "free memory with the matching API (new/delete, new[]/delete[], malloc/free)";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeDoubleFree(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "attempting double-free") ||
          icontains(runtimeLog, "double-free") ||
          icontains(runtimeLog, "double free") ||
          icontains(runtimeLog, "free(): double free") ||
          icontains(runtimeLog, "double free detected") ||
          (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "double-free"));

      if (!hit)
        return false;

      print_header("runtime error: double free");

      const std::string hint =
          "the same allocation was freed twice (double owner or duplicate delete/free)";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeUseAfterReturn(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "use-after-return") ||
          (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "use-after-return"));

      if (!hit)
        return false;

      print_header("runtime error: use-after-return");

      const std::string hint =
          "a pointer/reference/view escaped a function and outlived its stack variable";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeStackUseAfterScope(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "stack-use-after-scope") ||
          icontains(runtimeLog, "use-after-scope") ||
          (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "stack-use-after-scope"));

      if (!hit)
        return false;

      print_header("runtime error: stack-use-after-scope");

      const std::string hint =
          "a reference/view/span outlived the object it refers to";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    static bool handleRuntimeBufferOverflow(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool isHeapBO = icontains(runtimeLog, "heap-buffer-overflow");
      const bool isStackBO = icontains(runtimeLog, "stack-buffer-overflow");
      const bool isGlobalBO = icontains(runtimeLog, "global-buffer-overflow");
      const bool isStackOverflow = icontains(runtimeLog, "stack-overflow") || icontains(runtimeLog, "stack overflow");

      const bool isOutOfBounds =
          icontains(runtimeLog, "out of bounds") ||
          icontains(runtimeLog, "out-of-bounds") ||
          icontains(runtimeLog, "index out of range");

      const bool genericOverflow =
          icontains(runtimeLog, "buffer-overflow") ||
          icontains(runtimeLog, "buffer overflow") ||
          icontains(runtimeLog, "heap overflow") ||
          icontains(runtimeLog, "stack overflow") ||
          (icontains(runtimeLog, "AddressSanitizer") && icontains(runtimeLog, "overflow"));

      const bool hit =
          isHeapBO || isStackBO || isGlobalBO || isStackOverflow || isOutOfBounds || genericOverflow;

      if (!hit)
        return false;

      std::string title = "runtime error: buffer overflow";
      std::string hint = "read/write went past bounds (check indices and sizes)";

      if (isHeapBO)
      {
        title = "runtime error: heap-buffer-overflow";
        hint = "heap out-of-bounds (check vector/string indexing and sizes)";
      }
      else if (isStackBO)
      {
        title = "runtime error: stack-buffer-overflow";
        hint = "stack out-of-bounds (local array overflow or too large stack buffer)";
      }
      else if (isGlobalBO)
      {
        title = "runtime error: global-buffer-overflow";
        hint = "global out-of-bounds (global array overflow)";
      }
      else if (isStackOverflow)
      {
        title = "runtime error: stack overflow";
        hint = "deep recursion or huge stack allocations. reduce recursion depth or move buffers to heap";
      }
      else if (isOutOfBounds)
      {
        title = "runtime error: out-of-bounds access";
        hint = "index out of bounds (check indices, sizes, and signed/unsigned conversions)";
      }

      print_header(title);

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(runtimeLog);
      }

      return true;
    }

    // Sanitizers (typed UBSan + LSan/TSan + generic)
    static std::optional<std::string> extractUBSanKind(const std::string &log)
    {
      // UBSan formats:
      //  1) file:line:col: runtime error: <kind>
      //  2) runtime error: <kind>
      {
        static const std::regex re(R"(runtime error:\s*([^\n]+))", std::regex::icase);
        std::smatch m;
        if (std::regex_search(log, m, re))
        {
          std::string k = m[1].str();
          k = std::string(trim_view(k));
          // trim after " (" if present
          const std::size_t p = k.find(" (");
          if (p != std::string::npos)
            k = k.substr(0, p);
          return k;
        }
      }
      return std::nullopt;
    }

    static std::pair<std::string, std::string> ubsanTitleHintFromKind(std::string_view kind)
    {
      const std::string k = std::string(trim_view(kind));

      auto has = [&](std::string_view needle)
      { return icontains(k, needle); };

      if (has("signed integer overflow"))
        return {"runtime error: signed integer overflow", "a signed integer overflow occurred (undefined behavior). use wider types or check bounds"};
      if (has("division by zero"))
        return {"runtime error: division by zero", "division/modulo by zero (undefined behavior). guard denominators"};
      if (has("shift exponent") || has("shift count") || has("shift out of bounds"))
        return {"runtime error: invalid shift", "shift count is invalid (negative or >= bit-width). validate shift values"};
      if (has("load of misaligned address") || has("misaligned"))
        return {"runtime error: misaligned access", "misaligned pointer dereference. check casts/packing and alignment"};
      if (has("null pointer") && (has("passed") || has("argument")))
        return {"runtime error: null pointer argument", "a null pointer was passed where non-null was required"};
      if (has("member access within null pointer"))
        return {"runtime error: null pointer dereference", "member access on a null pointer. check object lifetimes and null guards"};
      if (has("invalid vptr") || has("vptr"))
        return {"runtime error: invalid vptr", "object used after destruction or memory corruption (virtual dispatch on invalid object)"};
      if (has("out of bounds") || has("index out of range"))
        return {"runtime error: out-of-bounds access", "index out of bounds (undefined behavior). validate indices/sizes"};

      return {"runtime error: undefined behavior", "undefined behavior detected (UBSan). fix the first reported issue"};
    }

    static bool handleUBSanRuntimeError(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      const bool hasUB =
          icontains(log, "UndefinedBehaviorSanitizer") ||
          icontains(log, "runtime error:");

      if (!hasUB)
        return false;

      const auto kindOpt = extractUBSanKind(log);
      const auto [title, hint] = kindOpt ? ubsanTitleHintFromKind(*kindOpt) : ubsanTitleHintFromKind("");

      print_header(title);

      if (auto loc = tryExtractFirstUserFrame(log, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(maybe_add_san_hint(hint, log),
                             !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(log);
      }

      return true;
    }

    static bool handleLeakSanitizer(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(log, "LeakSanitizer") ||
          icontains(log, "detected memory leaks");

      if (!hit)
        return false;

      print_header("runtime error: memory leak");

      const std::string hint =
          "memory leaks detected (LSan). free allocations or use RAII/smart pointers";

      if (auto loc = tryExtractFirstUserFrame(log, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(hint, !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(log);
      }

      return true;
    }

    static bool handleThreadSanitizer(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(log, "ThreadSanitizer") ||
          icontains(log, "data race");

      if (!hit)
        return false;

      print_header("runtime error: data race");

      const std::string hint =
          "data race detected (TSan). protect shared state (mutex/atomic), avoid unsynchronized access";

      if (auto loc = tryExtractFirstUserFrame(log, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(hint, !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(log);
      }

      return true;
    }

    static bool handleMemorySanitizer(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(log, "MemorySanitizer") ||
          icontains(log, "uninitialized") && icontains(log, "use-of-uninitialized-value");

      if (!hit)
        return false;

      print_header("runtime error: uninitialized memory");

      const std::string hint =
          "use of uninitialized memory (MSan). initialize variables and ensure buffers are written before read";

      if (auto loc = tryExtractFirstUserFrame(log, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(hint, !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(log);
      }

      return true;
    }

    static bool handleGenericSanitizerBanner(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(log, "AddressSanitizer") ||
          icontains(log, "UndefinedBehaviorSanitizer") ||
          icontains(log, "LeakSanitizer") ||
          icontains(log, "ThreadSanitizer") ||
          icontains(log, "MemorySanitizer");

      if (!hit)
        return false;

      print_header("runtime error: sanitizer");

      const std::string hint =
          "sanitizer reported an issue. fix the first reported problem in the log";

      if (auto loc = tryExtractFirstUserFrame(log, sourceFile))
      {
        print_codeframe_then_bottom_default(*loc, hint);
      }
      else
      {
        print_hint_at_bottom(maybe_add_san_hint(hint, log),
                             !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");
        print_excerpt(log);
      }

      return true;
    }

    static bool handleLinkerErrors(const std::string &buildLog, const std::filesystem::path &sourceFile)
    {
      bool hasUndefinedRef = false;
      bool hasLdError = false;

      std::istringstream iss(buildLog);
      std::string line;
      while (std::getline(iss, line))
      {
        if (!hasUndefinedRef && line.find("undefined reference to") != std::string::npos)
          hasUndefinedRef = true;

        if (line.find("ld returned") != std::string::npos ||
            line.find("collect2: error: ld returned") != std::string::npos ||
            line.find("clang: error: linker command failed") != std::string::npos)
          hasLdError = true;
      }

      if (!(hasUndefinedRef || hasLdError))
        return false;

      print_header("link error: undefined reference(s)");

      std::string hint = "a symbol is declared but not linked (missing .cpp or missing library)";
      std::string at = !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "";
      print_hint_at_bottom(hint, at);

      print_excerpt(buildLog);
      return true;
    }

    // Master runtime dispatcher (order matters)
    static bool handleRuntimeAnything(const std::string &log, const std::filesystem::path &sourceFile)
    {
      // 1) Very specific app/runtime failures
      if (handleRuntimePortAlreadyInUse(log, sourceFile))
        return true;

      // 2) C++ exceptions (your existing handler)
      if (vix::cli::errors::rules::handleUncaughtException(log, sourceFile))
        return true;

      // 3) Typed UB (UBSan) MUST be early because it may also contain "runtime error:"
      if (handleUBSanRuntimeError(log, sourceFile))
        return true;

      // 4) Typed allocator/memory families
      if (handleRuntimeAllocDeallocMismatch(log, sourceFile))
        return true;
      if (handleRuntimeDoubleFree(log, sourceFile))
        return true;
      if (handleRuntimeDoubleFreeInvalidFree(log, sourceFile))
        return true;
      if (handleRuntimeHeapUseAfterFree(log, sourceFile))
        return true;
      if (handleRuntimeUseAfterReturn(log, sourceFile))
        return true;
      if (handleRuntimeStackUseAfterScope(log, sourceFile))
        return true;
      if (handleRuntimeBufferOverflow(log, sourceFile))
        return true;

      // 5) Other common crashes & signals
      if (handleRuntimeAssertionFailed(log, sourceFile))
        return true;
      if (handleRuntimeBadAllocOOM(log, sourceFile))
        return true;
      if (handleRuntimeSegfaultAbortTerminate(log, sourceFile))
        return true;

      // 6) Other sanitizers (LSan/TSan/MSan)
      if (handleLeakSanitizer(log, sourceFile))
        return true;
      if (handleThreadSanitizer(log, sourceFile))
        return true;
      if (handleMemorySanitizer(log, sourceFile))
        return true;

      // 7) Generic sanitizer banner (last resort)
      if (handleGenericSanitizerBanner(log, sourceFile))
        return true;

      return false;
    }

  } // namespace

  bool RawLogDetectors::handleKnownRunFailure(const std::string &log, const std::filesystem::path &ctx)
  {
    if (handleRuntimePortAlreadyInUse(log, ctx))
      return true;

    return handleRuntimeAnything(log, ctx);
  }

  bool RawLogDetectors::handleRuntimeCrash(
      const std::string &runtimeLog,
      const std::filesystem::path &sourceFile,
      [[maybe_unused]] const std::string &contextMessage)
  {
    return handleRuntimeAnything(runtimeLog, sourceFile);
  }

  bool RawLogDetectors::handleLinkerOrSanitizer(
      const std::string &buildLog,
      const std::filesystem::path &sourceFile,
      [[maybe_unused]] const std::string &contextMessage)
  {
    if (handleRuntimeAnything(buildLog, sourceFile))
      return true;

    if (handleLinkerErrors(buildLog, sourceFile))
      return true;

    return false;
  }

} // namespace vix::cli::errors
