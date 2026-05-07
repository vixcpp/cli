/**
 *
 *  @file DevFileIndex.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Dev mode file index
 *
 */

#ifndef VIX_CLI_COMMANDS_RUN_DEV_DEV_FILE_INDEX_HPP
#define VIX_CLI_COMMANDS_RUN_DEV_DEV_FILE_INDEX_HPP

#include <vix/cli/commands/run/dev/DevChangeClassifier.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vix::commands::RunCommand::dev
{
  namespace fs = std::filesystem;

  struct DevIndexedFile
  {
    fs::path path{};
    fs::file_time_type mtime{};
    std::uintmax_t size{0};
    DevChangeKind kind{DevChangeKind::Ignore};
  };

  struct DevIndexedChange
  {
    fs::path path{};
    DevChangeKind kind{DevChangeKind::Ignore};

    bool valid() const;
  };

  class DevFileIndex
  {
  public:
    DevFileIndex() = default;

    explicit DevFileIndex(fs::path projectDir);

    const fs::path &project_dir() const;

    void reset(fs::path projectDir);

    void refresh();

    std::vector<DevIndexedChange> poll_changes();

    bool empty() const;

  private:
    fs::path projectDir_{};
    DevChangeClassifier classifier_{};
    std::unordered_map<std::string, DevIndexedFile> files_{};

    std::unordered_map<std::string, DevIndexedFile> scan_project() const;

    bool should_skip_directory(const fs::path &path) const;
    bool should_consider_file(const fs::path &path) const;

    std::optional<DevIndexedFile> read_file_state(const fs::path &path) const;

    static std::string path_key(const fs::path &path);
  };

} // namespace vix::commands::RunCommand::dev

#endif
