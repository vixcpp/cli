/**
 *
 *  @file DevChangeClassifier.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Dev mode file change classifier
 *
 */

#include <vix/cli/commands/run/dev/DevChangeClassifier.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace vix::commands::RunCommand::dev
{
  namespace
  {
    std::string normalized_generic_string(const fs::path &path)
    {
      return path.lexically_normal().generic_string();
    }

    bool path_contains_segment(
        const fs::path &path,
        const std::string &segment)
    {
      for (const auto &part : path)
      {
        if (part.string() == segment)
          return true;
      }

      return false;
    }

    bool starts_with_path(
        const fs::path &path,
        const fs::path &prefix)
    {
      const std::string pathText = normalized_generic_string(path);
      const std::string prefixText = normalized_generic_string(prefix);

      if (prefixText.empty())
        return false;

      if (pathText == prefixText)
        return true;

      return pathText.rfind(prefixText + "/", 0) == 0;
    }
  } // namespace

  std::string to_string(DevChangeKind kind)
  {
    switch (kind)
    {
    case DevChangeKind::Ignore:
      return "ignore";

    case DevChangeKind::RebuildOnly:
      return "rebuild-only";

    case DevChangeKind::ReconfigureAndRebuild:
      return "reconfigure-and-rebuild";

    default:
      return "ignore";
    }
  }

  DevChangeKind DevChangeClassifier::classify(
      const fs::path &projectDir,
      const fs::path &changedPath) const
  {
    if (changedPath.empty())
      return DevChangeKind::Ignore;

    if (should_ignore_path(projectDir, changedPath))
      return DevChangeKind::Ignore;

    if (is_config_file(changedPath))
      return DevChangeKind::ReconfigureAndRebuild;

    if (is_source_file(changedPath) || is_header_file(changedPath))
      return DevChangeKind::RebuildOnly;

    return DevChangeKind::Ignore;
  }

  bool DevChangeClassifier::should_ignore_path(
      const fs::path &projectDir,
      const fs::path &changedPath) const
  {
    const fs::path normalizedProject = projectDir.lexically_normal();
    const fs::path normalizedChanged = changedPath.lexically_normal();

    if (!normalizedProject.empty() &&
        !starts_with_path(normalizedChanged, normalizedProject))
    {
      return true;
    }

    static const std::vector<std::string> ignoredSegments = {
        ".git",
        ".vix",
        "build",
        "build-dev",
        "build-ninja",
        "build-release",
        "node_modules",
        ".cache",
        ".idea",
        ".vscode"};

    for (const auto &segment : ignoredSegments)
    {
      if (path_contains_segment(normalizedChanged, segment))
        return true;
    }

    return false;
  }

  bool DevChangeClassifier::is_source_file(const fs::path &path) const
  {
    const std::string ext = path.extension().string();

    return ext == ".cpp" ||
           ext == ".cc" ||
           ext == ".cxx" ||
           ext == ".c";
  }

  bool DevChangeClassifier::is_header_file(const fs::path &path) const
  {
    const std::string ext = path.extension().string();

    return ext == ".hpp" ||
           ext == ".hh" ||
           ext == ".hxx" ||
           ext == ".h" ||
           ext == ".ipp";
  }

  bool DevChangeClassifier::is_config_file(const fs::path &path) const
  {
    const std::string name = path.filename().string();
    const std::string ext = path.extension().string();

    if (name == "CMakeLists.txt" ||
        name == "CMakePresets.json" ||
        name == "vix.json" ||
        name == "vix.toml" ||
        name == "vix.lock")
    {
      return true;
    }

    return ext == ".cmake";
  }

} // namespace vix::commands::RunCommand::dev
