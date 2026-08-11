/**
 *
 *  @file AddressAlreadyInUseRule.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the LICENSE file.
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

    std::optional<int> try_extract_port(
        const std::string &log)
    {
      /*
       * Prefer an explicit "port <number>" marker.
       *
       * Do not accept a bare ':' before the number here.
       * A pattern such as ':8081' also matches timestamps:
       *
       *   13:10:49
       *      ^^^
       *
       * and would incorrectly report port 10.
       */
      static const std::regex re(
          R"(\bport[ \t]*[:=]?[ \t]*([0-9]{1,5})\b)",
          std::regex::icase);

      std::smatch match;

      if (!std::regex_search(log, match, re))
        return std::nullopt;

      if (match.size() < 2)
        return std::nullopt;

      try
      {
        const int port =
            std::stoi(match[1].str());

        if (port > 0 && port <= 65535)
          return port;
      }
      catch (...)
      {
        return std::nullopt;
      }

      return std::nullopt;
    }

    std::string choose_message(
        const std::string &log)
    {
      if (try_extract_port(log))
        return "port is already in use";

      return "address is already in use";
    }

    std::string choose_description(
        const std::string &log)
    {
      if (const auto port = try_extract_port(log))
      {
        return "Port " +
               std::to_string(*port) +
               " is already being used by another process.";
      }

      return "This network address is already being used by another process.";
    }

    std::string choose_hint(
        const std::string &log)
    {
      if (const auto port = try_extract_port(log))
      {
        return "stop the process using port " +
               std::to_string(*port) +
               " or choose another port";
      }

      return "stop the process using this address or choose another host or port";
    }

    std::vector<std::string>
    source_patterns_for_addr_in_use()
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

    bool looks_like_addr_in_use_log(
        const std::string &log)
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
       * Print "at:" only when Vix found a real file and line.
       *
       * Do not use sourceFile as a fallback because
       * "source: main.cpp" is not an exact error location.
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
        const std::string &log,
        const std::optional<int> port)
    {
      if (!technical_details_enabled())
        return;

      std::cerr << "\n"
                << GRAY
                << "technical details:"
                << RESET
                << "\n";

#if defined(__linux__)
      if (port)
      {
        std::cerr << "  inspect: lsof -i :"
                  << *port
                  << " or ss -lptn"
                  << "\n";
      }
#endif

      print_runtime_log_excerpt(
          log,
          20);
    }
  } // namespace

  class AddressAlreadyInUseRule final
      : public IRuntimeErrorRule
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
         * Preserve the existing runtime-location contract.
         *
         * When the runtime log has no location, search the
         * source for the most relevant network operation.
         */
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_addr_in_use());
      }

      print_error(
          message,
          description);

      if (location.valid())
      {
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
       * Native runtime output and platform-specific inspection
       * commands remain available through:
       *
       * VIX_LOG_LEVEL=debug vix run ...
       */
      print_debug_details(
          log,
          try_extract_port(log));

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeAddressAlreadyInUseRule()
  {
    return std::make_unique<
        AddressAlreadyInUseRule>();
  }
} // namespace vix::cli::errors::runtime
