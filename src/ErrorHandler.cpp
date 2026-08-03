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

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <cstdlib>
#include <cctype>

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

  std::string hint_for_compiler_error(
      const vix::cli::errors::CompilerError &err)
  {
    const std::string &message = err.message;

    if (message.find("No such file or directory") != std::string::npos ||
        message.find("file not found") != std::string::npos)
    {
      return "Check that the header exists and that its directory is listed in include_dirs, target_include_directories, or your compiler include paths.";
    }

    if (message.find("fatal error:") != std::string::npos &&
        message.find("#include") != std::string::npos)
    {
      return "A header could not be found. Check the include path or the header name.";
    }

    if (message.find("use of undeclared identifier 'std'") != std::string::npos)
    {
      return "Include the required standard header before using std names.";
    }

    if (message.find("use of undeclared identifier") != std::string::npos)
    {
      return "Declare the symbol before use, include the right header, or check the namespace.";
    }

    if (message.find("has not been declared") != std::string::npos ||
        message.find("was not declared in this scope") != std::string::npos)
    {
      return "Include the header that declares this name or check its namespace.";
    }

    if (message.find("does not name a type") != std::string::npos)
    {
      return "Check that the type is declared before use and that the correct header is included.";
    }

    if (message.find("expected ';'") != std::string::npos)
    {
      return "Add the missing semicolon, often on the previous line.";
    }

    if (message.find("expected primary-expression") != std::string::npos)
    {
      return "Check the expression syntax near this location, especially missing operators, parentheses, or variables.";
    }

    if (message.find("no matching function for call to") != std::string::npos)
    {
      const bool isVixJson =
          message.find("vix::http::ResponseWrapper::json") != std::string::npos ||
          message.find("ResponseWrapper::json") != std::string::npos;

      if (isVixJson)
        return "Response::json() expects one JSON value, not key/value arguments.";

      return "Check argument types, overloads, const qualifiers, and references.";
    }

    if (message.find("undefined reference to") != std::string::npos)
    {
      return "A symbol is declared but not linked. Check missing .cpp files, libraries, or target_link_libraries.";
    }

    if (message.find("defined but not used") != std::string::npos)
    {
      return "Remove the unused function or mark it intentionally unused.";
    }

    return {};
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
    auto errors = ClangGccParser::parse(buildLog);

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

      const bool debugOutput = []()
      {
        const char *level = std::getenv("VIX_LOG_LEVEL");

        if (!level || !*level)
          return false;

        std::string value(level);

        for (char &c : value)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        return value == "debug" || value == "trace";
      }();

      if (debugOutput && !cleanedLog.empty())
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
      else
      {
        print_hint("run with VIX_LOG_LEVEL=debug to inspect the full compiler output");
      }

      return false;
    }

    ErrorContext ctx{sourceFile, contextMessage, buildLog};
    ErrorPipeline pipeline;

    if (pipeline.tryHandle(errors, ctx))
      return true;

    std::vector<CompilerError> unique;
    unique.reserve(errors.size());

    std::unordered_set<std::string> seen;
    seen.reserve(errors.size());

    for (const auto &err : errors)
    {
      const std::string key =
          err.file +
          "|" +
          std::to_string(err.line) +
          "|" +
          std::to_string(err.column) +
          "|" +
          err.message;

      if (seen.insert(key).second)
        unique.push_back(err);
    }

    if (unique.empty())
    {
      std::cerr << RED
                << "error: "
                << RESET
                << (contextMessage.empty()
                        ? "Build failed"
                        : contextMessage)
                << "\n";

      print_hint(
          "no compiler error could be extracted; "
          "run with VIX_LOG_LEVEL=debug to inspect the full output");

      return false;
    }

    /*
     * Show the first compiler error by default.
     *
     * Compiler errors frequently cascade: once the parser cannot
     * understand one declaration, it may report several secondary
     * errors. Showing the first error keeps the output focused for
     * beginners.
     */
    const std::size_t maxToShow = 1;

    CodeFrameOptions codeFrameOptions;
    codeFrameOptions.contextLines = 1;
    codeFrameOptions.maxLineWidth = 120;
    codeFrameOptions.tabWidth = 4;
    codeFrameOptions.leadingBlankLine = true;

    for (std::size_t i = 0;
         i < unique.size() && i < maxToShow;
         ++i)
    {
      const CompilerError &err = unique[i];

      std::cerr << RED
                << "error: "
                << RESET
                << err.message
                << "\n";

      /*
       * Use the shared codeframe contract directly.
       *
       * This avoids creating a second list of source lines with
       * incorrect line-number assumptions.
       */
      printCodeFrame(
          err,
          ctx,
          codeFrameOptions);

      const std::string hint =
          hint_for_compiler_error(err);

      if (!hint.empty())
        print_hint(hint);
    }

    const std::size_t hiddenCount =
        unique.size() > maxToShow
            ? unique.size() - maxToShow
            : 0;

    if (hiddenCount > 0)
    {
      std::cerr << "\n"
                << GRAY
                << hiddenCount
                << " more compiler error"
                << (hiddenCount == 1 ? "" : "s")
                << " hidden. Run with --verbose to see them."
                << RESET
                << "\n";
    }

    return true;
  }
} // namespace vix::cli
