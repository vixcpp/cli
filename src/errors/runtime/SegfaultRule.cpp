/**
 *
 *  @file SegfaultRule.cpp
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
    std::string choose_hint(const std::string &log)
    {
      if (icontains(log, "null") ||
          icontains(log, "address 0x0") ||
          icontains(log, "addr 0x0"))
      {
        return "check for null pointers before dereferencing them";
      }

      if (icontains(log, "out of bounds") ||
          icontains(log, "buffer") ||
          icontains(log, "overflow"))
      {
        return "check array, vector, string, and buffer bounds before access";
      }

      if (icontains(log, "use-after-free") ||
          icontains(log, "dangling"))
      {
        return "check dangling pointers, references, and object lifetimes";
      }

      return "check null pointers, dangling pointers, out-of-bounds access, and invalid references";
    }
  } // namespace

  class SegfaultRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      return icontains(log, "segmentation fault") ||
             icontains(log, "sigsegv") ||
             icontains(log, "signal 11");
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
                  "nullptr",
                  "= nullptr",
                  "->",
                  "*",
                  "[",
                  ".at(",
                  "delete",
                  "free(",
              });

      const std::string message = "segmentation fault";

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

  std::unique_ptr<IRuntimeErrorRule> makeSegfaultRule()
  {
    return std::make_unique<SegfaultRule>();
  }
} // namespace vix::cli::errors::runtime
