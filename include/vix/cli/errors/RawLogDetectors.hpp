/**
 *
 *  @file RawLogDetectors.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RAW_LOG_DETECTORS_HPP
#define VIX_RAW_LOG_DETECTORS_HPP

#include <filesystem>
#include <string>

namespace vix::cli::errors
{
  /**
   * @brief Structured evidence captured from a completed runtime process.
   *
   * Text emitted by the application is not, by itself, evidence of a runtime
   * fault. Signal termination and recognized sanitizer diagnostics provide
   * the provenance required before runtime error rules may classify a run.
   */
  struct RuntimeCrashEvidence final
  {
    bool terminatedBySignal{false};
    int termSignal{0};
  };

  class RawLogDetectors
  {
  public:
    /**
     * @brief Returns whether a process result and its log establish a runtime event.
     */
    [[nodiscard]]
    static bool hasAuthoritativeRuntimeEvidence(
        const std::string &runtimeLog,
        const RuntimeCrashEvidence &evidence);

    static bool handleLinkerOrSanitizer(
        const std::string &buildLog,
        const std::filesystem::path &sourceFile,
        const std::string &contextMessage);

    static bool handleRuntimeCrash(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile,
        const std::string &contextMessage,
        const RuntimeCrashEvidence &evidence = {});

    static bool handleKnownRunFailure(const std::string &log, const std::filesystem::path &ctx);
  };
} // namespace vix::cli::errors

#endif
