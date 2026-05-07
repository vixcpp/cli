/**
 *
 *  @file ErrorHandler.cpp
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
#include <vix/cli/ErrorHandler.hpp>

#include <vix/cli/errors/ClangGccParser.hpp>
#include <vix/cli/errors/CodeFrame.hpp>
#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/ErrorContext.hpp>
#include <vix/cli/errors/ErrorPipeline.hpp>
#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/errors/build/BuildErrorDetectors.hpp>
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fstream>

#include <vix/cli/Style.hpp>
#include <vix/cli/build/BuildStyle.hpp>

using namespace vix::cli::style;

namespace
{
  namespace fs = std::filesystem;

  void print_hint(std::string_view text)
  {
    if (text.empty())
      return;

    std::cerr << YELLOW
              << "hint: "
              << RESET
              << text
              << "\n";
  }

  vix::cli::build::BuildDiagnostic make_build_diagnostic(
      const vix::cli::errors::CompilerError &err,
      const std::string &contextMessage)
  {
    vix::cli::build::BuildDiagnostic diagnostic;

    diagnostic.title = contextMessage.empty()
                           ? "Build failed"
                           : contextMessage;

    diagnostic.error = err.message;

    diagnostic.location.file = err.file;
    diagnostic.location.line = static_cast<std::size_t>(err.line);
    diagnostic.location.column = static_cast<std::size_t>(err.column);

    return diagnostic;
  }

  std::vector<std::string> read_code_frame_lines(
      const fs::path &file,
      std::size_t line,
      std::size_t contextLines,
      std::size_t maxLineWidth)
  {
    std::vector<std::string> out;

    if (file.empty() || line == 0)
      return out;

    std::ifstream in(file);
    if (!in)
      return out;

    const std::size_t startLine =
        line > contextLines ? line - contextLines : 1;

    const std::size_t endLine = line + contextLines;

    std::string current;
    std::size_t currentLine = 0;

    while (std::getline(in, current))
    {
      ++currentLine;

      if (currentLine < startLine)
        continue;

      if (currentLine > endLine)
        break;

      if (maxLineWidth > 0 && current.size() > maxLineWidth)
      {
        current = current.substr(0, maxLineWidth);
        current += "...";
      }

      out.push_back(current);
    }

    return out;
  }

  std::string hint_for_compiler_error(
      const vix::cli::errors::CompilerError &err)
  {
    const std::string &message = err.message;

    if (message.find("use of undeclared identifier 'std'") != std::string::npos)
      return "Include the required standard header before using std names.";

    if (message.find("expected ';'") != std::string::npos)
      return "Add the missing semicolon, often on the previous line.";

    if (message.find("no matching function for call to") != std::string::npos)
    {
      const bool isVixJson =
          message.find("vix::http::ResponseWrapper::json") != std::string::npos ||
          message.find("ResponseWrapper::json") != std::string::npos;

      if (isVixJson)
        return "Response::json() expects one JSON value, not key/value arguments.";

      return "Check argument types, overloads, const qualifiers, and references.";
    }

    if (message.find("was not declared in this scope") != std::string::npos)
      return "Declare the symbol before use, include the right header, or move the function definition above the call.";

    if (message.find("defined but not used") != std::string::npos)
      return "Remove the unused function or mark it intentionally unused.";

    return {};
  }

  vix::cli::build::BuildDiagnostic diagnostic_from_compiler_error(
      const vix::cli::errors::CompilerError &err,
      const std::string &contextMessage)
  {
    vix::cli::build::BuildDiagnostic diagnostic;

    diagnostic.title =
        contextMessage.empty()
            ? "Build failed"
            : contextMessage;

    diagnostic.error = err.message;
    diagnostic.hint = hint_for_compiler_error(err);

    diagnostic.location.file = err.file;
    diagnostic.location.line =
        err.line > 0 ? static_cast<std::size_t>(err.line) : 0;
    diagnostic.location.column =
        err.column > 0 ? static_cast<std::size_t>(err.column) : 0;

    diagnostic.codeFrame.location = diagnostic.location;
    diagnostic.codeFrame.lines =
        read_code_frame_lines(
            fs::path(err.file),
            diagnostic.location.line,
            2,
            120);

    return diagnostic;
  }

  bool handle_unrecognized_cli_option_as_script_runtime_args(
      std::string_view log)
  {
    const std::size_t pos =
        log.find("unrecognized command-line option");

    if (pos == std::string_view::npos)
      return false;

    std::string option;

    const std::size_t openQuote = log.find("‘", pos);
    std::size_t closeQuote = std::string_view::npos;

    if (openQuote != std::string_view::npos)
      closeQuote = log.find("’", openQuote + 1);

    if (openQuote != std::string_view::npos &&
        closeQuote != std::string_view::npos &&
        closeQuote > openQuote + 1)
    {
      option =
          std::string(log.substr(openQuote + 1, closeQuote - (openQuote + 1)));
    }

    if (option.size() < 3 || option.rfind("--", 0) != 0)
      return false;

    std::cerr << RED
              << "error: runtime argument passed as compiler flag"
              << RESET << "\n";

    print_hint("use --args for runtime arguments in .cpp script mode");

    std::cerr << GREEN
              << "at: "
              << RESET
              << "vix run <file.cpp> --args " << option << "\n";

    return true;
  }

  bool hints_verbose_enabled() noexcept
  {
    const char *level = vix::utils::vix_getenv("VIX_LOG_LEVEL");

    if (!level || !*level)
      return false;

    return std::strcmp(level, "debug") == 0 ||
           std::strcmp(level, "trace") == 0;
  }

  std::size_t line_start(std::string_view text, std::size_t pos)
  {
    if (pos == std::string_view::npos)
      return std::string_view::npos;

    while (pos > 0 && text[pos - 1] != '\n')
      --pos;

    return pos;
  }

  std::size_t find_first_error_anchor(std::string_view log)
  {
    static constexpr std::string_view anchors[] = {
        "FAILED:",
        "ninja: build stopped:",
        "fatal error:",
        "error:",
        "CMake Error",
        "make: ***",
    };

    std::size_t best = std::string_view::npos;

    for (const auto anchor : anchors)
    {
      const std::size_t pos = log.find(anchor);

      if (pos == std::string_view::npos)
        continue;

      std::size_t start = line_start(log, pos);

      if (start == std::string_view::npos)
        start = 0;

      if (best == std::string_view::npos || start < best)
        best = start;
    }

    if (best == std::string_view::npos)
    {
      const std::size_t pos = log.find(": error:");

      if (pos != std::string_view::npos)
      {
        const std::size_t start = line_start(log, pos);
        best = start == std::string_view::npos ? 0 : start;
      }
    }

    return best;
  }

  std::string trim_build_preamble(const std::string &log)
  {
    const std::string_view view(log);
    const std::size_t start = find_first_error_anchor(view);

    if (start == std::string_view::npos)
      return log;

    return std::string(view.substr(start));
  }
} // namespace

namespace vix::cli
{
  bool ErrorHandler::printBuildErrors(
      const std::string &buildLog,
      const fs::path &sourceFile,
      const std::string &contextMessage)
  {
    using namespace vix::cli::errors;

    const std::string cleanedLog = ::trim_build_preamble(buildLog);
    auto errors = ClangGccParser::parse(cleanedLog);

    if (errors.empty())
    {
      if (handle_unrecognized_cli_option_as_script_runtime_args(cleanedLog))
        return true;

      if (vix::cli::errors::build::handleBuildErrors(cleanedLog))
        return true;

      if (RawLogDetectors::handleLinkerOrSanitizer(
              cleanedLog,
              sourceFile,
              contextMessage))
      {
        return true;
      }

      vix::cli::build::print_build_error(
          std::cerr,
          contextMessage.empty() ? "Build failed" : contextMessage);

      print_hint("run with --verbose to inspect the full build output");

      if (!cleanedLog.empty())
      {
        std::cerr << "\n"
                  << GRAY
                  << "compiler output:"
                  << RESET
                  << "\n";

        std::cerr << cleanedLog;

        if (cleanedLog.back() != '\n')
          std::cerr << "\n";
      }

      return false;
    }

    ErrorContext ctx{sourceFile, contextMessage, cleanedLog};
    ErrorPipeline pipeline;

    if (pipeline.tryHandle(errors, ctx))
      return true;

    std::unordered_map<std::string, int> counts;
    counts.reserve(errors.size());

    for (const auto &err : errors)
    {
      const std::string key = err.file + "|" + err.message;
      ++counts[key];
    }

    std::vector<CompilerError> unique;
    unique.reserve(errors.size());

    std::unordered_set<std::string> seen;
    seen.reserve(errors.size());

    for (const auto &err : errors)
    {
      const std::string key = err.file + "|" + err.message;

      if (seen.insert(key).second)
        unique.push_back(err);
    }

    if (unique.empty())
    {
      std::cerr << RED
                << "error: "
                << RESET
                << contextMessage
                << "\n";

      print_hint("no unique compiler error was detected; inspect the raw compiler output");

      if (!cleanedLog.empty())
        std::cerr << cleanedLog << "\n";

      return false;
    }

    std::cerr << RED
              << "error: "
              << RESET
              << contextMessage
              << "\n";

    CodeFrameOptions codeFrameOptions;
    codeFrameOptions.contextLines = 2;
    codeFrameOptions.maxLineWidth = 120;
    codeFrameOptions.tabWidth = 4;

    const std::size_t maxToShow =
        std::min<std::size_t>(unique.size(), 3);

    for (std::size_t i = 0; i < maxToShow; ++i)
    {
      const auto &err = unique[i];

      const vix::cli::build::BuildDiagnostic diagnostic =
          ::diagnostic_from_compiler_error(
              err,
              contextMessage);

      vix::cli::build::print_build_diagnostic(
          std::cerr,
          diagnostic);

      const std::string key = err.file + "|" + err.message;
      const auto it = counts.find(key);

      if (it != counts.end() && it->second > 1)
      {
        vix::cli::build::print_build_warning(
            std::cerr,
            std::to_string(it->second - 1) + " similar error(s) hidden");
      }
    }

    if (unique.size() > maxToShow)
    {
      std::cerr << "\n"
                << GRAY
                << (unique.size() - maxToShow)
                << " more distinct error(s) hidden. Run with --verbose for full output."
                << RESET
                << "\n";
    }

    if (!sourceFile.empty())
    {
      vix::cli::build::print_build_info(
          std::cerr,
          "at: " + sourceFile.string());
    }

    return true;
  }
} // namespace vix::cli
