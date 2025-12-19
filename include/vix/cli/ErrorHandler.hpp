// ============================================================
// File: modules/cli/include/vix/cli/ErrorHandler.hpp
// ============================================================
#pragma once

#include <filesystem>
#include <string>

namespace vix::cli
{
    namespace fs = std::filesystem;

    /// High-level error reporting helper for the Vix CLI.
    ///
    /// Responsibilities:
    ///  - Parse raw compiler / linker logs (Clang/GCC-style).
    ///  - Detect "known" patterns and show friendly, colored explanations.
    ///  - Fall back to compact error list when no special pattern matches.
    class ErrorHandler
    {
    public:
        /// Parse a build log and print a friendly summary to stderr.
        static void printBuildErrors(
            const std::string &buildLog,
            const fs::path &sourceFile,
            const std::string &contextMessage = "Script build failed");
    };
} // namespace vix::cli
