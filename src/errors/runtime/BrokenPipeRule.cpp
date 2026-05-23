/**
 *
 *  @file BrokenPipeRule.cpp
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
    enum class BrokenPipeKind
    {
      BrokenPipe,
      ConnectionReset,
      WriteFailed,
      GenericPeerClosed,
    };

    BrokenPipeKind classify_issue(const std::string &log)
    {
      if (icontains(log, "Broken pipe") || icontains(log, "EPIPE"))
        return BrokenPipeKind::BrokenPipe;

      if (icontains(log, "connection reset by peer") ||
          icontains(log, "ECONNRESET"))
      {
        return BrokenPipeKind::ConnectionReset;
      }

      if (icontains(log, "write failed"))
        return BrokenPipeKind::WriteFailed;

      return BrokenPipeKind::GenericPeerClosed;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BrokenPipeKind::ConnectionReset:
        return "connection reset by peer";

      case BrokenPipeKind::BrokenPipe:
      case BrokenPipeKind::WriteFailed:
      case BrokenPipeKind::GenericPeerClosed:
      default:
        return "broken pipe";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "the peer closed the connection before the write completed; handle disconnects and retry only when safe";
    }

    std::vector<std::string> source_patterns_for_broken_pipe()
    {
      return {
          "async_write",
          ".write(",
          "write(",
          "send(",
          "socket",
          "stream",
      };
    }

    bool looks_like_broken_pipe_log(const std::string &log)
    {
      if (icontains(log, "Broken pipe") ||
          icontains(log, "EPIPE") ||
          icontains(log, "connection reset by peer") ||
          icontains(log, "ECONNRESET"))
      {
        return true;
      }

      // "write failed" alone is too generic; require additional socket context
      if (icontains(log, "write failed") &&
          (icontains(log, "socket") ||
           icontains(log, "stream") ||
           icontains(log, "connection")))
      {
        return true;
      }

      return false;
    }
  } // namespace

  class BrokenPipeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_broken_pipe_log(log);
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
            source_patterns_for_broken_pipe());
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
              "check write error codes instead of assuming the peer is still connected",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeBrokenPipeRule()
  {
    return std::make_unique<BrokenPipeRule>();
  }
} // namespace vix::cli::errors::runtime
