/**
 *
 *  @file Fs.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_FS_HPP
#define VIX_CLI_FS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::util
{
  namespace fs = std::filesystem;

  bool file_exists(const fs::path &p);
  bool dir_exists(const fs::path &p);
  bool ensure_dir(const fs::path &p, std::string &err);
  std::string read_text_file_or_empty(const fs::path &p);
  bool write_text_file_atomic(const fs::path &p, const std::string &content);
  std::optional<fs::path> find_project_root(fs::path start);
  void collect_files_recursive(
      const fs::path &root,
      const std::string &ext,
      std::vector<fs::path> &out);
  void print_log_tail_fast(const fs::path &logPath, std::size_t maxLines);
  bool executable_on_path(const std::string &exeName);

} // namespace vix::cli::util

#endif
