/**
 * @file Project.hpp
 * @brief Normalized internal project identity contract.
 */
#ifndef VIX_CLI_PROJECT_PROJECT_HPP
#define VIX_CLI_PROJECT_PROJECT_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/project/Target.hpp>
#include <vix/cli/project/Test.hpp>

namespace vix::cli::project
{
  // The legacy CLI is built around std::filesystem and is not linked to the
  // independently installable Files domain.  Keep one explicit local alias
  // until that layering is reconciled; do not introduce another path type.
  using Path = std::filesystem::path;

  struct Project
  {
    Path root;
    std::string name;
    std::vector<Target> targets;
    std::vector<Test> tests;
  };
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_PROJECT_HPP
