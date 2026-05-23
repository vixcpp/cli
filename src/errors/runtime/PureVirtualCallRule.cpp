/**
 *
 *  @file PureVirtualCallRule.cpp
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
  namespace
  {
    enum class PureVirtualKind
    {
      LibcxxPureVirtual,
      GenericPureVirtual,
    };

    PureVirtualKind classify_issue(const std::string &log)
    {
      if (icontains(log, "__cxa_pure_virtual"))
        return PureVirtualKind::LibcxxPureVirtual;

      return PureVirtualKind::GenericPureVirtual;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "pure virtual function call";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "avoid calling virtual functions from constructors/destructors or after object destruction";
    }

    std::vector<std::string> source_patterns_for_pure_virtual()
    {
      return {
          "virtual",
          "= 0",
          "~",
      };
    }

    bool looks_like_pure_virtual_log(const std::string &log)
    {
      return icontains(log, "pure virtual method called") ||
             icontains(log, "pure virtual function call") ||
             icontains(log, "__cxa_pure_virtual") ||
             icontains(log, "pure virtual");
    }
  } // namespace

  class PureVirtualCallRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_pure_virtual_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      RuntimeLocation location = find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location = find_best_runtime_location_or_source_hint(
            log,
            sourceFile,
            source_patterns_for_pure_virtual());
      }

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
              "the dynamic type during base construction/destruction is the base, not the derived class",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makePureVirtualCallRule()
  {
    return std::make_unique<PureVirtualCallRule>();
  }
} // namespace vix::cli::errors::runtime
