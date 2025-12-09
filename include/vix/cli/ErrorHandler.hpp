#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace vix::cli
{
    namespace fs = std::filesystem;

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

    /// High-level error reporting helper for the Vix CLI.
    ///
    /// The ErrorHandler is responsible for:
    ///  - Parsing raw compiler / linker logs (Clang/GCC-style).
    ///  - Detecting "known" patterns and replacing them with very friendly,
    ///    colored explanations (especially in script mode: `vix run main.cpp`).
    ///  - Falling back to a compact list of errors when no special pattern matches.
    class ErrorHandler
    {
    public:
        /// Parse a build log and print a friendly summary to stderr.
        ///
        /// This method:
        ///  - Extracts structured compiler errors (file, line, column, message).
        ///  - Handles special cases (common C++ mistakes) with educational messages.
        ///  - Optionally detects linker and sanitizer errors from the raw log.
        ///
        /// \param buildLog
        ///     Full textual output captured from the build command
        ///     (stdout + stderr).
        ///
        /// \param sourceFile
        ///     The main source file for the script or target being built.
        ///     Used only for display (e.g., `Source file: main.cpp`).
        ///
        /// \param contextMessage
        ///     Short label describing the context (defaults to "Script build failed").
        ///     This is used in generic error headers when no special case applies.
        static void printBuildErrors(
            const std::string &buildLog,
            const fs::path &sourceFile,
            const std::string &contextMessage = "Script build failed");

    private:
        /// Parse Clang/GCC-style errors from the build log.
        ///
        /// Expected format (simplified):
        ///   /path/to/file.cpp:line:column: error: message...
        ///   /path/to/file.hpp:line:column: fatal error: message...
        ///
        /// Only "error" and "fatal error" entries are returned (warnings are ignored).
        static std::vector<CompilerError> parseClangGccErrors(
            const std::string &buildLog);

        /// Print a single compiler error in a compact, human-friendly format.
        ///
        /// Typically used for generic build output (non–special-case handling),
        /// for example:
        ///
        ///   main.cpp:17:5
        ///     error: use of undeclared identifier 'foo'
        static void printSingleError(const CompilerError &err);

        /// Print small, generic hints for common error messages.
        ///
        /// Examples:
        ///  - Missing `std` namespace.
        ///  - Missing semicolon.
        ///  - No matching function for a given call.
        static void printHints(const CompilerError &err);

        /// Handle "known" error patterns with very friendly, high-level messages.
        ///
        /// This is where we turn cryptic compiler diagnostics into something like:
        ///
        ///   ❌ ERROR: Cannot print std::vector<int> using operator<<
        ///
        ///   💡 Tip:
        ///       std::cout << value;   // ❌ invalid
        ///       for (auto x : value)  // ✔ correct
        ///           std::cout << x << std::endl;
        ///
        ///   📍 Source: main.cpp:18
        ///
        /// If a pattern is recognized and a custom message is printed, this
        /// function returns true and the generic error printing pipeline is skipped.
        ///
        /// \return true if the error set has already been handled and printed
        ///         with a custom message; false otherwise.
        static bool handleSpecialCases(
            const std::vector<CompilerError> &errors,
            const fs::path &sourceFile,
            const std::string &contextMessage);
    };

} // namespace vix::cli
