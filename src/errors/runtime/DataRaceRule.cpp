/**
 *
 *  @file DataRaceRule.cpp
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
    enum class DataRaceKind
    {
      ReadWriteRace,
      WriteWriteRace,
      AtomicRace,
      MutexProtectedRace,
      ThreadSanitizerRace,
      GenericRace,
    };

    DataRaceKind classify_data_race(const std::string &log)
    {
      const bool hasRead =
          icontains(log, "Read of size") ||
          icontains(log, "read of size");

      const bool hasWrite =
          icontains(log, "Write of size") ||
          icontains(log, "write of size");

      const bool hasPreviousRead =
          icontains(log, "Previous read of size") ||
          icontains(log, "previous read of size");

      const bool hasPreviousWrite =
          icontains(log, "Previous write of size") ||
          icontains(log, "previous write of size");

      if (hasWrite && hasPreviousWrite)
        return DataRaceKind::WriteWriteRace;

      if ((hasRead && hasPreviousWrite) ||
          (hasWrite && hasPreviousRead) ||
          (hasRead && hasWrite))
      {
        return DataRaceKind::ReadWriteRace;
      }

      if (icontains(log, "atomic") ||
          icontains(log, "std::atomic"))
      {
        return DataRaceKind::AtomicRace;
      }

      if (icontains(log, "mutex") ||
          icontains(log, "pthread_mutex") ||
          icontains(log, "lock"))
      {
        return DataRaceKind::MutexProtectedRace;
      }

      if (icontains(log, "ThreadSanitizer"))
        return DataRaceKind::ThreadSanitizerRace;

      return DataRaceKind::GenericRace;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_data_race(log))
      {
      case DataRaceKind::ReadWriteRace:
        return "data race between read and write";

      case DataRaceKind::WriteWriteRace:
        return "data race between concurrent writes";

      case DataRaceKind::AtomicRace:
        return "data race around atomic/shared state";

      case DataRaceKind::MutexProtectedRace:
        return "data race around mutex-protected state";

      case DataRaceKind::ThreadSanitizerRace:
        return "ThreadSanitizer detected a data race";

      case DataRaceKind::GenericRace:
      default:
        return "data race";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      switch (classify_data_race(log))
      {
      case DataRaceKind::ReadWriteRace:
        return "protect every read and write of the shared value with the same mutex, or make the value atomic";

      case DataRaceKind::WriteWriteRace:
        return "two threads write the same memory concurrently; serialize writes with a mutex or redesign ownership";

      case DataRaceKind::AtomicRace:
        return "ensure all accesses to the shared value use std::atomic or are protected by the same lock";

      case DataRaceKind::MutexProtectedRace:
        return "verify that all code paths use the same mutex before touching the shared state";

      case DataRaceKind::ThreadSanitizerRace:
        return "read the ThreadSanitizer report below; it usually shows both conflicting access locations";

      case DataRaceKind::GenericRace:
      default:
        return "protect shared mutable state with std::mutex, std::scoped_lock, or std::atomic";
      }
    }

    std::vector<std::string> source_patterns_for_data_race()
    {
      return {
          "std::thread",
          "std::jthread",
          "std::async",
          "std::atomic",
          "std::mutex",
          "std::scoped_lock",
          "std::lock_guard",
          "std::unique_lock",
          ".lock(",
          ".unlock(",
          ".store(",
          ".load(",
          "++",
          "--",
          "+=",
          "-=",
          "=",
      };
    }

    bool looks_like_data_race_log(const std::string &log)
    {
      return icontains(log, "ThreadSanitizer") ||
             icontains(log, "data race") ||
             icontains(log, "WARNING: ThreadSanitizer") ||
             icontains(log, "Read of size") ||
             icontains(log, "Write of size") ||
             icontains(log, "Previous read of size") ||
             icontains(log, "Previous write of size");
    }
  } // namespace

  class DataRaceRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_data_race_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_data_race());
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
              "do not ignore the runtime log: data races usually require comparing both conflicting stack traces",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 24);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeDataRaceRule()
  {
    return std::make_unique<DataRaceRule>();
  }
} // namespace vix::cli::errors::runtime
