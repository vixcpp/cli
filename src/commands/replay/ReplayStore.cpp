/**
 *
 *  @file ReplayStore.cpp
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
#include <vix/cli/commands/replay/ReplayStore.hpp>
#include <vix/cli/commands/replay/ReplayJson.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Write text to a file.
     *
     * @param path Target file path.
     * @param text Text content.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool write_text_file(const fs::path &path, const std::string &text, std::string &err)
    {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);

      if (ec)
      {
        err = ec.message();
        return false;
      }

      std::ofstream out(path, std::ios::binary);
      if (!out)
      {
        err = "cannot open file for writing: " + path.string();
        return false;
      }

      out.write(text.data(), static_cast<std::streamsize>(text.size()));

      if (!out.good())
      {
        err = "cannot write file: " + path.string();
        return false;
      }

      return true;
    }

    /**
     * @brief Read a full text file.
     *
     * @param path Source file path.
     * @param text Output text.
     * @param err Error message written on failure.
     * @return true on success.
     */
    bool read_text_file(const fs::path &path, std::string &text, std::string &err)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
      {
        err = "cannot open file for reading: " + path.string();
        return false;
      }

      std::ostringstream oss;
      oss << in.rdbuf();
      text = oss.str();

      if (!in.good() && !in.eof())
      {
        err = "cannot read file: " + path.string();
        return false;
      }

      return true;
    }

    /**
     * @brief Trim whitespace from a string.
     *
     * @param value Input string.
     * @return Trimmed string.
     */
    std::string trim_copy(std::string value)
    {
      auto is_space = [](unsigned char c)
      {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
      };

      while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
        value.pop_back();

      std::size_t start = 0;
      while (start < value.size() && is_space(static_cast<unsigned char>(value[start])))
        ++start;

      if (start > 0)
        value.erase(0, start);

      return value;
    }

    /**
     * @brief Convert a full replay record to a lightweight list entry.
     *
     * @param record Replay record.
     * @param paths Replay paths.
     * @return Replay list entry.
     */
    ReplayListEntry make_entry_from_record(
        const ReplayRecord &record,
        const ReplayRunPaths &paths)
    {
      ReplayListEntry entry{};

      entry.id = record.id;
      entry.run_dir = paths.run_dir;
      entry.record_file = paths.record_file;
      entry.status = record.status;
      entry.mode = record.mode;
      entry.target_kind = record.target_kind;
      entry.command = record.command;
      entry.started_at = record.timing.started_at;
      entry.duration_ms = record.timing.duration_ms;

      return entry;
    }

    /**
     * @brief Return true when a directory name can be treated as a replay id.
     *
     * @param dir Directory path.
     * @return true when the directory name is a safe replay id.
     */
    bool is_replay_run_dir_name(const fs::path &dir)
    {
      return is_safe_replay_id(dir.filename().string());
    }

  } // namespace

  bool save_replay_record(
      const fs::path &baseDir,
      const ReplayRecord &record,
      std::string &err)
  {
    if (!is_safe_replay_id(record.id))
    {
      err = "unsafe replay id: " + record.id;
      return false;
    }

    ReplayRunPaths paths = make_replay_run_paths(baseDir, record.id);

    if (!ensure_replay_run_dir(paths, err))
      return false;

    const std::string json = replay_record_to_json_string(record);

    if (!write_text_file(paths.record_file, json + "\n", err))
      return false;

    return write_latest_replay_id(baseDir, record.id, err);
  }

  bool load_replay_record(
      const fs::path &baseDir,
      const std::string &id,
      ReplayRecord &record,
      std::string &err)
  {
    if (!is_safe_replay_id(id))
    {
      err = "unsafe replay id: " + id;
      return false;
    }

    const ReplayRunPaths paths = make_replay_run_paths(baseDir, id);

    std::string text;
    if (!read_text_file(paths.record_file, text, err))
      return false;

    if (!replay_record_from_json_string(text, record, err))
      return false;

    if (record.id.empty())
      record.id = id;

    return true;
  }

  bool load_latest_replay_record(
      const fs::path &baseDir,
      ReplayRecord &record,
      std::string &err)
  {
    const auto id = read_latest_replay_id(baseDir, err);
    if (!id)
      return false;

    return load_replay_record(baseDir, *id, record, err);
  }

  std::optional<std::string> read_latest_replay_id(
      const fs::path &baseDir,
      std::string &err)
  {
    const fs::path latest = replay_latest_file(baseDir);

    std::error_code ec;
    if (!fs::exists(latest, ec) || ec)
    {
      err = "no replay runs found. Run `vix run` or `vix dev` first.";
      return std::nullopt;
    }

    std::string text;
    if (!read_text_file(latest, text, err))
      return std::nullopt;

    const std::string id = trim_copy(text);

    if (!is_safe_replay_id(id))
    {
      err = "latest replay id is invalid: " + id;
      return std::nullopt;
    }

    return id;
  }

  bool write_latest_replay_id(
      const fs::path &baseDir,
      const std::string &id,
      std::string &err)
  {
    if (!is_safe_replay_id(id))
    {
      err = "unsafe replay id: " + id;
      return false;
    }

    if (!ensure_replay_root(baseDir, err))
      return false;

    return write_text_file(replay_latest_file(baseDir), id + "\n", err);
  }

  std::vector<ReplayListEntry> list_replay_runs(
      const fs::path &baseDir,
      std::size_t limit,
      std::string &err)
  {
    std::vector<ReplayListEntry> entries;

    const fs::path root = replay_runs_root(baseDir);

    std::error_code ec;
    if (!fs::exists(root, ec))
      return entries;

    if (ec)
    {
      err = ec.message();
      return entries;
    }

    for (const auto &item : fs::directory_iterator(root, ec))
    {
      if (ec)
      {
        err = ec.message();
        break;
      }

      if (!item.is_directory(ec) || ec)
        continue;

      const fs::path runDir = item.path();

      if (!is_replay_run_dir_name(runDir))
        continue;

      const std::string id = runDir.filename().string();

      ReplayRecord record{};
      std::string loadErr;
      if (!load_replay_record(baseDir, id, record, loadErr))
        continue;

      entries.push_back(make_entry_from_record(record, make_replay_run_paths(baseDir, id)));
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const ReplayListEntry &a, const ReplayListEntry &b)
        {
          return a.id > b.id;
        });

    if (limit > 0 && entries.size() > limit)
      entries.resize(limit);

    return entries;
  }

  std::vector<ReplayListEntry> list_failed_replay_runs(
      const fs::path &baseDir,
      std::size_t limit,
      std::string &err)
  {
    std::vector<ReplayListEntry> failed;

    const auto entries = list_replay_runs(baseDir, 0, err);

    for (const auto &entry : entries)
    {
      ReplayRecord record{};
      std::string loadErr;

      if (!load_replay_record(baseDir, entry.id, record, loadErr))
        continue;

      if (!is_failed(record))
        continue;

      failed.push_back(entry);

      if (limit > 0 && failed.size() >= limit)
        break;
    }

    return failed;
  }

  bool delete_replay_run(
      const fs::path &baseDir,
      const std::string &id,
      std::string &err)
  {
    if (!is_safe_replay_id(id))
    {
      err = "unsafe replay id: " + id;
      return false;
    }

    const fs::path dir = replay_run_dir(baseDir, id);

    std::error_code ec;
    fs::remove_all(dir, ec);

    if (ec)
    {
      err = ec.message();
      return false;
    }

    return true;
  }

  bool clear_replay_runs(
      const fs::path &baseDir,
      std::string &err)
  {
    const fs::path root = replay_runs_root(baseDir);

    std::error_code ec;
    if (!fs::exists(root, ec))
      return true;

    if (ec)
    {
      err = ec.message();
      return false;
    }

    for (const auto &item : fs::directory_iterator(root, ec))
    {
      if (ec)
      {
        err = ec.message();
        return false;
      }

      fs::remove_all(item.path(), ec);

      if (ec)
      {
        err = ec.message();
        return false;
      }
    }

    if (!ensure_replay_root(baseDir, err))
      return false;

    return true;
  }

} // namespace vix::commands::replay
