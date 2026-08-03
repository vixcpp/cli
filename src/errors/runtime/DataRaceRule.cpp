/**
 *
 *  @file DataRaceRule.cpp
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
    enum class DataRaceKind
    {
      ReadWriteRace,
      WriteWriteRace,
      MixedAtomicAccess,
      InconsistentLocking,
      ThreadSanitizerRace,
      GenericRace,
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

    bool has_current_read(const std::string &log)
    {
      return icontains(log, "Read of size");
    }

    bool has_current_write(const std::string &log)
    {
      return icontains(log, "Write of size");
    }

    bool has_previous_read(const std::string &log)
    {
      return icontains(log, "Previous read of size");
    }

    bool has_previous_write(const std::string &log)
    {
      return icontains(log, "Previous write of size");
    }

    bool has_atomic_access(const std::string &log)
    {
      /*
       * Do not classify a report as atomic merely because an
       * unrelated stack frame contains std::atomic.
       *
       * These phrases describe an actual atomic access in a
       * ThreadSanitizer report.
       */
      return icontains(log, "Atomic read of size") ||
             icontains(log, "Atomic write of size") ||
             icontains(log, "Previous atomic read of size") ||
             icontains(log, "Previous atomic write of size");
    }

    bool has_lock_context(const std::string &log)
    {
      return icontains(log, "Mutexes:") ||
             icontains(log, "pthread_mutex_lock") ||
             icontains(log, "pthread_mutex_unlock") ||
             icontains(log, "std::mutex::lock") ||
             icontains(log, "std::mutex::unlock");
    }

    DataRaceKind classify_data_race(
        const std::string &log)
    {
      if (has_atomic_access(log))
        return DataRaceKind::MixedAtomicAccess;

      const bool currentRead =
          has_current_read(log);

      const bool currentWrite =
          has_current_write(log);

      const bool previousRead =
          has_previous_read(log);

      const bool previousWrite =
          has_previous_write(log);

      if (currentWrite && previousWrite)
        return DataRaceKind::WriteWriteRace;

      if ((currentRead && previousWrite) ||
          (currentWrite && previousRead) ||
          (currentRead && currentWrite))
      {
        return DataRaceKind::ReadWriteRace;
      }

      if (has_lock_context(log))
        return DataRaceKind::InconsistentLocking;

      if (icontains(log, "ThreadSanitizer"))
        return DataRaceKind::ThreadSanitizerRace;

      return DataRaceKind::GenericRace;
    }

    std::string choose_message(
        const std::string &log)
    {
      switch (classify_data_race(log))
      {
      case DataRaceKind::ReadWriteRace:
        return "data race between a read and a write";

      case DataRaceKind::WriteWriteRace:
        return "data race between concurrent writes";

      case DataRaceKind::MixedAtomicAccess:
        return "mixed atomic and non-atomic access";

      case DataRaceKind::InconsistentLocking:
        return "shared state is not consistently locked";

      case DataRaceKind::ThreadSanitizerRace:
      case DataRaceKind::GenericRace:
      default:
        return "data race detected";
      }
    }

    std::string choose_description(
        const std::string &log)
    {
      switch (classify_data_race(log))
      {
      case DataRaceKind::ReadWriteRace:
        return "One thread read shared memory while another thread modified it without proper synchronization.";

      case DataRaceKind::WriteWriteRace:
        return "Two threads modified the same memory at the same time without proper synchronization.";

      case DataRaceKind::MixedAtomicAccess:
        return "The same shared state appears to be accessed through incompatible atomic and non-atomic operations.";

      case DataRaceKind::InconsistentLocking:
        return "Some accesses to the shared state appear to bypass the mutex used by other code paths.";

      case DataRaceKind::ThreadSanitizerRace:
      case DataRaceKind::GenericRace:
      default:
        return "Multiple threads accessed the same memory concurrently, and at least one access modified it.";
      }
    }

    std::string choose_hint(
        const std::string &log)
    {
      switch (classify_data_race(log))
      {
      case DataRaceKind::ReadWriteRace:
        return "protect every read and write with the same mutex, or use an atomic type when appropriate";

      case DataRaceKind::WriteWriteRace:
        return "serialize writes with one mutex or give the shared state a single owner";

      case DataRaceKind::MixedAtomicAccess:
        return "make every access to this value atomic or protect every access with the same mutex";

      case DataRaceKind::InconsistentLocking:
        return "ensure every code path locks the same mutex before accessing the shared state";

      case DataRaceKind::ThreadSanitizerRace:
      case DataRaceKind::GenericRace:
      default:
        return "protect shared mutable state with std::mutex, std::scoped_lock, or std::atomic";
      }
    }

    bool looks_like_data_race_log(
        const std::string &log)
    {
      const bool hasThreadSanitizer =
          icontains(log, "ThreadSanitizer") ||
          icontains(log, "WARNING: ThreadSanitizer");

      const bool explicitlyReportsRace =
          icontains(log, "data race");

      /*
       * "Read of size" and "Write of size" also appear in other
       * sanitizer reports. They are considered data-race evidence
       * only when ThreadSanitizer context is present.
       */
      const bool hasAccessReport =
          has_current_read(log) ||
          has_current_write(log) ||
          has_previous_read(log) ||
          has_previous_write(log) ||
          has_atomic_access(log);

      return explicitlyReportsRace ||
             (hasThreadSanitizer && hasAccessReport);
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

    void print_hints_and_location(
        const std::string &hint,
        const RuntimeLocation &location)
    {
      std::vector<std::string> hints;

      if (!hint.empty())
        hints.push_back(hint);

      hints.push_back(
          "run with VIX_LOG_LEVEL=debug to compare both conflicting access traces");

      /*
       * Do not fall back to "source: main.cpp".
       *
       * A data race normally has two conflicting locations.
       * Without a real ThreadSanitizer frame, searching for
       * operators such as "=", "++" or lock declarations would
       * produce a misleading codeframe.
       */
      const std::string at =
          location.valid()
              ? make_at_text(
                    location,
                    std::filesystem::path{})
              : std::string{};

      std::cerr << "\n";

      print_runtime_hints_and_at(
          hints,
          at);
    }

    void print_debug_log(
        const std::string &log)
    {
      if (!technical_details_enabled())
        return;

      /*
       * The complete ThreadSanitizer report is important because
       * it normally contains both conflicting stack traces.
       */
      print_runtime_log_excerpt(
          log,
          24);
    }
  } // namespace

  class DataRaceRule final
      : public IRuntimeErrorRule
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
      const std::string message =
          choose_message(log);

      const std::string description =
          choose_description(log);

      const std::string hint =
          choose_hint(log);

      /*
       * ThreadSanitizer normally provides the exact frame.
       *
       * Do not use a source-pattern fallback here: a data race
       * cannot be located reliably by scanning for assignments,
       * increments, thread declarations, or mutex declarations.
       */
      const RuntimeLocation location =
          find_best_runtime_location(
              log,
              sourceFile);

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

      print_hints_and_location(
          hint,
          location);

      print_debug_log(log);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule>
  makeDataRaceRule()
  {
    return std::make_unique<DataRaceRule>();
  }
} // namespace vix::cli::errors::runtime
