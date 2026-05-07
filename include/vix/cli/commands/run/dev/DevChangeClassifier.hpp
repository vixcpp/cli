/**
 *
 *  @file DevChangeClassifier.hpp
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

#ifndef VIX_CLI_COMMANDS_RUN_DEV_DEV_CHANGE_CLASSIFIER_HPP
#define VIX_CLI_COMMANDS_RUN_DEV_DEV_CHANGE_CLASSIFIER_HPP

#include <filesystem>
#include <string>

namespace vix::commands::RunCommand::dev
{
  namespace fs = std::filesystem;

  enum class DevChangeKind
  {
    Ignore,
    RebuildOnly,
    ReconfigureAndRebuild
  };

  std::string to_string(DevChangeKind kind);

  class DevChangeClassifier
  {
  public:
    DevChangeClassifier() = default;

    DevChangeKind classify(
        const fs::path &projectDir,
        const fs::path &changedPath) const;

  private:
    bool should_ignore_path(
        const fs::path &projectDir,
        const fs::path &changedPath) const;

    bool is_source_file(const fs::path &path) const;
    bool is_header_file(const fs::path &path) const;
    bool is_config_file(const fs::path &path) const;
  };

} // namespace vix::commands::RunCommand::dev

#endif // VIX_CLI_COMMANDS_RUN_DEV_DEV_CHANGE_CLASSIFIER_HPP
