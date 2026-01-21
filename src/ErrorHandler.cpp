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

  static bool hints_verbose_enabled() noexcept
  {
    const char *lvl = std::getenv("VIX_LOG_LEVEL");
    if (!lvl || !*lvl)
      return false;

    return std::strcmp(lvl, "debug") == 0 ||
           std::strcmp(lvl, "trace") == 0;
  }

  static void printHints(const vix::cli::errors::CompilerError &err)
  {
    const std::string &msg = err.message;
    const bool verbose = hints_verbose_enabled();

    auto printHintHeader = []()
    {
      std::cerr << "\n"
                << YELLOW << "Hint:" << RESET << " ";
    };

    if (msg.find("use of undeclared identifier 'std'") != std::string::npos)
    {
      printHintHeader();
      std::cerr << "The C++ standard library namespace `std` is not visible here.\n"
                << GRAY
                << "Fix:\n"
                << "  #include <iostream>\n"
                << RESET;
      return;
    }

    if (msg.find("expected ';'") != std::string::npos)
    {
      printHintHeader();
      std::cerr << "A ';' is missing at this location.\n"
                << GRAY
                << "Check the previous line.\n"
                << RESET;
      return;
    }

    if (msg.find("no matching function for call to") != std::string::npos)
    {
      const bool isVixJson =
          (msg.find("vix::vhttp::ResponseWrapper::json") != std::string::npos) ||
          (msg.find("ResponseWrapper::json") != std::string::npos);

      if (isVixJson)
      {
        printHintHeader();
        std::cerr
            << "Response::json() expects ONE JSON object. You passed (key, value).\n"
            << GRAY
            << "Did you mean:\n"
            << "  res.json({\"message\", \"Hello, world\"});\n"
            << RESET;

        if (verbose)
        {
          std::cerr
              << GRAY
              << "\nOther valid forms:\n"
              << "  res.json({ vix::json::kv(\"message\", \"Hello, world\") });\n"
              << "  return vix::json::o(\"message\", \"Hello, world\");\n"
              << "\nWhy:\n"
              << "  json() accepts a JSON container/value (Vix tokens/builders), not two separate strings.\n"
              << RESET;
        }
        return;
      }

      printHintHeader();
      std::cerr << "The function call does not match any known overload.\n"
                << GRAY
                << "Check argument types and qualifiers.\n"
                << RESET;
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

    auto errors = ClangGccParser::parse(buildLog);

    if (errors.empty())
    {
      if (RawLogDetectors::handleLinkerOrSanitizer(buildLog, sourceFile, contextMessage))
      {
        return;
      }

      error(contextMessage + " (see compiler output below):");
      std::cerr << buildLog << "\n";
      return;
    }

    ErrorContext ctx{sourceFile, contextMessage, buildLog};
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
            buildLog};

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
