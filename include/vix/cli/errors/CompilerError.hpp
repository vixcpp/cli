#pragma once

#include <string>

namespace vix::cli::errors
{
    /// Represents a single compiler error extracted from the build log.
    struct CompilerError
    {
        /// Full path to the source file reported by the compiler.
        std::string file;
        /// 1-based line number in the source file.
        int line = 0;
        /// 1-based column number in the source file.
        int column = 0;
        /// Human-readable error message (without the file:line:column prefix).
        std::string message;
        /// Raw line as seen in the compiler output (for debugging/fallbacks).
        std::string raw;
    };
} // namespace vix::cli::errors
