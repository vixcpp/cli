/**
 *
 *  @file ThreadCreationFailureRule.cpp
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
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>
#include <vix/cli/errors/runtime/RuntimeRuleUtils.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  class ThreadCreationFailureRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "std::system_error") &&
              (icontains(log, "thread") || icontains(log, "resource temporarily unavailable"))) ||
             icontains(log, "resource temporarily unavailable") ||
             icontains(log, "thread constructor failed") ||
             icontains(log, "failed to create thread") ||
             icontains(log, "pthread_create");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: thread creation failed";
      std::vector<std::string> hints = {
          "the program failed to start a new thread",
          "check system thread limits, available memory, stack size, and thread explosion in the design",
      };

      if (icontains(log, "resource temporarily unavailable"))
      {
        title = "runtime error: thread resource limit reached";
        hints = {
            "the system refused to create another thread",
            "reduce the number of concurrent threads, reuse threads with a pool, or check process/system limits",
        };
      }
      else if (icontains(log, "pthread_create"))
      {
        title = "runtime error: pthread_create failed";
        hints = {
            "the low-level thread creation call failed",
            "check resource limits, stack size, and whether too many threads are being spawned",
        };
      }

      std::cerr << RED
                << title
                << RESET << "\n";

      print_runtime_hints_and_at(
          hints,
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeThreadCreationFailureRule()
  {
    return std::make_unique<ThreadCreationFailureRule>();
  }
} // namespace vix::cli::errors::runtime
