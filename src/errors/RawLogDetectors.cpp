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
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

    bool runtime_technical_details_enabled()
    {
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (level == nullptr || *level == '\0')
        return false;

      std::string value(level);

      for (char &character : value)
      {
        character = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(character)));
      }

      return value == "debug" || value == "trace";
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

    void print_codeframe_then_bottom_default(
        const CompilerError &location,
        std::string_view hint)
    {
      ErrorContext context;

      CodeFrameOptions options;
      options.contextLines = 2;
      options.maxLineWidth = 120;
      options.tabWidth = 4;

      printCodeFrame(location, context, options);

      print_hint_at_bottom(
          hint,
          location.file + ":" + std::to_string(location.line));
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

    std::string trim_linker_text(std::string text)
    {
      while (!text.empty() &&
             std::isspace(
                 static_cast<unsigned char>(text.front())) != 0)
      {
        text.erase(text.begin());
      }

      while (!text.empty() &&
             std::isspace(
                 static_cast<unsigned char>(text.back())) != 0)
      {
        text.pop_back();
      }

      return text;
    }

    std::string strip_linker_quotes(std::string text)
    {
      text = trim_linker_text(std::move(text));

      if (!text.empty() &&
          (text.front() == '`' ||
           text.front() == '\'' ||
           text.front() == '"'))
      {
        text.erase(text.begin());
      }

      while (!text.empty() &&
             (text.back() == '\'' ||
              text.back() == '`' ||
              text.back() == '"' ||
              text.back() == ':'))
      {
        text.pop_back();
      }

      return trim_linker_text(std::move(text));
    }

    std::optional<std::string>
    try_extract_undefined_symbol(
        const std::string &buildLog)
    {
      static constexpr std::string_view markers[] = {
          "undefined reference to",
          "undefined symbol:",
      };

      std::istringstream input(buildLog);
      std::string line;

      while (std::getline(input, line))
      {
        for (const std::string_view marker : markers)
        {
          const std::size_t pos =
              line.find(marker);

          if (pos == std::string::npos)
            continue;

          std::string symbol =
              line.substr(pos + marker.size());

          symbol =
              strip_linker_quotes(
                  std::move(symbol));

          if (!symbol.empty())
            return symbol;
        }
      }

      return std::nullopt;
    }

    std::string linker_symbol_search_name(
        const std::string &symbol)
    {
      std::string name = symbol;

      /*
       * vtable/typeinfo diagnostics do not map reliably to one
       * explicit source expression.
       */
      if (name.rfind("vtable for ", 0) == 0 ||
          name.rfind("typeinfo for ", 0) == 0 ||
          name.rfind("typeinfo name for ", 0) == 0)
      {
        return {};
      }

      const std::size_t signature =
          name.find('(');

      if (signature != std::string::npos)
        name.resize(signature);

      name = trim_linker_text(
          std::move(name));

      const std::size_t namespacePos =
          name.rfind("::");

      if (namespacePos != std::string::npos)
        name = name.substr(namespacePos + 2);

      return trim_linker_text(
          std::move(name));
    }

    std::optional<CompilerError>
    try_find_linker_symbol_location(
        const std::filesystem::path &sourceFile,
        const std::string &symbol)
    {
      if (sourceFile.empty())
        return std::nullopt;

      const std::string needle =
          linker_symbol_search_name(symbol);

      if (needle.empty())
        return std::nullopt;

      const auto lines =
          read_file_lines(sourceFile);

      if (!lines)
        return std::nullopt;

      std::optional<CompilerError> best;

      /*
       * Keep the last occurrence.
       *
       * In a small standalone script this usually prefers the call
       * over an earlier forward declaration:
       *
       *   int calculate_result(int);
       *   ...
       *   calculate_result(42);
       */
      for (std::size_t i = 0;
           i < lines->size();
           ++i)
      {
        const std::size_t pos =
            (*lines)[i].find(needle);

        if (pos == std::string::npos)
          continue;

        CompilerError location;
        location.file = sourceFile.string();
        location.line =
            static_cast<int>(i + 1);
        location.column =
            static_cast<int>(pos + 1);
        location.message =
            "symbol used here";

        best = std::move(location);
      }

      return best;
    }

    bool linker_technical_details_enabled()
    {
      const char *level =
          std::getenv("VIX_LOG_LEVEL");

      if (level == nullptr || *level == '\0')
        return false;

      std::string value =
          trim_linker_text(level);

      for (char &character : value)
      {
        character =
            static_cast<char>(
                std::tolower(
                    static_cast<unsigned char>(
                        character)));
      }

      return value == "debug" ||
             value == "trace";
    }

    void print_linker_debug_details(
        const std::string &buildLog)
    {
      if (!linker_technical_details_enabled())
        return;

      std::cerr << "\n"
                << GRAY
                << "technical details:"
                << RESET
                << "\n";

      print_excerpt(
          buildLog,
          20);
    }

    bool handleLinkerErrors(
        const std::string &buildLog,
        const std::filesystem::path &sourceFile)
    {
      const std::optional<std::string> symbol =
          try_extract_undefined_symbol(
              buildLog);

      bool hasLinkerFailure = false;

      std::istringstream input(buildLog);
      std::string line;

      while (std::getline(input, line))
      {
        if (line.find("ld returned") != std::string::npos ||
            line.find("collect2: error: ld returned") != std::string::npos ||
            line.find("clang: error: linker command failed") != std::string::npos ||
            line.find("ld.lld: error:") != std::string::npos ||
            line.find("mold: error:") != std::string::npos)
        {
          hasLinkerFailure = true;
        }
      }

      if (!symbol && !hasLinkerFailure)
        return false;

      if (symbol)
      {
        const bool looksLikeFunction =
            symbol->find('(') != std::string::npos;

        print_header(
            looksLikeFunction
                ? "link error: function has no implementation"
                : "link error: missing definition");

        if (looksLikeFunction)
        {
          std::cerr
              << "  The function `"
              << *symbol
              << "` is used by the program, but the linker could not find its function body.\n";
        }
        else
        {
          std::cerr
              << "  The symbol `"
              << *symbol
              << "` is used by the program, but the linker could not find its compiled definition.\n";
        }

        const std::string hint =
            looksLikeFunction
                ? "add the function definition, or link the .cpp file or library that contains it"
                : "add the missing definition, or link the .cpp file or library that provides this symbol";

        const auto location =
            try_find_linker_symbol_location(
                sourceFile,
                *symbol);

        if (location)
        {
          print_codeframe_then_bottom_default(
              *location,
              hint);
        }
        else
        {
          std::cerr << "\n";

          print_hint_at_bottom(
              hint,
              "");
        }

        print_linker_debug_details(
            buildLog);

        return true;
      }

      /*
       * We know the linker failed but could not extract a useful
       * unresolved symbol. Keep this case understandable without
       * pretending we know more than the linker told us.
       */
      print_header(
          "link error: linking failed");

      std::cerr
          << "  Compilation succeeded, but the final executable could not be created.\n";

      std::cerr << "\n";

      print_hint_at_bottom(
          "check that every required .cpp file and library is included in the build",
          "");

      print_linker_debug_details(
          buildLog);

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

      (void)sourceFile;

      print_hint_at_bottom(
          hint,
          "");

      if (runtime_technical_details_enabled() &&
          !runtimeLog.empty())
      {
        print_excerpt(runtimeLog);
      }

      return true;
    }

    bool handle_runtime_anything(
        const std::string &log,
        const std::filesystem::path &sourceFile)
    {
      if (handle_runtime_rules(log, sourceFile))
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
