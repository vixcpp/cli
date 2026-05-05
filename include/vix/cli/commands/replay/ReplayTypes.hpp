/**
 *
 *  @file ReplayTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_CLI_COMMANDS_REPLAY_TYPES_HPP
#define VIX_CLI_COMMANDS_REPLAY_TYPES_HPP

#include <cstdint>
#include <string>

namespace vix::commands::replay
{

  /**
   * @brief Describes the high-level command that produced a replay record.
   *
   * A replay can come from `vix run`, `vix dev`, or later from a dedicated
   * runtime/server capture mode.
   */
  enum class ReplayMode
  {
    Unknown,
    Run,
    Dev,
    Test,
    Server
  };

  /**
   * @brief Describes the original input kind used by the command.
   *
   * This tells Vix whether the recorded execution came from a single C++ file,
   * a CMake/Vix project, a manifest file, or an already-built binary.
   */
  enum class ReplayTargetKind
  {
    Unknown,
    SingleCpp,
    Project,
    Manifest,
    Binary
  };

  /**
   * @brief Describes the final status of a recorded execution.
   */
  enum class ReplayStatus
  {
    Unknown,
    Success,
    Failed,
    Interrupted,
    TimedOut,
    Crashed
  };

  /**
   * @brief Describes the source of a recorded failure.
   */
  enum class ReplayErrorKind
  {
    None,
    BuildError,
    RuntimeError,
    Signal,
    Timeout,
    InternalError,
    Unknown
  };

  /**
   * @brief Convert a replay mode to a stable string.
   *
   * @param mode Replay mode.
   * @return Stable lowercase string representation.
   */
  inline std::string to_string(ReplayMode mode)
  {
    switch (mode)
    {
    case ReplayMode::Run:
      return "run";
    case ReplayMode::Dev:
      return "dev";
    case ReplayMode::Test:
      return "test";
    case ReplayMode::Server:
      return "server";
    case ReplayMode::Unknown:
    default:
      return "unknown";
    }
  }

  /**
   * @brief Convert a replay target kind to a stable string.
   *
   * @param kind Replay target kind.
   * @return Stable lowercase string representation.
   */
  inline std::string to_string(ReplayTargetKind kind)
  {
    switch (kind)
    {
    case ReplayTargetKind::SingleCpp:
      return "single-cpp";
    case ReplayTargetKind::Project:
      return "project";
    case ReplayTargetKind::Manifest:
      return "manifest";
    case ReplayTargetKind::Binary:
      return "binary";
    case ReplayTargetKind::Unknown:
    default:
      return "unknown";
    }
  }

  /**
   * @brief Convert a replay status to a stable string.
   *
   * @param status Replay status.
   * @return Stable lowercase string representation.
   */
  inline std::string to_string(ReplayStatus status)
  {
    switch (status)
    {
    case ReplayStatus::Success:
      return "success";
    case ReplayStatus::Failed:
      return "failed";
    case ReplayStatus::Interrupted:
      return "interrupted";
    case ReplayStatus::TimedOut:
      return "timed-out";
    case ReplayStatus::Crashed:
      return "crashed";
    case ReplayStatus::Unknown:
    default:
      return "unknown";
    }
  }

  /**
   * @brief Convert a replay error kind to a stable string.
   *
   * @param kind Replay error kind.
   * @return Stable lowercase string representation.
   */
  inline std::string to_string(ReplayErrorKind kind)
  {
    switch (kind)
    {
    case ReplayErrorKind::None:
      return "none";
    case ReplayErrorKind::BuildError:
      return "build-error";
    case ReplayErrorKind::RuntimeError:
      return "runtime-error";
    case ReplayErrorKind::Signal:
      return "signal";
    case ReplayErrorKind::Timeout:
      return "timeout";
    case ReplayErrorKind::InternalError:
      return "internal-error";
    case ReplayErrorKind::Unknown:
    default:
      return "unknown";
    }
  }

  /**
   * @brief Parse a replay mode from a stable string.
   *
   * @param value String value.
   * @return Parsed replay mode, or ReplayMode::Unknown.
   */
  inline ReplayMode replay_mode_from_string(const std::string &value)
  {
    if (value == "run")
      return ReplayMode::Run;
    if (value == "dev")
      return ReplayMode::Dev;
    if (value == "test")
      return ReplayMode::Test;
    if (value == "server")
      return ReplayMode::Server;

    return ReplayMode::Unknown;
  }

  /**
   * @brief Parse a replay target kind from a stable string.
   *
   * @param value String value.
   * @return Parsed target kind, or ReplayTargetKind::Unknown.
   */
  inline ReplayTargetKind replay_target_kind_from_string(const std::string &value)
  {
    if (value == "single-cpp")
      return ReplayTargetKind::SingleCpp;
    if (value == "project")
      return ReplayTargetKind::Project;
    if (value == "manifest")
      return ReplayTargetKind::Manifest;
    if (value == "binary")
      return ReplayTargetKind::Binary;

    return ReplayTargetKind::Unknown;
  }

  /**
   * @brief Parse a replay status from a stable string.
   *
   * @param value String value.
   * @return Parsed replay status, or ReplayStatus::Unknown.
   */
  inline ReplayStatus replay_status_from_string(const std::string &value)
  {
    if (value == "success")
      return ReplayStatus::Success;
    if (value == "failed")
      return ReplayStatus::Failed;
    if (value == "interrupted")
      return ReplayStatus::Interrupted;
    if (value == "timed-out")
      return ReplayStatus::TimedOut;
    if (value == "crashed")
      return ReplayStatus::Crashed;

    return ReplayStatus::Unknown;
  }

  /**
   * @brief Parse a replay error kind from a stable string.
   *
   * @param value String value.
   * @return Parsed error kind, or ReplayErrorKind::Unknown.
   */
  inline ReplayErrorKind replay_error_kind_from_string(const std::string &value)
  {
    if (value == "none")
      return ReplayErrorKind::None;
    if (value == "build-error")
      return ReplayErrorKind::BuildError;
    if (value == "runtime-error")
      return ReplayErrorKind::RuntimeError;
    if (value == "signal")
      return ReplayErrorKind::Signal;
    if (value == "timeout")
      return ReplayErrorKind::Timeout;
    if (value == "internal-error")
      return ReplayErrorKind::InternalError;

    return ReplayErrorKind::Unknown;
  }

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_TYPES_HPP
