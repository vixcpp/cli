/**
 *
 *  @file CMakeBuildErrors.cpp
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
#include <vix/cli/errors/build/CMakeBuildErrors.hpp>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>
#include <iostream>
#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::build
{
  namespace
  {
    bool handle_cache_mismatch(std::string_view log)
    {
      const bool cacheDirMismatch =
          log.find("The current CMakeCache.txt directory") != std::string_view::npos;
      const bool sourceMismatch =
          log.find("does not match the source") != std::string_view::npos &&
          log.find("used to generate cache") != std::string_view::npos;

      if (!cacheDirMismatch && !sourceMismatch)
        return false;

      error("CMake configure failed: stale build cache detected.");
      hint("Your build directory was generated from another project location.");
      hint("Remove the old build directory and reconfigure.");
      hint("Recommended: vix build --clean");
      hint("Manual fix: rm -rf build-ninja build-dev build-release");
      return true;
    }

    bool handle_missing_link_target(std::string_view log)
    {
      const bool hasMissingTarget =
          log.find("but the target was not found") != std::string_view::npos &&
          log.find("target_link_libraries") != std::string_view::npos;

      if (!hasMissingTarget)
        return false;

      std::string targetName;
      std::string missingLink;

      {
        const std::regex reTarget(R"re(Target\s+"([^"]+)")re");
        std::match_results<std::string_view::const_iterator> m;

        if (std::regex_search(log.begin(), log.end(), m, reTarget) && m.size() >= 2)
          targetName = m[1].str();
      }

      {
        const std::regex reMissing(R"re(links\s+to:\s*\n\s*\n\s*([^\s][^\n]*))re");
        std::match_results<std::string_view::const_iterator> m;

        if (std::regex_search(log.begin(), log.end(), m, reMissing) && m.size() >= 2)
          missingLink = m[1].str();
      }

      error("Build failed: unresolved CMake target.");

      if (!targetName.empty())
        std::cerr << "    target: " << targetName << "\n";

      if (!missingLink.empty())
        std::cerr << "    missing: " << RED << missingLink << RESET << "\n";

      std::cerr << "\n";
      hint("Fix the target name or make sure it is defined before linking.");

      return true;
    }

  } // namespace

  bool handleCMakeBuildError(std::string_view log)
  {
    if (handle_cache_mismatch(log))
      return true;
    if (handle_missing_link_target(log))
      return true;
    return false;
  }

} // namespace vix::cli::errors::build
