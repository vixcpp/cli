#pragma once

#include <filesystem>
#include <string>

namespace vix::cli::commands::helpers
{
    std::string quote(const std::string &s);
    bool has_cmake_cache(const std::filesystem::path &buildDir);
    std::string run_and_capture_with_code(const std::string &cmd, int &outCode);

} // namespace vix::cli::commands::helpers
