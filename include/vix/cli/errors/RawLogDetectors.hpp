#pragma once

#include <filesystem>
#include <string>

namespace vix::cli::errors
{
    class RawLogDetectors
    {
    public:
        static bool handleLinkerOrSanitizer(
            const std::string &buildLog,
            const std::filesystem::path &sourceFile,
            const std::string &contextMessage);

        static bool handleRuntimeCrash(
            const std::string &runtimeLog,
            const std::filesystem::path &sourceFile,
            const std::string &contextMessage);
    };
} // namespace vix::cli::errors