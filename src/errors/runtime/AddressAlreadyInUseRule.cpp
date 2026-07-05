/**
 *
 *  @file AddressAlreadyInUseRule.cpp
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

#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    std::optional<int> try_extract_port(const std::string &log)
    {
      static const std::regex re(
          R"((?:port|:)[ \t]*([0-9]{2,5}))",
          std::regex::icase);

      std::smatch match;
      if (!std::regex_search(log, match, re))
        return std::nullopt;

      if (match.size() < 2)
        return std::nullopt;

      try
      {
        const int port = std::stoi(match[1].str());
        if (port > 0 && port <= 65535)
          return port;
      }
      catch (...)
      {
        return std::nullopt;
      }

      return std::nullopt;
    }

    std::string choose_message(const std::string &log)
    {
      (void)log;
      return "address already in use";
    }

    std::string choose_hint(const std::string &log)
    {
      if (const auto port = try_extract_port(log))
      {
        return "port " + std::to_string(*port) +
               " is already in use; stop the other process or change the port";
      }

      return "this address is already in use; stop the other process or change host/port";
    }

    std::vector<std::string> source_patterns_for_addr_in_use()
    {
      return {
          "bind(",
          "listen(",
          "SERVER_PORT",
          "socket",
          "port",
          "host",
      };
    }

    bool looks_like_addr_in_use_log(const std::string &log)
    {
      if (icontains(log, "Address already in use") ||
          icontains(log, "address already in use") ||
          icontains(log, "EADDRINUSE"))
      {
        return true;
      }

      if (icontains(log, "port already in use"))
        return true;

      return false;
    }
  } // namespace

  class AddressAlreadyInUseRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_addr_in_use_log(log);
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
            source_patterns_for_addr_in_use());
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
              "on Linux, `lsof -i :<port>` or `ss -lptn` can list the process holding the address",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeAddressAlreadyInUseRule()
  {
    return std::make_unique<AddressAlreadyInUseRule>();
  }
} // namespace vix::cli::errors::runtime
