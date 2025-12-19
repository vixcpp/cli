#pragma once

#include <filesystem>
#include <string>

namespace vix::cli::errors
{
    struct ErrorContext
    {
        std::filesystem::path sourceFile;
        std::string contextMessage;
        std::string buildLog;
    };
} // namespace vix::cli::errors
