/**
 *
 *  @file ReplayJson.cpp
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
#include <vix/cli/commands/replay/ReplayJson.hpp>

#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Read an optional string field from JSON.
     *
     * @param json JSON object.
     * @param key Field name.
     * @param fallback Fallback value.
     * @return Parsed string.
     */
    std::string json_string_or(
        const nlohmann::json &json,
        const char *key,
        const std::string &fallback = {})
    {
      if (!json.is_object() || !json.contains(key))
        return fallback;

      const auto &value = json.at(key);
      if (!value.is_string())
        return fallback;

      return value.get<std::string>();
    }

    /**
     * @brief Read an optional int field from JSON.
     *
     * @param json JSON object.
     * @param key Field name.
     * @param fallback Fallback value.
     * @return Parsed integer.
     */
    int json_int_or(const nlohmann::json &json, const char *key, int fallback = 0)
    {
      if (!json.is_object() || !json.contains(key))
        return fallback;

      const auto &value = json.at(key);
      if (!value.is_number_integer())
        return fallback;

      return value.get<int>();
    }

    /**
     * @brief Read an optional int64 field from JSON.
     *
     * @param json JSON object.
     * @param key Field name.
     * @param fallback Fallback value.
     * @return Parsed int64.
     */
    std::int64_t json_int64_or(
        const nlohmann::json &json,
        const char *key,
        std::int64_t fallback = 0)
    {
      if (!json.is_object() || !json.contains(key))
        return fallback;

      const auto &value = json.at(key);
      if (!value.is_number_integer())
        return fallback;

      return value.get<std::int64_t>();
    }

    /**
     * @brief Read an optional uint32 field from JSON.
     *
     * @param json JSON object.
     * @param key Field name.
     * @param fallback Fallback value.
     * @return Parsed uint32.
     */
    std::uint32_t json_uint32_or(
        const nlohmann::json &json,
        const char *key,
        std::uint32_t fallback = 0)
    {
      if (!json.is_object() || !json.contains(key))
        return fallback;

      const auto &value = json.at(key);
      if (!value.is_number_unsigned() && !value.is_number_integer())
        return fallback;

      const auto parsed = value.get<std::uint64_t>();
      if (parsed > static_cast<std::uint64_t>(UINT32_MAX))
        return fallback;

      return static_cast<std::uint32_t>(parsed);
    }

    /**
     * @brief Read an optional bool field from JSON.
     *
     * @param json JSON object.
     * @param key Field name.
     * @param fallback Fallback value.
     * @return Parsed boolean.
     */
    bool json_bool_or(const nlohmann::json &json, const char *key, bool fallback = false)
    {
      if (!json.is_object() || !json.contains(key))
        return fallback;

      const auto &value = json.at(key);
      if (!value.is_boolean())
        return fallback;

      return value.get<bool>();
    }

    /**
     * @brief Read an optional filesystem path field from JSON.
     *
     * @param json JSON object.
     * @param key Field name.
     * @return Parsed path.
     */
    std::filesystem::path json_path_or_empty(const nlohmann::json &json, const char *key)
    {
      return std::filesystem::path(json_string_or(json, key));
    }

    /**
     * @brief Convert a vector of strings to JSON.
     *
     * @param values String list.
     * @return JSON array.
     */
    nlohmann::json string_vector_to_json(const std::vector<std::string> &values)
    {
      nlohmann::json out = nlohmann::json::array();

      for (const auto &value : values)
        out.push_back(value);

      return out;
    }

    /**
     * @brief Convert JSON to a vector of strings.
     *
     * @param json JSON array.
     * @return String list.
     */
    std::vector<std::string> string_vector_from_json(const nlohmann::json &json)
    {
      std::vector<std::string> out;

      if (!json.is_array())
        return out;

      for (const auto &item : json)
      {
        if (item.is_string())
          out.push_back(item.get<std::string>());
      }

      return out;
    }

  } // namespace

  nlohmann::json replay_env_var_to_json(const ReplayEnvVar &env)
  {
    return {
        {"name", env.name},
        {"value", env.value},
    };
  }

  ReplayEnvVar replay_env_var_from_json(const nlohmann::json &json)
  {
    ReplayEnvVar env{};
    env.name = json_string_or(json, "name");
    env.value = json_string_or(json, "value");
    return env;
  }

  nlohmann::json replay_timing_to_json(const ReplayTiming &timing)
  {
    return {
        {"started_at", timing.started_at},
        {"finished_at", timing.finished_at},
        {"duration_ms", timing.duration_ms},
    };
  }

  ReplayTiming replay_timing_from_json(const nlohmann::json &json)
  {
    ReplayTiming timing{};
    timing.started_at = json_string_or(json, "started_at");
    timing.finished_at = json_string_or(json, "finished_at");
    timing.duration_ms = json_int64_or(json, "duration_ms");
    return timing;
  }

  nlohmann::json replay_process_result_to_json(const ReplayProcessResult &result)
  {
    return {
        {"exit_code", result.exit_code},
        {"raw_status", result.raw_status},
        {"terminated_by_signal", result.terminated_by_signal},
        {"signal", result.signal},
    };
  }

  ReplayProcessResult replay_process_result_from_json(const nlohmann::json &json)
  {
    ReplayProcessResult result{};
    result.exit_code = json_int_or(json, "exit_code");
    result.raw_status = json_int_or(json, "raw_status");
    result.terminated_by_signal = json_bool_or(json, "terminated_by_signal");
    result.signal = json_int_or(json, "signal");
    return result;
  }

  nlohmann::json replay_log_paths_to_json(const ReplayLogPaths &logs)
  {
    return {
        {"stdout_log", logs.stdout_log.string()},
        {"stderr_log", logs.stderr_log.string()},
        {"combined_log", logs.combined_log.string()},
    };
  }

  ReplayLogPaths replay_log_paths_from_json(const nlohmann::json &json)
  {
    ReplayLogPaths logs{};
    logs.stdout_log = json_path_or_empty(json, "stdout_log");
    logs.stderr_log = json_path_or_empty(json, "stderr_log");
    logs.combined_log = json_path_or_empty(json, "combined_log");
    return logs;
  }

  nlohmann::json replay_record_to_json(const ReplayRecord &record)
  {
    nlohmann::json env = nlohmann::json::array();

    for (const auto &item : record.env)
      env.push_back(replay_env_var_to_json(item));

    return {
        {"id", record.id},
        {"schema_version", record.schema_version},
        {"mode", to_string(record.mode)},
        {"target_kind", to_string(record.target_kind)},
        {"status", to_string(record.status)},
        {"error_kind", to_string(record.error_kind)},
        {"cwd", record.cwd.string()},
        {"project_dir", record.project_dir.string()},
        {"target_path", record.target_path.string()},
        {"command", record.command},
        {"resolved_command", record.resolved_command},
        {"vix_args", string_vector_to_json(record.vix_args)},
        {"app_args", string_vector_to_json(record.app_args)},
        {"env", env},
        {"timing", replay_timing_to_json(record.timing)},
        {"process", replay_process_result_to_json(record.process)},
        {"logs", replay_log_paths_to_json(record.logs)},
        {"error_message", record.error_message},
        {"hint", record.hint},
        {"replayable", record.replayable},
        {"watch", record.watch},
        {"direct_script", record.direct_script},
        {"cmake_fallback", record.cmake_fallback},
    };
  }

  ReplayRecord replay_record_from_json(const nlohmann::json &json)
  {
    ReplayRecord record{};

    record.id = json_string_or(json, "id");
    record.schema_version = json_uint32_or(json, "schema_version", 1);

    record.mode = replay_mode_from_string(json_string_or(json, "mode"));
    record.target_kind = replay_target_kind_from_string(json_string_or(json, "target_kind"));
    record.status = replay_status_from_string(json_string_or(json, "status"));
    record.error_kind = replay_error_kind_from_string(json_string_or(json, "error_kind"));

    record.cwd = json_path_or_empty(json, "cwd");
    record.project_dir = json_path_or_empty(json, "project_dir");
    record.target_path = json_path_or_empty(json, "target_path");

    record.command = json_string_or(json, "command");
    record.resolved_command = json_string_or(json, "resolved_command");

    if (json.is_object() && json.contains("vix_args"))
      record.vix_args = string_vector_from_json(json.at("vix_args"));

    if (json.is_object() && json.contains("app_args"))
      record.app_args = string_vector_from_json(json.at("app_args"));

    if (json.is_object() && json.contains("env") && json.at("env").is_array())
    {
      for (const auto &item : json.at("env"))
        record.env.push_back(replay_env_var_from_json(item));
    }

    if (json.is_object() && json.contains("timing"))
      record.timing = replay_timing_from_json(json.at("timing"));

    if (json.is_object() && json.contains("process"))
      record.process = replay_process_result_from_json(json.at("process"));

    if (json.is_object() && json.contains("logs"))
      record.logs = replay_log_paths_from_json(json.at("logs"));

    record.error_message = json_string_or(json, "error_message");
    record.hint = json_string_or(json, "hint");

    record.replayable = json_bool_or(json, "replayable", true);
    record.watch = json_bool_or(json, "watch");
    record.direct_script = json_bool_or(json, "direct_script");
    record.cmake_fallback = json_bool_or(json, "cmake_fallback");

    return record;
  }

  std::string replay_record_to_json_string(const ReplayRecord &record)
  {
    return replay_record_to_json(record).dump(2);
  }

  bool replay_record_from_json_string(
      const std::string &text,
      ReplayRecord &record,
      std::string &err)
  {
    try
    {
      const nlohmann::json json = nlohmann::json::parse(text);
      record = replay_record_from_json(json);
      return true;
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
  }

} // namespace vix::commands::replay
