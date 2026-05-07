/**
 *
 *  @file BuildGraphExecutor.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Target-aware build graph executor
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_HPP
#define VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/build/BuildGraph.hpp>
#include <vix/cli/build/BuildScheduler.hpp>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  struct BuildGraphExecutorOptions
  {
    fs::path buildDir;
    std::string target;

    int jobs{0};
    bool quiet{false};
    bool verbose{false};
  };

  struct BuildGraphExecutorResult
  {
    bool ok{false};

    std::string target;
    std::size_t selectedTasks{0};
    std::size_t selectedCompileTasks{0};
    std::size_t dirtyCompileTasks{0};
    std::size_t executedCompileTasks{0};
    std::size_t skippedCompileTasks{0};

    int exitCode{0};
    std::string output;
  };

  class BuildGraphExecutor
  {
  public:
    explicit BuildGraphExecutor(BuildGraphExecutorOptions options);

    const BuildGraphExecutorOptions &options() const;

    BuildGraphExecutorResult run_target(BuildGraph &graph) const;

  private:
    BuildGraphExecutorOptions options_;
  };

} // namespace vix::cli::build

#endif
