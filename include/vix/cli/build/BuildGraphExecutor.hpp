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
 *  Production-safe target-aware build graph executor
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_HPP
#define VIX_CLI_BUILD_BUILD_GRAPH_EXECUTOR_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/build/BuildGraph.hpp>
#include <vix/cli/build/BuildScheduler.hpp>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  enum class BuildGraphExecutorStatus
  {
    Success,
    UpToDate,
    DelegatedToNinja,
    InvalidRequest,
    InvalidGraph,
    UnsupportedTarget,
    CompileFailed,
    NinjaFailed,
    CacheFailed
  };

  inline const char *to_string(BuildGraphExecutorStatus status)
  {
    switch (status)
    {
    case BuildGraphExecutorStatus::Success:
      return "success";
    case BuildGraphExecutorStatus::UpToDate:
      return "up-to-date";
    case BuildGraphExecutorStatus::DelegatedToNinja:
      return "delegated-to-ninja";
    case BuildGraphExecutorStatus::InvalidRequest:
      return "invalid-request";
    case BuildGraphExecutorStatus::InvalidGraph:
      return "invalid-graph";
    case BuildGraphExecutorStatus::UnsupportedTarget:
      return "unsupported-target";
    case BuildGraphExecutorStatus::CompileFailed:
      return "compile-failed";
    case BuildGraphExecutorStatus::NinjaFailed:
      return "ninja-failed";
    case BuildGraphExecutorStatus::CacheFailed:
      return "cache-failed";
    default:
      return "unknown";
    }
  }

  struct BuildGraphExecutorOptions
  {
    fs::path buildDir;
    std::string target;

    int jobs{0};
    bool quiet{false};
    bool verbose{false};

    /*
     * Production rule:
     * The graph executor is an optimization layer.
     * Ninja remains the source of truth when the graph is incomplete,
     * ambiguous, unsupported or too risky.
     */
    bool allowNinjaFallback{true};

    /*
     * 0 means no artificial limit.
     * Production builds should not fail just because many files are dirty.
     * Large dirty sets can be delegated to Ninja instead.
     */
    std::size_t maxGraphDirtyCompileTasks{0};
  };

  struct BuildGraphExecutorResult
  {
    bool ok{false};

    BuildGraphExecutorStatus status{BuildGraphExecutorStatus::InvalidRequest};

    std::string target;
    std::string reason;

    bool usedGraph{false};
    bool usedNinja{false};
    bool usedFallback{false};

    std::size_t selectedTasks{0};
    std::size_t selectedCompileTasks{0};
    std::size_t dirtyCompileTasks{0};
    std::size_t executedCompileTasks{0};
    std::size_t skippedCompileTasks{0};

    int exitCode{0};
    std::string output;

    bool success() const
    {
      return ok && exitCode == 0;
    }
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
