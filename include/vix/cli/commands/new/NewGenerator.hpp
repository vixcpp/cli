#pragma once

#include <filesystem>
#include <string>

namespace vix::commands::new_cmd::generator
{
  namespace fs = std::filesystem;

  bool dir_exists(const fs::path &path);
  bool dir_is_empty(const fs::path &path);
  bool ensure_dir(const fs::path &path, std::string &diagnostic);
  bool is_dot_path(const std::string &value);
  std::string current_dir_name();

  bool generate_native_project(
      const fs::path &projectDir,
      const std::string &projectName,
      std::string &diagnostic);
} // namespace vix::commands::new_cmd::generator
