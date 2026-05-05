/**
 *
 *  @file ReplayJson.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_JSON_HPP
#define VIX_CLI_COMMANDS_REPLAY_JSON_HPP

#include <string>

#include <nlohmann/json.hpp>

#include <vix/cli/commands/replay/ReplayRecord.hpp>

namespace vix::commands::replay
{

  /**
   * @brief Convert a replay environment variable to JSON.
   *
   * @param env Environment variable.
   * @return JSON representation.
   */
  nlohmann::json replay_env_var_to_json(const ReplayEnvVar &env);

  /**
   * @brief Convert JSON to a replay environment variable.
   *
   * @param json JSON value.
   * @return Parsed environment variable.
   */
  ReplayEnvVar replay_env_var_from_json(const nlohmann::json &json);

  /**
   * @brief Convert replay timing information to JSON.
   *
   * @param timing Timing information.
   * @return JSON representation.
   */
  nlohmann::json replay_timing_to_json(const ReplayTiming &timing);

  /**
   * @brief Convert JSON to replay timing information.
   *
   * @param json JSON value.
   * @return Parsed timing information.
   */
  ReplayTiming replay_timing_from_json(const nlohmann::json &json);

  /**
   * @brief Convert a replay process result to JSON.
   *
   * @param result Process result.
   * @return JSON representation.
   */
  nlohmann::json replay_process_result_to_json(const ReplayProcessResult &result);

  /**
   * @brief Convert JSON to a replay process result.
   *
   * @param json JSON value.
   * @return Parsed process result.
   */
  ReplayProcessResult replay_process_result_from_json(const nlohmann::json &json);

  /**
   * @brief Convert replay log paths to JSON.
   *
   * @param logs Log paths.
   * @return JSON representation.
   */
  nlohmann::json replay_log_paths_to_json(const ReplayLogPaths &logs);

  /**
   * @brief Convert JSON to replay log paths.
   *
   * @param json JSON value.
   * @return Parsed log paths.
   */
  ReplayLogPaths replay_log_paths_from_json(const nlohmann::json &json);

  /**
   * @brief Convert a replay record to JSON.
   *
   * @param record Replay record.
   * @return JSON representation.
   */
  nlohmann::json replay_record_to_json(const ReplayRecord &record);

  /**
   * @brief Convert JSON to a replay record.
   *
   * Missing fields are interpreted as default values.
   *
   * @param json JSON value.
   * @return Parsed replay record.
   */
  ReplayRecord replay_record_from_json(const nlohmann::json &json);

  /**
   * @brief Serialize a replay record to pretty JSON text.
   *
   * @param record Replay record.
   * @return Pretty JSON string.
   */
  std::string replay_record_to_json_string(const ReplayRecord &record);

  /**
   * @brief Parse a replay record from JSON text.
   *
   * @param text JSON text.
   * @param record Output replay record.
   * @param err Error message written on failure.
   * @return true on success.
   */
  bool replay_record_from_json_string(
      const std::string &text,
      ReplayRecord &record,
      std::string &err);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_JSON_HPP
