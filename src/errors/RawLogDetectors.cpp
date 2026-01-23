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
#include <fstream>

#include <algorithm>
#include <cctype>
#include <filesystem>
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

    static bool is_system_path(std::string_view p) noexcept
    {
      return p.find("/usr/include/") != std::string_view::npos ||
             p.find("/usr/lib/") != std::string_view::npos ||
             p.find("/lib/") != std::string_view::npos ||
             p.find("libsanitizer") != std::string_view::npos;
    }

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

      // 1) BEST: exact script absolute path appears in stack
      if (!abs.empty())
      {
        const std::regex re(abs + R"(:([0-9]+)(?::([0-9]+))?)");
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

      // 3) Last resort: no reliable file:line info
      return std::nullopt;
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

    // Sanitizer helpers
    static std::string extractSanitizerKind(const std::vector<std::string> &lines, std::string_view san)
    {
      for (const auto &l : lines)
      {
        if (icontains(l, "ERROR:") && icontains(l, san))
        {
          const std::size_t pos = l.find(':');
          if (pos == std::string::npos)
            continue;
          const std::size_t pos2 = l.find(':', pos + 1);
          if (pos2 == std::string::npos)
            continue;

          std::string_view after = trim_view(std::string_view(l).substr(pos2 + 1));
          const std::size_t sp = after.find(' ');
          if (sp != std::string_view::npos)
            after = after.substr(0, sp);

          return std::string(after);
        }
      }
      return {};
    }

    static std::vector<std::string> extractSanitizerExcerpt(
        const std::vector<std::string> &lines,
        std::string_view san,
        std::size_t maxLines)
    {
      std::size_t firstHit = lines.size();

      for (std::size_t i = 0; i < lines.size(); ++i)
      {
        const std::string &l = lines[i];
        const std::string_view t = trim_view(std::string_view(l));

        const bool interesting =
            icontains(l, san) ||
            icontains(l, "SUMMARY:") ||
            icontains(l, "ERROR:") ||
            startsWith(t, "#") ||
            startsWith(t, "==") ||
            icontains(l, "allocated") ||
            icontains(l, "freed") ||
            icontains(l, "READ") ||
            icontains(l, "WRITE") ||
            icontains(l, "invalid") ||
            icontains(l, "double-free") ||
            icontains(l, "double free") ||
            icontains(l, "overflow");

        if (interesting)
        {
          firstHit = i;
          break;
        }
      }

      if (firstHit != lines.size())
      {
        auto out = excerptWindow(lines, firstHit, 2, 12, maxLines);
        if (!out.empty())
          return out;
      }

      std::vector<std::string> out;
      for (std::size_t i = 0; i < std::min(lines.size(), maxLines); ++i)
        out.push_back(lines[i]);
      return out;
    }

    // Runtime detectors
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

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        std::cerr << RED
                  << "runtime error: invalid free"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: you freed a pointer that was not a valid heap allocation (or not owned)"
                  << RESET << "\n";

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << "runtime error: invalid free"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: freed pointer is invalid (not malloc/new, shifted pointer, or double-free). run with --san for exact location"
                  << RESET << "\n";

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
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

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        std::cerr << RED
                  << "runtime error: use-after-free"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: you used memory after it was freed (dangling pointer/reference)"
                  << RESET << "\n";

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << "runtime error: use-after-free"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: memory was accessed after being freed. run with --san for exact location"
                  << RESET << "\n";

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
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
          icontains(runtimeLog, "operator new[] vs free");

      const bool fuzzyMismatch =
          icontains(runtimeLog, "new[]") &&
          icontains(runtimeLog, "delete") &&
          icontains(runtimeLog, "mismatch");

      const bool hit = explicitMismatch || (hasAsan && (explicitMismatch || fuzzyMismatch));

      if (!hit)
        return false;

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        std::cerr << RED
                  << "runtime error: alloc/dealloc mismatch"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: free memory with the matching API (new/delete, new[]/delete[], malloc/free)"
                  << RESET << "\n";

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << "runtime error: alloc/dealloc mismatch"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: allocated with one API and freed with another. run with --san for exact location"
                  << RESET << "\n";

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
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

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        std::cerr << RED
                  << "runtime error: double free"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: the same allocation was freed twice (double owner or duplicate delete/free)"
                  << RESET << "\n";

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << "runtime error: double free"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: the same allocation was freed twice. run with --san for exact location"
                  << RESET << "\n";

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
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

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        std::cerr << RED
                  << "runtime error: use-after-return"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: a pointer/reference/view escaped a function and outlived its stack variable"
                  << RESET << "\n";

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << "runtime error: use-after-return"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: dangling stack reference/view. run with --san for exact location"
                  << RESET << "\n";

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
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

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        std::cerr << RED
                  << "runtime error: stack-use-after-scope"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: a reference/view/span outlived the object it refers to"
                  << RESET << "\n";

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << "runtime error: stack-use-after-scope"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: dangling stack reference/view/span. run with --san for exact location"
                  << RESET << "\n";

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
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

      const bool isUseAfterScope =
          icontains(runtimeLog, "stack-use-after-scope") ||
          icontains(runtimeLog, "use-after-scope");

      const bool isUseAfterReturn = icontains(runtimeLog, "use-after-return");

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
          isHeapBO || isStackBO || isGlobalBO ||
          isUseAfterScope || isUseAfterReturn ||
          isOutOfBounds || genericOverflow;

      if (!hit)
        return false;

      std::string title = "runtime error: buffer overflow";
      if (isHeapBO)
        title = "runtime error: heap-buffer-overflow";
      else if (isStackBO)
        title = "runtime error: stack-buffer-overflow";
      else if (isGlobalBO)
        title = "runtime error: global-buffer-overflow";
      else if (isUseAfterScope)
        title = "runtime error: stack-use-after-scope";
      else if (isUseAfterReturn)
        title = "runtime error: use-after-return";
      else if (isOutOfBounds)
        title = "runtime error: out-of-bounds access";

      if (auto loc = tryExtractFirstUserFrame(runtimeLog, sourceFile))
      {
        std::cerr << RED
                  << title
                  << RESET << "\n";

        if (isUseAfterReturn)
        {
          std::cerr << YELLOW
                    << "hint: a pointer/reference/view outlived a stack variable (returned from a function)"
                    << RESET << "\n";
        }
        else if (isUseAfterScope)
        {
          std::cerr << YELLOW
                    << "hint: a reference/view/span outlived the object it refers to"
                    << RESET << "\n";
        }
        else
        {
          std::cerr << YELLOW
                    << "hint: read/write went past bounds (check indices and sizes)"
                    << RESET << "\n";
        }

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << title
                  << RESET << "\n";

        if (isUseAfterReturn)
        {
          std::cerr << YELLOW
                    << "hint: dangling stack pointer/reference/view. run with --san for exact location"
                    << RESET << "\n";
        }
        else if (isUseAfterScope)
        {
          std::cerr << YELLOW
                    << "hint: dangling stack reference/view/span. run with --san for exact location"
                    << RESET << "\n";
        }
        else
        {
          std::cerr << YELLOW
                    << "hint: out-of-bounds memory access. run with --san for exact location"
                    << RESET << "\n";
        }

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
      }

      return true;
    }

    static bool handleSanitizers(const std::string &log, const std::filesystem::path &sourceFile)
    {
      const bool hasASan = icontains(log, "AddressSanitizer");
      const bool hasLSan = icontains(log, "LeakSanitizer");
      const bool hasUBSan = icontains(log, "UndefinedBehaviorSanitizer") || icontains(log, "runtime error:");
      const bool hasMSan = icontains(log, "MemorySanitizer");
      const bool hasTSan = icontains(log, "ThreadSanitizer");

      if (!(hasASan || hasLSan || hasUBSan || hasMSan || hasTSan))
        return false;

      const std::string san =
          hasASan    ? "AddressSanitizer"
          : hasLSan  ? "LeakSanitizer"
          : hasUBSan ? "UndefinedBehaviorSanitizer"
          : hasMSan  ? "MemorySanitizer"
                     : "ThreadSanitizer";

      const auto lines = splitLines(log);
      const auto kind = extractSanitizerKind(lines, san);

      if (auto loc = tryExtractFirstUserFrame(log, sourceFile))
      {
        std::cerr << RED
                  << "runtime error: sanitizer"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: fix the first reported issue"
                  << RESET << "\n";

        std::cerr << GREEN
                  << "at: " << loc->file << ":" << loc->line
                  << RESET << "\n";

        ErrorContext ctx;
        CodeFrameOptions opt;
        opt.contextLines = 2;
        opt.maxLineWidth = 120;
        opt.tabWidth = 4;

        printCodeFrame(*loc, ctx, opt);
      }
      else
      {
        std::cerr << RED
                  << "runtime error: sanitizer"
                  << RESET << "\n";

        std::cerr << YELLOW
                  << "hint: " << san;
        if (!kind.empty())
          std::cerr << " (" << kind << ")";
        std::cerr << ". run with --san for exact location"
                  << RESET << "\n";

        if (!sourceFile.empty())
        {
          std::cerr << GREEN
                    << "source: " << sourceFile.filename().string()
                    << RESET << "\n";
        }
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
            line.find("collect2: error: ld returned") != std::string::npos)
          hasLdError = true;
      }

      if (!(hasUndefinedRef || hasLdError))
        return false;

      std::cerr << RED
                << "link error: undefined reference(s)"
                << RESET << "\n";

      std::cerr << YELLOW
                << "hint: a symbol is declared but not linked (missing .cpp or missing library)"
                << RESET << "\n";

      if (!sourceFile.empty())
      {
        std::cerr << GREEN
                  << "source: " << sourceFile.filename().string()
                  << RESET << "\n";
      }

      return true;
    }

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

      std::cerr << RED
                << "runtime error: address already in use"
                << RESET << "\n";

      if (!port.empty())
      {
        std::cerr << YELLOW
                  << "hint: port " << port << " is already in use (stop the other process or change the port)"
                  << RESET << "\n";
      }
      else
      {
        std::cerr << YELLOW
                  << "hint: this port is already in use (stop the other process or change the port)"
                  << RESET << "\n";
      }

      if (!sourceFile.empty())
      {
        std::cerr << GREEN
                  << "source: " << sourceFile.filename().string()
                  << RESET << "\n";
      }

      return true;
    }

  } // namespace

  bool RawLogDetectors::handleKnownRunFailure(const std::string &log, const std::filesystem::path &ctx)
  {
    return handleRuntimePortAlreadyInUse(log, ctx);
  }

  bool RawLogDetectors::handleRuntimeCrash(
      const std::string &runtimeLog,
      const std::filesystem::path &sourceFile,
      [[maybe_unused]] const std::string &contextMessage)
  {

    // Order matters: keep specific ones first.
    if (handleRuntimePortAlreadyInUse(runtimeLog, sourceFile))
      return true;

    if (vix::cli::errors::rules::handleUncaughtException(runtimeLog, sourceFile))
      return true;

    if (handleRuntimeAllocDeallocMismatch(runtimeLog, sourceFile))
      return true;

    if (handleRuntimeDoubleFree(runtimeLog, sourceFile))
      return true;

    if (handleRuntimeDoubleFreeInvalidFree(runtimeLog, sourceFile))
      return true;

    if (handleRuntimeHeapUseAfterFree(runtimeLog, sourceFile))
      return true;

    if (handleRuntimeUseAfterReturn(runtimeLog, sourceFile))
      return true;

    if (handleRuntimeStackUseAfterScope(runtimeLog, sourceFile))
      return true;

    if (handleRuntimeBufferOverflow(runtimeLog, sourceFile))
      return true;

    if (handleSanitizers(runtimeLog, sourceFile))
      return true;

    return false;
  }

  bool RawLogDetectors::handleLinkerOrSanitizer(
      const std::string &buildLog,
      const std::filesystem::path &sourceFile,
      [[maybe_unused]] const std::string &contextMessage)
  {
    if (handleRuntimeAllocDeallocMismatch(buildLog, sourceFile))
      return true;

    if (handleRuntimeDoubleFree(buildLog, sourceFile))
      return true;

    if (handleRuntimeDoubleFreeInvalidFree(buildLog, sourceFile))
      return true;

    if (handleRuntimeHeapUseAfterFree(buildLog, sourceFile))
      return true;

    if (handleRuntimeUseAfterReturn(buildLog, sourceFile))
      return true;

    if (handleRuntimeStackUseAfterScope(buildLog, sourceFile))
      return true;

    if (handleRuntimeBufferOverflow(buildLog, sourceFile))
      return true;

    if (handleSanitizers(buildLog, sourceFile))
      return true;

    if (handleLinkerErrors(buildLog, sourceFile))
      return true;

    return false;
  }

} // namespace vix::cli::errors
