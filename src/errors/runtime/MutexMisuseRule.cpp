/**
 *
 *  @file MutexMisuseRule.cpp
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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  class MutexMisuseRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "mutex") ||
             icontains(log, "resource deadlock avoided") ||
             icontains(log, "operation not permitted") ||
             icontains(log, "pthread_mutex");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      std::string title = "runtime error: mutex misuse";
      std::vector<std::string> hints = {
          "a mutex was used incorrectly",
          "check double lock, unlock without ownership, destroyed mutex access, and inconsistent lock usage",
      };

      if (icontains(log, "resource deadlock avoided"))
      {
        title = "runtime error: mutex deadlock";
        hints = {
            "the same thread likely tried to lock a non-recursive mutex twice",
            "avoid locking the same mutex twice, or redesign the locking flow",
        };
      }
      else if (icontains(log, "operation not permitted"))
      {
        title = "runtime error: invalid mutex unlock";
        hints = {
            "a mutex was likely unlocked by a thread that does not own it",
            "only unlock mutexes locked by the current thread, and prefer RAII with std::lock_guard or std::unique_lock",
        };
      }
      else if (icontains(log, "invalid argument"))
      {
        title = "runtime error: invalid mutex state";
        hints = {
            "the mutex may be uninitialized, destroyed, or otherwise invalid",
            "check mutex lifetime and avoid using a mutex after destruction",
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

  std::unique_ptr<IRuntimeErrorRule> makeMutexMisuseRule()
  {
    return std::make_unique<MutexMisuseRule>();
  }
} // namespace vix::cli::errors::runtime
