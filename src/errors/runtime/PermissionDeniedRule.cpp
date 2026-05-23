/**
 *
 *  @file PermissionDeniedRule.cpp
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
    enum class PermissionKind
    {
      PermissionDenied,
      OperationNotPermitted,
      GenericPermission,
    };

    PermissionKind classify_issue(const std::string &log)
    {
      if (icontains(log, "operation not permitted") || icontains(log, "EPERM"))
        return PermissionKind::OperationNotPermitted;

      if (icontains(log, "Permission denied") ||
          icontains(log, "permission denied") ||
          icontains(log, "EACCES"))
      {
        return PermissionKind::PermissionDenied;
      }

      return PermissionKind::GenericPermission;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "permission denied";
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "check file permissions, ownership, sudo requirements, or target directory access";
    }

    std::vector<std::string> source_patterns_for_permission()
    {
      return {
          "std::filesystem",
          "open(",
          "ifstream",
          "ofstream",
          "remove(",
          "rename(",
          "chmod",
          "chown",
      };
    }

    bool looks_like_permission_log(const std::string &log)
    {
      return icontains(log, "Permission denied") ||
             icontains(log, "permission denied") ||
             icontains(log, "EACCES") ||
             icontains(log, "EPERM") ||
             icontains(log, "operation not permitted");
    }
  } // namespace

  class PermissionDeniedRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_permission_log(log);
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
            source_patterns_for_permission());
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
              "the runtime log below contains the exact path that the OS rejected",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makePermissionDeniedRule()
  {
    return std::make_unique<PermissionDeniedRule>();
  }
} // namespace vix::cli::errors::runtime
