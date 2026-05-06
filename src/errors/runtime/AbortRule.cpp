/**
 *
 *  @file AbortRule.cpp
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
      if (icontains(log, "terminate called"))
        return "std::terminate called";

      if (icontains(log, "abort()") ||
          icontains(log, "std::abort"))
        return "std::abort called";

      if (icontains(log, "sigabrt") ||
          icontains(log, "signal 6"))
        return "SIGABRT received";

      return "aborted";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "terminate called"))
        return "check uncaught exceptions, joinable std::thread destruction, or noexcept violations";

      if (icontains(log, "abort()") ||
          icontains(log, "std::abort"))
        return "check the code path that explicitly calls abort() or std::abort()";

      if (icontains(log, "assert") ||
          icontains(log, "assertion"))
        return "check the failed assertion and the condition that was expected to be true";

      return "check assertions, std::terminate(), uncaught exceptions, and invalid runtime states";
    }
  } // namespace

  class AbortRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "aborted") ||
             icontains(log, "sigabrt") ||
             icontains(log, "signal 6") ||
             icontains(log, "abort()") ||
             icontains(log, "std::abort") ||
             icontains(log, "core dumped") ||
             icontains(log, "terminate called");
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
                  "std::abort",
                  "abort()",
                  "abort(",
                  "assert(",
                  "throw ",
                  "std::terminate",
                  "terminate()",
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

  std::unique_ptr<IRuntimeErrorRule> makeAbortRule()
  {
    return std::make_unique<AbortRule>();
  }
} // namespace vix::cli::errors::runtime
