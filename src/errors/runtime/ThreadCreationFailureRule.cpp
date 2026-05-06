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

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::string choose_message(const std::string &log)
    {
      if (icontains(log, "resource temporarily unavailable"))
        return "thread resource limit reached";

      if (icontains(log, "pthread_create"))
        return "pthread_create failed";

      return "thread creation failed";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "resource temporarily unavailable"))
        return "reduce the number of concurrent threads or use a thread pool";

      if (icontains(log, "pthread_create"))
        return "check thread limits, available memory, stack size, and thread count";

      return "check system thread limits, available memory, and excessive thread creation";
    }
  } // namespace

  class ThreadCreationFailureRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return (icontains(log, "std::system_error") &&
              (icontains(log, "thread") ||
               icontains(log, "resource temporarily unavailable"))) ||
             icontains(log, "resource temporarily unavailable") ||
             icontains(log, "thread constructor failed") ||
             icontains(log, "failed to create thread") ||
             icontains(log, "pthread_create");
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const RuntimeLocation location =
          find_best_runtime_location_or_source_hint(
              log,
              sourceFile,
              {
                  "std::thread",
                  "thread(",
                  ".detach(",
                  ".join(",
                  "pthread_create",
                  "emplace_back(",
                  "push_back(",
              });

      const std::string message = choose_message(log);

      std::cerr << RED
                << "runtime error: "
                << message
                << RESET << "\n";

      if (location.valid())
      {
        const auto err = make_runtime_location(
            location.file,
            location.line,
            location.column,
            message);

        print_runtime_codeframe(err);
      }

      print_runtime_hints_and_at(
          {
              choose_hint(log),
          },
          make_at_text(location, sourceFile));

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeThreadCreationFailureRule()
  {
    return std::make_unique<ThreadCreationFailureRule>();
  }
} // namespace vix::cli::errors::runtime
