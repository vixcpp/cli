#pragma once

#include <filesystem>
#include <string>

namespace vix::cli::errors::rules
{
  // Returns true if it handled the runtime log (and printed a clean summary).
  bool handleUncaughtException(
      const std::string &runtimeLog,
      const std::filesystem::path &sourceFile);
} // namespace vix::cli::errors::rules
