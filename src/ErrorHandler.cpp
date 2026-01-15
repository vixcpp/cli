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

#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

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

  static void printHints(const vix::cli::errors::CompilerError &err)
  {
    const std::string &msg = err.message;

    auto printHintHeader = []()
    {
      std::cerr << "\n"
                << YELLOW << "Hint:" << RESET << " ";
    };

    if (msg.find("use of undeclared identifier 'std'") != std::string::npos)
    {
      printHintHeader();
      std::cerr << "It looks like the C++ standard library namespace `std` is not visible here.\n"
                << GRAY
                << "  • Did you forget to include <iostream> ?\n"
                << "  • Or to use 'std::cout' / 'std::endl' correctly ?\n"
                << RESET;
    }
    else if (msg.find("expected ';'") != std::string::npos)
    {
      printHintHeader();
      std::cerr << "The compiler expected a ';' at this location.\n"
                << GRAY
                << "  • Check the previous line for a missing semicolon.\n"
                << RESET;
    }
    else if (msg.find("no matching function for call to") != std::string::npos)
    {
      printHintHeader();
      std::cerr << "The function you are calling does not match any known overload.\n"
                << GRAY
                << "  • Check parameter types and const/reference qualifiers.\n"
                << RESET;
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

    std::size_t maxToShow = std::min<std::size_t>(unique.size(), 3);
    for (std::size_t i = 0; i < maxToShow; ++i)
    {
      const auto &err = unique[i];

      std::cerr << "\n";
      ::printSingleError(err);
      ::printHints(err);

      std::string key = err.file + "|" + err.message;
      auto it = counts.find(key);
      if (it != counts.end() && it->second > 1)
      {
        std::cerr << "\n  (" << (it->second - 1)
                  << " similar error(s) with the same message hidden)\n";
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
