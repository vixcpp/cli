/**
 *
 *  @file BrokenPipeRule.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>
#include <vix/cli/errors/runtime/RuntimeRuleUtils.hpp>

#include <cctype>
#include <cstdlib>
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

    std::string trim_copy(const std::string &value)
    {
      std::size_t begin = 0;

      while (begin < value.size() &&
             std::isspace(
                 static_cast<unsigned char>(value[begin])) != 0)
      {
        ++begin;
      }

      std::size_t end = value.size();

      while (end > begin &&
             std::isspace(
                 static_cast<unsigned char>(value[end - 1])) != 0)
      {
        --end;
      }

      return value.substr(begin, end - begin);
    }

    std::string lowercase_copy(std::string value)
    {
      for (char &character : value)
      {
        character = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(character)));
      }

      return value;
    }

    bool technical_details_enabled()
    {
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (level == nullptr || *level == '\0')
        return false;

      const std::string value =
          lowercase_copy(trim_copy(level));

      return value == "debug" ||
             value == "trace";
    }

    BrokenPipeKind classify_issue(
        const std::string &log)
    {
      if (icontains(log, "Broken pipe") ||
          icontains(log, "EPIPE"))
      {
        return BrokenPipeKind::BrokenPipe;
      }

      if (icontains(log, "connection reset by peer") ||
          icontains(log, "ECONNRESET"))
      {
        return BrokenPipeKind::ConnectionReset;
      }

      if (icontains(log, "write failed"))
        return BrokenPipeKind::WriteFailed;

      return BrokenPipeKind::GenericPeerClosed;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BrokenPipeKind::ConnectionReset:
        return "connection was reset";

      case BrokenPipeKind::BrokenPipe:
      case BrokenPipeKind::WriteFailed:
        return "connection closed during write";

      case BrokenPipeKind::GenericPeerClosed:
      default:
        return "connection closed unexpectedly";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BrokenPipeKind::ConnectionReset:
        return "The remote peer closed the connection before the operation completed.";

      case BrokenPipeKind::BrokenPipe:
        return "The program tried to write after the other side had closed the connection.";

      case BrokenPipeKind::WriteFailed:
        return "Data could not be sent because the connection was no longer available.";

      case BrokenPipeKind::GenericPeerClosed:
      default:
        return "The connection ended before all data could be sent.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_issue(log))
      {
      case BrokenPipeKind::ConnectionReset:
        return "handle peer disconnections and retry only when repeating the operation is safe";

      case BrokenPipeKind::BrokenPipe:
        return "check the write result and stop sending after the connection closes";

      case BrokenPipeKind::WriteFailed:
        return "check the returned error before attempting another write";

      case BrokenPipeKind::GenericPeerClosed:
      default:
        return "handle connection closure before sending more data";
      }
    }

    std::vector<std::string>
    source_patterns_for_broken_pipe()
    {
      return {
          "async_write",
          "asio::write",
          ".write(",
          "::write(",
          "send(",
          "sendto(",
          "sendmsg(",
      };
    }

    bool looks_like_broken_pipe_log(
        const std::string &log)
    {
      if (icontains(log, "Broken pipe") ||
          icontains(log, "EPIPE") ||
          icontains(log, "connection reset by peer") ||
          icontains(log, "ECONNRESET"))
      {
        return true;
      }

      /*
       * "write failed" alone can describe many unrelated
       * operations. Require network or stream context.
       */
      if (icontains(log, "write failed") &&
          (icontains(log, "socket") ||
           icontains(log, "stream") ||
           icontains(log, "connection")))
      {
        return true;
      }

      return false;
    }

    void print_error(
        const std::string &message,
        const std::string &description)
    {
      std::cerr << RED
                << "runtime error: "
                << message
                << RESET
                << "\n";

      if (!description.empty())
      {
        std::cerr << "  "
                  << description
                  << "\n";
      }
    }

    void print_hint_and_location(
        const std::string &hint,
        const RuntimeLocation &location)
    {
      std::vector<std::string> hints;

      if (!hint.empty())
        hints.push_back(hint);

      /*
       * Do not fall back to "source: main.cpp".
       * Print "at:" only when Vix found a useful location.
       */
      const std::string at =
          location.valid()
              ? make_at_text(
                    location,
                    std::filesystem::path{})
              : std::string{};

      if (hints.empty() && at.empty())
        return;

      std::cerr << "\n";

      print_runtime_hints_and_at(
          hints,
          at);
    }

    void print_debug_details(
        const std::string &log)
    {
      if (!technical_details_enabled())
        return;

      std::cerr << "\n"
                << GRAY
                << "technical details:"
                << RESET
                << "\n";

      print_runtime_log_excerpt(
          log,
          20);
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
      const std::string message =
          choose_message(log);

      const std::string description =
          choose_description(log);

      const std::string hint =
          choose_hint(log);

      RuntimeLocation location =
          find_best_runtime_location(
              log,
              sourceFile);

      if (!location.valid())
      {
        /*
         * Preserve the existing location contract.
         *
         * When the runtime provides no location, search for
         * a likely write operation in the supplied source.
         */
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_broken_pipe());
      }

      print_error(
          message,
          description);

      if (location.valid())
      {
        std::cerr << "\n";

        const auto error =
            make_runtime_location(
                location.file,
                location.line,
                location.column,
                message);

        print_runtime_codeframe(error);
      }

      print_hint_and_location(
          hint,
          location);

      /*
       * Native operating-system output remains available with:
       *
       * VIX_LOG_LEVEL=debug vix run ...
       */
      print_debug_details(log);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeBrokenPipeRule()
  {
    return std::make_unique<BrokenPipeRule>();
  }
} // namespace vix::cli::errors::runtime
