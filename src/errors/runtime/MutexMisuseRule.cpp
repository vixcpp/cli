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
  namespace
  {
    std::string choose_message(const std::string &log)
    {
      if (icontains(log, "resource deadlock avoided"))
        return "mutex deadlock";

      if (icontains(log, "operation not permitted"))
        return "invalid mutex unlock";

      if (icontains(log, "invalid argument"))
        return "invalid mutex state";

      return "mutex misuse";
    }

    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "resource deadlock avoided"))
        return "avoid locking the same non-recursive mutex twice in the same thread";

      if (icontains(log, "operation not permitted"))
        return "only unlock a mutex owned by the current thread and prefer RAII locks";

      if (icontains(log, "invalid argument"))
        return "check mutex lifetime and avoid using an uninitialized or destroyed mutex";

      return "check double lock, invalid unlock, destroyed mutex access, and inconsistent lock usage";
    }
  } // namespace

  class MutexMisuseRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "resource deadlock avoided") ||
             icontains(log, "operation not permitted") ||
             icontains(log, "pthread_mutex") ||
             (icontains(log, "mutex") &&
              (icontains(log, "deadlock") ||
               icontains(log, "unlock") ||
               icontains(log, "lock") ||
               icontains(log, "invalid argument")));
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

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

  std::unique_ptr<IRuntimeErrorRule> makeMutexMisuseRule()
  {
    return std::make_unique<MutexMisuseRule>();
  }
} // namespace vix::cli::errors::runtime
