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
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>

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
    char to_lower_ascii(unsigned char c) noexcept
    {
      return static_cast<char>(std::tolower(c));
    }

    bool icontains(std::string_view haystack, std::string_view needle) noexcept
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

          if (to_lower_ascii(a) != to_lower_ascii(b))
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

    bool log_looks_sanitized(const std::string &log) noexcept
    {
      return icontains(log, "AddressSanitizer") ||
             icontains(log, "UndefinedBehaviorSanitizer") ||
             icontains(log, "LeakSanitizer") ||
             icontains(log, "ThreadSanitizer") ||
             icontains(log, "MemorySanitizer") ||
             (icontains(log, "==") && icontains(log, "==ABORTING"));
    }

    std::string maybe_add_san_hint(
        std::string hint,
        const std::string &log)
    {
      if (log_looks_sanitized(log))
        return hint;

      hint += ". run with --san for exact location";
      return hint;
    }

    std::string escape_regex(std::string text)
    {
      std::string out;
      out.reserve(text.size() + 16);

      for (const char c : text)
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

    std::vector<std::string> split_lines(const std::string &text)
    {
      std::vector<std::string> lines;
      std::istringstream input(text);
      std::string line;

      while (std::getline(input, line))
        lines.push_back(line);

      return lines;
    }

    std::string_view trim_view(std::string_view text) noexcept
    {
      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.front())) != 0)
      {
        text.remove_prefix(1);
      }

      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.back())) != 0)
      {
        text.remove_suffix(1);
      }

      return text;
    }

    bool starts_with(std::string_view text, std::string_view prefix) noexcept
    {
      return text.size() >= prefix.size() &&
             text.substr(0, prefix.size()) == prefix;
    }

    bool is_system_path(std::string_view path) noexcept
    {
      return path.find("/usr/include/") != std::string_view::npos ||
             path.find("/usr/lib/") != std::string_view::npos ||
             path.find("/usr/local/include/") != std::string_view::npos ||
             path.find("/usr/local/lib/") != std::string_view::npos ||
             path.find("/lib/") != std::string_view::npos ||
             path.find("/lib64/") != std::string_view::npos ||
             path.find("libsanitizer") != std::string_view::npos;
    }

    std::vector<std::string> excerpt_window(
        const std::vector<std::string> &lines,
        std::size_t center,
        std::size_t before,
        std::size_t after,
        std::size_t maxLines)
    {
      std::vector<std::string> out;

      if (lines.empty())
        return out;

      const std::size_t start = center > before ? center - before : 0;
      const std::size_t end = std::min(lines.size(), center + after + 1);

      for (std::size_t i = start; i < end && out.size() < maxLines; ++i)
        out.push_back(lines[i]);

      return out;
    }

    void print_excerpt(
        const std::string &log,
        std::size_t maxLines = 12)
    {
      const auto lines = split_lines(log);

      std::size_t firstHit = lines.size();

      for (std::size_t i = 0; i < lines.size(); ++i)
      {
        const std::string_view trimmed =
            trim_view(std::string_view(lines[i]));

        const bool interesting =
            icontains(lines[i], "ERROR:") ||
            icontains(lines[i], "SUMMARY:") ||
            icontains(lines[i], "runtime error:") ||
            icontains(lines[i], "AddressSanitizer") ||
            icontains(lines[i], "UndefinedBehaviorSanitizer") ||
            icontains(lines[i], "LeakSanitizer") ||
            icontains(lines[i], "ThreadSanitizer") ||
            icontains(lines[i], "MemorySanitizer") ||
            starts_with(trimmed, "#") ||
            starts_with(trimmed, "==") ||
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

      const auto window =
          firstHit != lines.size()
              ? excerpt_window(lines, firstHit, 2, 10, maxLines)
              : excerpt_window(lines, 0, 0, maxLines - 1, maxLines);

      if (window.empty())
        return;

      std::cerr << "\n";
      std::cerr << RED << "log:" << RESET << "\n";

      for (const auto &line : window)
      {
        if (starts_with(trim_view(line), "==") &&
            icontains(line, "ABORTING"))
        {
          continue;
        }

        std::cerr << "  " << line << "\n";
      }

      std::cerr << "\n";
    }

    std::optional<CompilerError> try_extract_first_user_frame(
        const std::string &log,
        const std::filesystem::path &fallbackSource)
    {
      auto make_location = [](const std::string &file, int line, int column)
      {
        CompilerError err;
        err.file = file;
        err.line = line;
        err.column = column > 0 ? column : 1;
        return err;
      };

      std::string canonicalSource;

      if (!fallbackSource.empty())
      {
        std::error_code ec;
        canonicalSource =
            std::filesystem::weakly_canonical(fallbackSource, ec).string();

        if (ec)
          canonicalSource = fallbackSource.string();
      }

      {
        static const std::regex re(
            R"((/[^:\n]+?\.(?:c|cc|cpp|cxx|h|hpp|hh)):(\d+):(\d+):\s*runtime error:)",
            std::regex::ECMAScript);

        std::smatch match;

        if (std::regex_search(log, match, re))
        {
          const std::string file = match[1].str();
          const int line = std::stoi(match[2].str());
          const int column = std::stoi(match[3].str());

          if (!is_system_path(file))
            return make_location(file, line, column);
        }
      }

      if (!canonicalSource.empty())
      {
        const std::regex re(
            escape_regex(canonicalSource) + R"(:([0-9]+)(?::([0-9]+))?)");

        std::smatch match;

        if (std::regex_search(log, match, re))
        {
          const int line = std::stoi(match[1].str());
          const int column =
              match.size() >= 3 && match[2].matched
                  ? std::stoi(match[2].str())
                  : 1;

          return make_location(canonicalSource, line, column);
        }
      }

      {
        static const std::regex re(
            R"((/[^:\n]+?\.(?:c|cc|cpp|cxx|h|hpp|hh)):(\d+)(?::(\d+))?)");

        std::smatch match;
        std::string::const_iterator it = log.begin();

        while (std::regex_search(it, log.cend(), match, re))
        {
          const std::string file = match[1].str();
          const int line = std::stoi(match[2].str());
          const int column =
              match.size() >= 4 && match[3].matched
                  ? std::stoi(match[3].str())
                  : 1;

          if (!is_system_path(file))
            return make_location(file, line, column);

          it = match.suffix().first;
        }
      }

      return std::nullopt;
    }

    void print_header(std::string_view title)
    {
      std::cerr << RED
                << title
                << RESET << "\n";
    }

    void print_hint_at_bottom(
        std::string_view hint,
        std::string_view at)
    {
      if (!hint.empty())
      {
        std::cerr << YELLOW
                  << "hint: "
                  << RESET
                  << hint
                  << "\n";
      }

      if (!at.empty())
      {
        std::cerr << GREEN
                  << "at: "
                  << RESET
                  << at
                  << "\n";
      }
    }

    void print_codeframe_then_bottom(
        const CompilerError &location,
        const ErrorContext &ctx,
        const CodeFrameOptions &options,
        std::string_view hint)
    {
      printCodeFrame(location, ctx, options);

      const std::string at =
          location.file + ":" + std::to_string(location.line);

      print_hint_at_bottom(hint, at);
    }

    void print_codeframe_then_bottom_default(
        const CompilerError &location,
        std::string_view hint)
    {
      ErrorContext ctx;

      CodeFrameOptions options;
      options.contextLines = 2;
      options.maxLineWidth = 120;
      options.tabWidth = 4;

      print_codeframe_then_bottom(location, ctx, options, hint);
    }

    static std::optional<std::vector<std::string>> read_file_lines(
        const std::filesystem::path &path)
    {
      std::ifstream input(path);

      if (!input)
        return std::nullopt;

      std::vector<std::string> lines;
      std::string line;

      while (std::getline(input, line))
        lines.push_back(line);

      return lines;
    }

    std::string strip_line_comment_copy(const std::string &line)
    {
      const std::size_t pos = line.find("//");

      if (pos == std::string::npos)
        return line;

      return line.substr(0, pos);
    }

    std::string ltrim_copy(std::string text)
    {
      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.front())) != 0)
      {
        text.erase(text.begin());
      }

      return text;
    }

    std::optional<CompilerError> try_find_suspicious_free_location(
        const std::filesystem::path &sourceFile)
    {
      if (sourceFile.empty())
        return std::nullopt;

      const auto lines = read_file_lines(sourceFile);

      if (!lines)
        return std::nullopt;

      for (std::size_t i = 0; i < lines->size(); ++i)
      {
        const std::string line =
            strip_line_comment_copy((*lines)[i]);

        std::size_t pos = line.find("delete[]");

        if (pos == std::string::npos)
          pos = line.find("delete ");

        if (pos == std::string::npos)
          pos = line.find("free(");

        if (pos == std::string::npos)
          continue;

        CompilerError location;
        location.file = sourceFile.string();
        location.line = static_cast<int>(i + 1);
        location.column = static_cast<int>(pos + 1);
        location.message = "possible free location";

        return location;
      }

      return std::nullopt;
    }

    std::optional<std::string> try_extract_copy_initialized_type(
        const std::string &line)
    {
      static const std::regex re(
          R"(^\s*([A-Za-z_]\w*)\s+[A-Za-z_]\w*\s*=\s*[A-Za-z_]\w*\s*;\s*$)");

      std::smatch match;

      if (!std::regex_match(line, match, re))
        return std::nullopt;

      return match[1].str();
    }

    std::optional<std::pair<std::size_t, std::size_t>> find_type_block_range(
        const std::vector<std::string> &lines,
        const std::string &typeName)
    {
      const std::regex startRe(
          "^[[:space:]]*(struct|class)[[:space:]]+" +
          escape_regex(typeName) +
          R"((\s|$))");

      for (std::size_t i = 0; i < lines.size(); ++i)
      {
        if (!std::regex_search(lines[i], startRe))
          continue;

        bool opened = false;
        int depth = 0;

        for (std::size_t j = i; j < lines.size(); ++j)
        {
          for (char c : lines[j])
          {
            if (c == '{')
            {
              opened = true;
              ++depth;
            }
            else if (c == '}')
            {
              --depth;

              if (opened && depth <= 0)
                return std::make_pair(i, j);
            }
          }
        }
      }

      return std::nullopt;
    }

    bool type_block_has_destructor(
        const std::vector<std::string> &lines,
        std::size_t begin,
        std::size_t end,
        const std::string &typeName)
    {
      const std::regex destructorRe(
          "~" + escape_regex(typeName) + R"(\s*\()");

      for (std::size_t i = begin; i <= end && i < lines.size(); ++i)
      {
        if (std::regex_search(lines[i], destructorRe))
          return true;
      }

      return false;
    }

    bool type_block_has_raw_pointer_field(
        const std::vector<std::string> &lines,
        std::size_t begin,
        std::size_t end)
    {
      static const std::regex pointerFieldRe(
          R"(\b[A-Za-z_]\w*(?:::\w+)?\s*\*\s*[A-Za-z_]\w*\s*(?:=\s*[^;]+)?;)");

      for (std::size_t i = begin; i <= end && i < lines.size(); ++i)
      {
        const std::string trimmed = ltrim_copy(lines[i]);

        if (trimmed.rfind("//", 0) == 0)
          continue;

        if (std::regex_search(lines[i], pointerFieldRe))
          return true;
      }

      return false;
    }

    std::optional<std::string> try_explain_cpp_shallow_copy_double_free(
        const CompilerError &location)
    {
      const auto lines = read_file_lines(location.file);

      if (!lines)
        return std::nullopt;

      if (location.line <= 0 ||
          static_cast<std::size_t>(location.line) > lines->size())
      {
        return std::nullopt;
      }

      const std::string &faultLine =
          (*lines)[static_cast<std::size_t>(location.line - 1)];

      const auto typeName = try_extract_copy_initialized_type(faultLine);

      if (!typeName)
        return std::nullopt;

      const auto block = find_type_block_range(*lines, *typeName);

      if (!block)
        return std::nullopt;

      const auto [begin, end] = *block;

      const bool hasDestructor =
          type_block_has_destructor(*lines, begin, end, *typeName);

      const bool hasRawPointer =
          type_block_has_raw_pointer_field(*lines, begin, end);

      if (hasDestructor && hasRawPointer)
        return std::string("shallow copy of raw pointer owner");

      return std::nullopt;
    }

    bool handleRuntimePortAlreadyInUse(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "address already in use") ||
          icontains(runtimeLog, "eaddrinuse") ||
          (icontains(runtimeLog, "bind") &&
           icontains(runtimeLog, "already in use"));

      if (!hit)
        return false;

      std::string port;

      {
        static const std::regex re1(R"(:([0-9]{2,5}))");
        std::smatch match;

        if (std::regex_search(runtimeLog, match, re1))
          port = match[1].str();

        if (port.empty())
        {
          static const std::regex re2(
              R"(port[^0-9]*([0-9]{2,5}))",
              std::regex::icase);

          if (std::regex_search(runtimeLog, match, re2))
            port = match[1].str();
        }
      }

      print_header("runtime error: address already in use");

      const std::string hint =
          port.empty()
              ? "this port is already in use; stop the other process or change the port"
              : "port " + port + " is already in use; stop the other process or change the port";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            hint,
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeAssertionFailed(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "assertion") &&
          icontains(runtimeLog, "failed");

      if (!hit)
        return false;

      print_header("runtime error: assertion failed");

      const std::string hint =
          "an expected condition was false; check invariants and input validation";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            hint,
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeBadAllocOOM(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool badAlloc =
          icontains(runtimeLog, "std::bad_alloc") ||
          icontains(runtimeLog, "bad_alloc");

      const bool oom =
          icontains(runtimeLog, "cannot allocate memory") ||
          icontains(runtimeLog, "out of memory") ||
          (icontains(runtimeLog, "killed") &&
           icontains(runtimeLog, "oom"));

      if (!badAlloc && !oom)
        return false;

      print_header("runtime error: out of memory");

      const std::string hint =
          "allocation failed; reduce memory usage, check leaks, or increase available memory";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            hint,
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeInvalidFree(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "free(): invalid pointer") ||
          icontains(runtimeLog, "munmap_chunk(): invalid pointer") ||
          (icontains(runtimeLog, "AddressSanitizer") &&
           icontains(runtimeLog, "not malloc()-ed")) ||
          (icontains(runtimeLog, "AddressSanitizer") &&
           icontains(runtimeLog, "attempting free"));

      if (!hit)
        return false;

      print_header("runtime error: invalid free");

      const std::string hint =
          "only free memory that was allocated on the heap and is still owned";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else if (auto guessed = try_find_suspicious_free_location(sourceFile))
      {
        print_codeframe_then_bottom_default(*guessed, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeHeapUseAfterFree(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "heap-use-after-free") ||
          (icontains(runtimeLog, "AddressSanitizer") &&
           icontains(runtimeLog, "use-after-free"));

      if (!hit)
        return false;

      std::string title = "runtime error: use-after-free";
      std::string hint =
          "you used memory after it was freed; check dangling pointers and references";

      if (icontains(runtimeLog, "std::vector") ||
          icontains(runtimeLog, "std::__debug::vector") ||
          icontains(runtimeLog, "normal_iterator") ||
          icontains(runtimeLog, "Safe_iterator"))
      {
        title = "runtime error: iterator invalidation";
        hint = "refresh iterators after push_back, insert, erase, resize, reserve, or reallocation";
      }

      print_header(title);

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeAllocDeallocMismatch(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hasAsan =
          icontains(runtimeLog, "AddressSanitizer");

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

      const bool hit =
          explicitMismatch || (hasAsan && fuzzyMismatch);

      if (!hit)
        return false;

      print_header("runtime error: alloc/dealloc mismatch");

      const std::string hint =
          "free memory with the matching API: new/delete, new[]/delete[], malloc/free";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else if (auto guessed = try_find_suspicious_free_location(sourceFile))
      {
        print_codeframe_then_bottom_default(*guessed, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeDoubleFree(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "attempting double-free") ||
          icontains(runtimeLog, "double-free") ||
          icontains(runtimeLog, "double free") ||
          icontains(runtimeLog, "free(): double free") ||
          icontains(runtimeLog, "double free detected") ||
          (icontains(runtimeLog, "AddressSanitizer") &&
           icontains(runtimeLog, "double-free"));

      if (!hit)
        return false;

      print_header("runtime error: double free");

      const std::string genericHint =
          "the same allocation was freed twice; check duplicate ownership";

      auto print_location = [&](const CompilerError &location) -> bool
      {
        if (auto note = try_explain_cpp_shallow_copy_double_free(location))
        {
          ErrorContext ctx;

          CodeFrameOptions options;
          options.contextLines = 2;
          options.maxLineWidth = 120;
          options.tabWidth = 4;

          CompilerError annotated = location;
          annotated.message = *note;

          printCodeFrame(annotated, ctx, options);

          print_hint_at_bottom(
              "define copy constructor/copy assignment, disable copying, or use std::vector/std::unique_ptr",
              location.file + ":" + std::to_string(location.line));

          return true;
        }

        print_codeframe_then_bottom_default(location, genericHint);
        return true;
      };

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
        return print_location(*location);

      if (auto guessed = try_find_suspicious_free_location(sourceFile))
        return print_location(*guessed);

      print_hint_at_bottom(
          genericHint,
          !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

      return true;
    }

    bool handleRuntimeUseAfterReturn(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "use-after-return") ||
          (icontains(runtimeLog, "AddressSanitizer") &&
           icontains(runtimeLog, "use-after-return"));

      if (!hit)
        return false;

      print_header("runtime error: use-after-return");

      const std::string hint =
          "a pointer, reference, view, or span escaped a function and outlived local storage";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeStackUseAfterScope(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "stack-use-after-scope") ||
          icontains(runtimeLog, "use-after-scope") ||
          (icontains(runtimeLog, "AddressSanitizer") &&
           icontains(runtimeLog, "stack-use-after-scope"));

      if (!hit)
        return false;

      print_header("runtime error: stack-use-after-scope");

      const std::string hint =
          "a reference, view, or span outlived the object it refers to";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleRuntimeBufferOverflow(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool isHeap =
          icontains(runtimeLog, "heap-buffer-overflow");

      const bool isStack =
          icontains(runtimeLog, "stack-buffer-overflow");

      const bool isGlobal =
          icontains(runtimeLog, "global-buffer-overflow");

      const bool isStackOverflow =
          icontains(runtimeLog, "stack-overflow") ||
          icontains(runtimeLog, "stack overflow");

      const bool isOutOfBounds =
          icontains(runtimeLog, "out of bounds") ||
          icontains(runtimeLog, "out-of-bounds") ||
          icontains(runtimeLog, "index out of range");

      const bool genericOverflow =
          icontains(runtimeLog, "buffer-overflow") ||
          icontains(runtimeLog, "buffer overflow") ||
          icontains(runtimeLog, "heap overflow") ||
          icontains(runtimeLog, "stack overflow") ||
          (icontains(runtimeLog, "AddressSanitizer") &&
           icontains(runtimeLog, "overflow"));

      const bool hit =
          isHeap || isStack || isGlobal ||
          isStackOverflow || isOutOfBounds || genericOverflow;

      if (!hit)
        return false;

      std::string title = "runtime error: buffer overflow";
      std::string hint = "check indices, sizes, and buffer boundaries";

      if (isHeap)
      {
        title = "runtime error: heap-buffer-overflow";
        hint = "check heap buffer, vector, or string bounds";
      }
      else if (isStack)
      {
        title = "runtime error: stack-buffer-overflow";
        hint = "check local array bounds or move large buffers to the heap";
      }
      else if (isGlobal)
      {
        title = "runtime error: global-buffer-overflow";
        hint = "check global array bounds";
      }
      else if (isStackOverflow)
      {
        title = "runtime error: stack overflow";
        hint = "reduce recursion depth or move large stack buffers to the heap";
      }
      else if (isOutOfBounds)
      {
        title = "runtime error: out-of-bounds access";
        hint = "check indices, sizes, and signed/unsigned conversions";
      }

      print_header(title);

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    std::optional<std::string> extract_ubsan_kind(const std::string &log)
    {
      static const std::regex re(
          R"(runtime error:\s*([^\n]+))",
          std::regex::icase);

      std::smatch match;

      if (!std::regex_search(log, match, re))
        return std::nullopt;

      std::string kind = match[1].str();
      kind = std::string(trim_view(kind));

      const std::size_t pos = kind.find(" (");
      if (pos != std::string::npos)
        kind = kind.substr(0, pos);

      return kind;
    }

    std::pair<std::string, std::string> ubsan_title_hint_from_kind(
        std::string_view kind)
    {
      const std::string normalized =
          std::string(trim_view(kind));

      auto has = [&](std::string_view needle)
      {
        return icontains(normalized, needle);
      };

      if (has("signed integer overflow"))
      {
        return {
            "runtime error: signed integer overflow",
            "use wider integer types or check bounds before arithmetic",
        };
      }

      if (has("division by zero"))
      {
        return {
            "runtime error: division by zero",
            "guard denominators before division or modulo",
        };
      }

      if (has("shift exponent") ||
          has("shift count") ||
          has("shift out of bounds"))
      {
        return {
            "runtime error: invalid shift",
            "validate shift values before shifting",
        };
      }

      if (has("load of misaligned address") ||
          has("misaligned"))
      {
        return {
            "runtime error: misaligned access",
            "check casts, packed structs, and pointer alignment",
        };
      }

      if (has("null pointer") &&
          (has("passed") || has("argument")))
      {
        return {
            "runtime error: null pointer argument",
            "avoid passing null where a valid pointer is required",
        };
      }

      if (has("member access within null pointer"))
      {
        return {
            "runtime error: null pointer dereference",
            "check pointers before member access",
        };
      }

      if (has("invalid vptr") ||
          has("vptr"))
      {
        return {
            "runtime error: invalid vptr",
            "check object lifetime before virtual dispatch",
        };
      }

      if (has("out of bounds") ||
          has("index out of range"))
      {
        return {
            "runtime error: out-of-bounds access",
            "validate indices and container sizes before access",
        };
      }

      return {
          "runtime error: undefined behavior",
          "fix the first undefined behavior reported by UBSan",
      };
    }

    bool handleUBSanRuntimeError(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hasUB =
          icontains(runtimeLog, "UndefinedBehaviorSanitizer") ||
          icontains(runtimeLog, "runtime error:");

      if (!hasUB)
        return false;

      const auto kind = extract_ubsan_kind(runtimeLog);
      const auto [title, hint] =
          kind ? ubsan_title_hint_from_kind(*kind)
               : ubsan_title_hint_from_kind("");

      print_header(title);

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            maybe_add_san_hint(hint, runtimeLog),
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleLeakSanitizer(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "LeakSanitizer") ||
          icontains(runtimeLog, "detected memory leaks");

      if (!hit)
        return false;

      print_header("runtime error: memory leak");

      const std::string hint =
          "free allocations or use RAII and smart pointers";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            hint,
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleThreadSanitizer(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "ThreadSanitizer") ||
          icontains(runtimeLog, "data race");

      if (!hit)
        return false;

      print_header("runtime error: data race");

      const std::string hint =
          "protect shared mutable state with a mutex, lock, or atomic";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            hint,
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleMemorySanitizer(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "MemorySanitizer") ||
          (icontains(runtimeLog, "uninitialized") &&
           icontains(runtimeLog, "use-of-uninitialized-value"));

      if (!hit)
        return false;

      print_header("runtime error: uninitialized memory");

      const std::string hint =
          "initialize variables and write buffers before reading them";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            hint,
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleGenericSanitizerBanner(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const bool hit =
          icontains(runtimeLog, "AddressSanitizer") ||
          icontains(runtimeLog, "UndefinedBehaviorSanitizer") ||
          icontains(runtimeLog, "LeakSanitizer") ||
          icontains(runtimeLog, "ThreadSanitizer") ||
          icontains(runtimeLog, "MemorySanitizer");

      if (!hit)
        return false;

      print_header("runtime error: sanitizer failure");

      const std::string hint =
          "fix the first sanitizer issue reported in the log";

      if (auto location = try_extract_first_user_frame(runtimeLog, sourceFile))
      {
        print_codeframe_then_bottom_default(*location, hint);
      }
      else
      {
        print_hint_at_bottom(
            hint,
            !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handleLinkerErrors(
        const std::string &buildLog,
        const std::filesystem::path &sourceFile)
    {
      bool hasUndefinedReference = false;
      bool hasLinkerFailure = false;

      std::istringstream input(buildLog);
      std::string line;

      while (std::getline(input, line))
      {
        if (!hasUndefinedReference &&
            line.find("undefined reference to") != std::string::npos)
        {
          hasUndefinedReference = true;
        }

        if (line.find("ld returned") != std::string::npos ||
            line.find("collect2: error: ld returned") != std::string::npos ||
            line.find("clang: error: linker command failed") != std::string::npos)
        {
          hasLinkerFailure = true;
        }
      }

      if (!hasUndefinedReference && !hasLinkerFailure)
        return false;

      print_header("link error: undefined reference");

      print_hint_at_bottom(
          "a symbol is declared but not linked; check missing .cpp files or libraries",
          !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

      print_excerpt(buildLog);

      return true;
    }

    std::vector<std::unique_ptr<runtime::IRuntimeErrorRule>> make_runtime_rules()
    {
      std::vector<std::unique_ptr<runtime::IRuntimeErrorRule>> rules;

      rules.push_back(runtime::makeThreadJoinableRule());
      rules.push_back(runtime::makeDataRaceRule());
      rules.push_back(runtime::makeDeadlockRule());
      rules.push_back(runtime::makeConditionVariableMisuseRule());
      rules.push_back(runtime::makeMutexMisuseRule());
      rules.push_back(runtime::makeFuturePromiseRule());
      rules.push_back(runtime::makeThreadCreationFailureRule());
      rules.push_back(runtime::makeDetachedThreadLifetimeRule());
      rules.push_back(runtime::makeEmptyContainerFrontBackRule());
      rules.push_back(runtime::makeOutOfRangeAccessRule());
      rules.push_back(runtime::makeInvalidIteratorDereferenceRule());
      rules.push_back(runtime::makeIteratorInvalidationRule());
      rules.push_back(runtime::makeStringViewDanglingRuntimeRule());
      rules.push_back(runtime::makeSpanLifetimeRule());

      // Memory safety
      rules.push_back(runtime::makeDoubleFreeRule());
      rules.push_back(runtime::makeInvalidFreeRule());
      rules.push_back(runtime::makeUseAfterFreeRule());
      rules.push_back(runtime::makeMemoryLeakRule());
      rules.push_back(runtime::makeBufferOverflowRule());
      rules.push_back(runtime::makeStackOverflowRule());

      // Pointer / arithmetic / undefined behavior
      rules.push_back(runtime::makeNullPointerRule());
      rules.push_back(runtime::makeDivisionByZeroRule());
      rules.push_back(runtime::makeIntegerOverflowRule());
      rules.push_back(runtime::makeUninitializedMemoryRule());
      rules.push_back(runtime::makeMisalignedAccessRule());
      rules.push_back(runtime::makeBadVariantAccessRule());
      rules.push_back(runtime::makeInvalidCastRule());
      rules.push_back(runtime::makePureVirtualCallRule());

      // Filesystem / OS / I/O / network
      rules.push_back(runtime::makeResourceNotFoundRule());
      rules.push_back(runtime::makeFilesystemRuntimeRule());
      rules.push_back(runtime::makePermissionDeniedRule());
      rules.push_back(runtime::makeAddressAlreadyInUseRule());
      rules.push_back(runtime::makeBrokenPipeRule());
      rules.push_back(runtime::makeTimeoutRuntimeRule());

      // Data / config parsing
      rules.push_back(runtime::makeJsonParseRuntimeRule());
      rules.push_back(runtime::makeConfigParseRuntimeRule());

      // Generic exception fallback
      rules.push_back(runtime::makeUncaughtExceptionRuntimeRule());

      // Last-resort crash/abort fallbacks
      rules.push_back(runtime::makeSegfaultRule());
      rules.push_back(runtime::makeAbortRule());

      return rules;
    }

    bool handle_runtime_rules(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      const auto rules = make_runtime_rules();

      for (const auto &rule : rules)
      {
        if (rule && rule->match(log, sourceFile))
          return rule->handle(log, sourceFile);
      }

      return false;
    }

    std::string trim_copy(std::string text)
    {
      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.front())) != 0)
      {
        text.erase(text.begin());
      }

      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.back())) != 0)
      {
        text.pop_back();
      }

      return text;
    }

    std::string strip_runtime_log_prefix(std::string line)
    {
      line = trim_copy(line);

      const std::size_t errorTag = line.find("[error]");
      if (errorTag != std::string::npos)
      {
        line = line.substr(errorTag + std::string("[error]").size());
        return trim_copy(line);
      }

      const std::size_t errorColon = line.find("error:");
      if (errorColon != std::string::npos)
      {
        line = line.substr(errorColon + std::string("error:").size());
        return trim_copy(line);
      }

      return line;
    }

    std::optional<std::string> extract_best_runtime_error_line(
        const std::string &runtimeLog)
    {
      const auto lines = split_lines(runtimeLog);

      for (const auto &rawLine : lines)
      {
        if (icontains(rawLine, "[error]"))
        {
          const std::string line = strip_runtime_log_prefix(rawLine);

          if (!line.empty())
            return line;
        }
      }

      for (const auto &rawLine : lines)
      {
        if (icontains(rawLine, "error:") ||
            icontains(rawLine, "failed") ||
            icontains(rawLine, "not found") ||
            icontains(rawLine, "cannot open") ||
            icontains(rawLine, "could not open"))
        {
          const std::string line = strip_runtime_log_prefix(rawLine);

          if (!line.empty())
            return line;
        }
      }

      return std::nullopt;
    }

    std::string simplify_runtime_error_title(std::string text)
    {
      text = trim_copy(text);

      if (text.empty())
        return "program reported an error";

      if (icontains(text, "asset file not found"))
        return "asset file not found";

      if (icontains(text, "file not found"))
        return "file not found";

      if (icontains(text, "no such file or directory"))
        return "file not found";

      if (icontains(text, "permission denied"))
        return "permission denied";

      if (icontains(text, "failed to load scene"))
        return "failed to load scene";

      if (icontains(text, "load failed"))
        return text;

      if (icontains(text, "failed"))
        return text;

      return text;
    }

    std::string choose_generic_runtime_hint(
        const std::string &runtimeLog,
        const std::string &title)
    {
      if (icontains(title, "asset") ||
          icontains(title, "file not found") ||
          icontains(runtimeLog, "asset file not found") ||
          icontains(runtimeLog, "no such file or directory"))
      {
        return "check the file path, working directory, or required runtime assets";
      }

      if (icontains(title, "permission denied") ||
          icontains(runtimeLog, "permission denied"))
      {
        return "check file permissions and whether the process can access the resource";
      }

      if (icontains(title, "failed to load") ||
          icontains(title, "load failed"))
      {
        return "check the resource path, configuration, and initialization order";
      }

      if (log_looks_sanitized(runtimeLog) ||
          icontains(runtimeLog, "SIGSEGV") ||
          icontains(runtimeLog, "SIGABRT") ||
          icontains(runtimeLog, "Segmentation fault") ||
          icontains(runtimeLog, "Aborted") ||
          icontains(runtimeLog, "terminate called"))
      {
        return "inspect the runtime log or rerun with --san when possible";
      }

      return "inspect the error lines in the runtime log";
    }

    bool handleGenericRuntimeFallback(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile)
    {
      const auto extracted = extract_best_runtime_error_line(runtimeLog);

      const std::string title =
          simplify_runtime_error_title(
              extracted.value_or("program reported an error"));

      const std::string hint =
          choose_generic_runtime_hint(runtimeLog, title);

      std::cerr << RED
                << "runtime error: "
                << title
                << RESET << "\n";

      print_hint_at_bottom(
          hint,
          !sourceFile.empty() ? "source: " + sourceFile.filename().string() : "");

      if (!runtimeLog.empty())
        print_excerpt(runtimeLog);

      return true;
    }

    bool handle_runtime_anything(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      if (handleRuntimePortAlreadyInUse(log, sourceFile))
        return true;

      if (handleUBSanRuntimeError(log, sourceFile))
        return true;

      if (handleRuntimeAllocDeallocMismatch(log, sourceFile))
        return true;

      if (handleRuntimeDoubleFree(log, sourceFile))
        return true;

      if (handleRuntimeInvalidFree(log, sourceFile))
        return true;

      if (handleRuntimeHeapUseAfterFree(log, sourceFile))
        return true;

      if (handleRuntimeUseAfterReturn(log, sourceFile))
        return true;

      if (handleRuntimeStackUseAfterScope(log, sourceFile))
        return true;

      if (handleRuntimeBufferOverflow(log, sourceFile))
        return true;

      if (handleRuntimeAssertionFailed(log, sourceFile))
        return true;

      if (handleRuntimeBadAllocOOM(log, sourceFile))
        return true;

      if (handle_runtime_rules(log, sourceFile))
        return true;

      if (rules::handleUncaughtException(log, sourceFile))
        return true;

      if (handleLeakSanitizer(log, sourceFile))
        return true;

      if (handleThreadSanitizer(log, sourceFile))
        return true;

      if (handleMemorySanitizer(log, sourceFile))
        return true;

      if (handleGenericSanitizerBanner(log, sourceFile))
        return true;

      return handleGenericRuntimeFallback(log, sourceFile);
    }
  } // namespace

  bool RawLogDetectors::handleKnownRunFailure(
      const std::string &log,
      const std::filesystem::path &ctx)
  {
    return handle_runtime_anything(log, ctx);
  }

  bool RawLogDetectors::handleRuntimeCrash(
      const std::string &runtimeLog,
      const std::filesystem::path &sourceFile,
      [[maybe_unused]] const std::string &contextMessage)
  {
    return handle_runtime_anything(runtimeLog, sourceFile);
  }

  bool RawLogDetectors::handleLinkerOrSanitizer(
      const std::string &buildLog,
      const std::filesystem::path &sourceFile,
      [[maybe_unused]] const std::string &contextMessage)
  {
    if (handleLinkerErrors(buildLog, sourceFile))
      return true;

    const bool looksRuntime =
        icontains(buildLog, "AddressSanitizer") ||
        icontains(buildLog, "UndefinedBehaviorSanitizer") ||
        icontains(buildLog, "LeakSanitizer") ||
        icontains(buildLog, "ThreadSanitizer") ||
        icontains(buildLog, "MemorySanitizer") ||
        icontains(buildLog, "runtime error:") ||
        icontains(buildLog, "Segmentation fault") ||
        icontains(buildLog, "SIGSEGV") ||
        icontains(buildLog, "SIGABRT") ||
        icontains(buildLog, "Aborted") ||
        icontains(buildLog, "terminate called after") ||
        icontains(buildLog, "what():");

    if (looksRuntime)
      return handle_runtime_anything(buildLog, sourceFile);

    return false;
  }
} // namespace vix::cli::errors
