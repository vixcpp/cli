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
#include "vix/cli/ErrorHandler.hpp"

#include "vix/cli/errors/ClangGccParser.hpp"
#include "vix/cli/errors/CompilerError.hpp"
#include "vix/cli/errors/ErrorContext.hpp"
#include "vix/cli/errors/ErrorPipeline.hpp"
#include "vix/cli/errors/RawLogDetectors.hpp"
#include "vix/cli/errors/CodeFrame.hpp"
#include <vix/utils/Env.hpp>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstring>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace
{
  static void printSingleError(const vix::cli::errors::CompilerError &err)
  {
    std::ostringstream oss;
    oss << err.file << ":" << err.line << ":" << err.column << "\n";
    oss << "  error: " << err.message << "\n";
    error(oss.str());
  }

  static bool handle_unrecognized_cli_option_as_script_runtime_args(std::string_view log)
  {
    // Exemple:
    // c++: error: unrecognized command-line option ‘--config’; did you mean ...
    const std::size_t p = log.find("unrecognized command-line option");
    if (p == std::string_view::npos)
      return false;

    std::string opt;
    {
      std::size_t q1 = log.find("‘", p);
      std::size_t q2 = std::string_view::npos;
      if (q1 != std::string_view::npos)
        q2 = log.find("’", q1 + 1);

      if (q1 != std::string_view::npos && q2 != std::string_view::npos && q2 > q1 + 1)
        opt = std::string(log.substr(q1 + 1, q2 - (q1 + 1)));
    }

    if (opt.size() >= 3 && opt.rfind("--", 0) == 0)
    {
      error("Script build failed: you passed runtime args as compiler flags.");
      hint("In .cpp script mode, everything after `--` is treated as compiler/linker flags.");
      hint("Use repeatable --args for runtime arguments.");
      if (!opt.empty())
      {
        std::cerr << GRAY << "example: vix run main.cpp --args " << opt << " --args <value>\n"
                  << RESET;
      }
      return true;
    }

    return false;
  }

  static bool hints_verbose_enabled() noexcept
  {
    const char *lvl = vix::utils::vix_getenv("VIX_LOG_LEVEL");
    if (!lvl || !*lvl)
      return false;

    return std::strcmp(lvl, "debug") == 0 ||
           std::strcmp(lvl, "trace") == 0;
  }

  static std::size_t line_start(std::string_view s, std::size_t pos)
  {
    if (pos == std::string_view::npos)
      return std::string_view::npos;
    while (pos > 0 && s[pos - 1] != '\n')
      --pos;
    return pos;
  }

  static std::size_t find_first_error_anchor(std::string_view log)
  {
    static constexpr std::string_view anchors[] = {
        "FAILED:",               // ninja
        "ninja: build stopped:", // ninja
        "error:",                // gcc/clang
        "fatal error:",          // gcc/clang
        "CMake Error",           // cmake configure
        "make: ***"              // make
    };

    std::size_t best = std::string_view::npos;

    for (auto a : anchors)
    {
      std::size_t p = log.find(a);
      if (p == std::string_view::npos)
        continue;

      std::size_t ls = line_start(log, p);
      if (ls == std::string_view::npos)
        ls = 0;

      if (best == std::string_view::npos || ls < best)
        best = ls;
    }

    if (best == std::string_view::npos)
    {
      std::size_t p = log.find(": error:");
      if (p != std::string_view::npos)
      {
        std::size_t ls = line_start(log, p);
        best = (ls == std::string_view::npos) ? 0 : ls;
      }
    }

    return best;
  }

  static std::string trim_build_preamble(const std::string &log)
  {
    std::string_view v(log);
    const std::size_t start = find_first_error_anchor(v);
    if (start == std::string_view::npos)
      return log;
    return std::string(v.substr(start));
  }

  static void printHints(const vix::cli::errors::CompilerError &err)
  {
    const std::string &msg = err.message;
    const bool verbose = hints_verbose_enabled();

    auto h = [](const std::string &s)
    {
      std::cerr << "\n"
                << YELLOW << "hint: " << RESET << s << "\n";
    };

    if (msg.find("use of undeclared identifier 'std'") != std::string::npos)
    {
      h("std is not visible here (include the required standard header)");
      if (verbose)
        std::cerr << GRAY << "e.g. #include <iostream>\n"
                  << RESET;
      return;
    }

    if (msg.find("expected ';'") != std::string::npos)
    {
      h("missing ';' (often the previous line)");
      return;
    }

    if (msg.find("no matching function for call to") != std::string::npos)
    {
      const bool isVixJson =
          (msg.find("vix::vhttp::ResponseWrapper::json") != std::string::npos) ||
          (msg.find("ResponseWrapper::json") != std::string::npos);

      if (isVixJson)
      {
        h("Response::json() expects one JSON value (not key,value)");
        if (verbose)
          std::cerr << GRAY << "e.g. res.json({\"message\", \"Hello\"});\n"
                    << RESET;
        return;
      }

      h("no matching overload (check argument types and qualifiers)");
      return;
    }
  }

} // namespace

namespace vix::cli
{
  void ErrorHandler::printBuildErrors(
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
      {
        std::cerr << "\n"
                  << GRAY << "compiler output:\n"
                  << RESET;
        std::cerr << cleanedLog << "\n";
        return;
      }

      if (RawLogDetectors::handleLinkerOrSanitizer(cleanedLog, sourceFile, contextMessage))
        return;

      error(contextMessage + " (see compiler output below):");
      std::cerr << cleanedLog << "\n";
      return;
    }

    ErrorContext ctx{sourceFile, contextMessage, cleanedLog};
    ErrorPipeline pipeline;

    if (pipeline.tryHandle(errors, ctx))
    {
      return;
    }

    std::unordered_map<std::string, int> counts;
    for (const auto &e : errors)
    {
      std::string key = e.file + "|" + e.message;
      counts[key]++;
    }

    std::vector<CompilerError> unique;
    unique.reserve(errors.size());

    std::unordered_set<std::string> seen;
    for (const auto &e : errors)
    {
      std::string key = e.file + "|" + e.message;
      if (seen.insert(key).second)
      {
        unique.push_back(e);
      }
    }

    if (unique.empty())
    {
      error(contextMessage + " (no unique errors found, see compiler output below):");
      std::cerr << buildLog << "\n";
      return;
    }

    // Generic build error header
    error(contextMessage + ":");

    CodeFrameOptions cf;
    cf.contextLines = 2;
    cf.maxLineWidth = 120;
    cf.tabWidth = 4;

    std::size_t maxToShow = std::min<std::size_t>(unique.size(), 3);

    for (std::size_t i = 0; i < maxToShow; ++i)
    {
      const auto &err = unique[i];

      std::cerr << "\n";

      ::printSingleError(err);

      ::printHints(err);

      {
        ErrorContext frameCtx{
            err.file,
            contextMessage,
            cleanedLog};

        printCodeFrame(err, frameCtx, cf);
      }

      const std::string key = err.file + "|" + err.message;
      auto it = counts.find(key);
      if (it != counts.end() && it->second > 1)
      {
        std::cerr << GRAY
                  << "\n  (" << (it->second - 1)
                  << " similar error(s) hidden)\n"
                  << RESET;
      }
    }

    if (unique.size() > maxToShow)
    {
      std::cerr << "\n… " << (unique.size() - maxToShow)
                << " more distinct errors hidden. Run the build manually for full output.\n";
    }

    std::cerr << "\nSource file: " << sourceFile << "\n";
  }
} // namespace vix::cli
