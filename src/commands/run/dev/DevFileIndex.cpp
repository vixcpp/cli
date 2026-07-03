/**
 *
 *  @file DevFileIndex.cpp
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

#include <vix/cli/commands/run/dev/DevFileIndex.hpp>

#include <algorithm>
#include <system_error>
#include <utility>

namespace vix::commands::RunCommand::dev
{
  bool DevIndexedChange::valid() const
  {
    return kind != DevChangeKind::Ignore && !path.empty();
  }

  DevFileIndex::DevFileIndex(fs::path projectDir)
      : projectDir_(std::move(projectDir))
  {
  }

  const fs::path &DevFileIndex::project_dir() const
  {
    return projectDir_;
  }

  void DevFileIndex::reset(fs::path projectDir)
  {
    projectDir_ = std::move(projectDir);
    files_.clear();
  }

  void DevFileIndex::refresh()
  {
    files_ = scan_project();
  }

  bool DevFileIndex::empty() const
  {
    return files_.empty();
  }

  std::vector<DevIndexedChange> DevFileIndex::poll_changes()
  {
    std::vector<DevIndexedChange> changes;

    auto next = scan_project();

    for (const auto &[key, nextFile] : next)
    {
      const auto oldIt = files_.find(key);

      if (oldIt == files_.end())
      {
        changes.push_back(DevIndexedChange{
            nextFile.path,
            nextFile.kind});
        continue;
      }

      const DevIndexedFile &oldFile = oldIt->second;

      if (oldFile.mtime != nextFile.mtime || oldFile.size != nextFile.size)
      {
        changes.push_back(DevIndexedChange{
            nextFile.path,
            nextFile.kind});
      }
    }

    for (const auto &[key, oldFile] : files_)
    {
      if (next.find(key) == next.end())
      {
        changes.push_back(DevIndexedChange{
            oldFile.path,
            oldFile.kind});
      }
    }

    files_ = std::move(next);

    return changes;
  }

  std::unordered_map<std::string, DevIndexedFile> DevFileIndex::scan_project() const
  {
    std::unordered_map<std::string, DevIndexedFile> out;

    std::error_code ec;

    if (projectDir_.empty())
      return out;

    if (!fs::exists(projectDir_, ec) || ec)
      return out;

    for (auto it = fs::recursive_directory_iterator(
             projectDir_,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      const fs::path path = it->path();

      if (it->is_directory())
      {
        if (should_skip_directory(path))
          it.disable_recursion_pending();

        continue;
      }

      if (!it->is_regular_file())
        continue;

      if (!should_consider_file(path))
        continue;

      std::optional<DevIndexedFile> file = read_file_state(path);
      if (!file)
        continue;

      out.emplace(path_key(file->path), std::move(*file));
    }

    return out;
  }

  bool DevFileIndex::should_skip_directory(const fs::path &path) const
  {
    const std::string name = path.filename().string();

    return name == ".git" ||
           name == ".vix" ||
           name == "build" ||
           name == "build-dev" ||
           name == "build-ninja" ||
           name == "build-release" ||
           name == "node_modules" ||
           name == ".cache" ||
           name == ".idea" ||
           name == ".vscode" ||
           name == "docs" ||
           name == "doc" ||
           name == "dist" ||
           name == "out" ||
           name == "coverage";
  }

  bool DevFileIndex::should_consider_file(const fs::path &path) const
  {
    const std::string name = path.filename().string();
    const std::string ext = path.extension().string();

    if (name == "README.md" ||
        name == "CHANGELOG.md" ||
        name == "LICENSE" ||
        name == ".gitignore")
    {
      return false;
    }

    if (name == "CMakeLists.txt" ||
        name == "CMakePresets.json" ||
        name == "vix.app" ||
        name == "vix.json" ||
        name == "vix.toml" ||
        name == "vix.lock")
    {
      return true;
    }

    return ext == ".cpp" ||
           ext == ".cc" ||
           ext == ".cxx" ||
           ext == ".c" ||
           ext == ".hpp" ||
           ext == ".hh" ||
           ext == ".hxx" ||
           ext == ".h" ||
           ext == ".ipp" ||
           ext == ".inl" ||
           ext == ".cmake";
  }

  std::optional<DevIndexedFile> DevFileIndex::read_file_state(const fs::path &path) const
  {
    std::error_code ec;

    if (!fs::exists(path, ec) || ec)
      return std::nullopt;

    if (!fs::is_regular_file(path, ec) || ec)
      return std::nullopt;

    const DevChangeKind kind = classifier_.classify(projectDir_, path);
    if (kind == DevChangeKind::Ignore)
      return std::nullopt;

    const fs::file_time_type mtime = fs::last_write_time(path, ec);
    if (ec)
      return std::nullopt;

    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec)
      return std::nullopt;

    return DevIndexedFile{
        path.lexically_normal(),
        mtime,
        size,
        kind};
  }

  std::string DevFileIndex::path_key(const fs::path &path)
  {
    return path.lexically_normal().generic_string();
  }

} // namespace vix::commands::RunCommand::dev
