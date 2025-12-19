#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class MissingSemicolonRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;

            // GCC example:
            //   "expected ‘,’ or ‘;’ before ‘std’"
            //
            // Clang example:
            //   "expected ';' after expression"
            //
            // We match "expected" + a semicolon token / wording, avoiding too-specific patterns.
            const bool hasExpected = (m.find("expected") != std::string::npos);

            const bool hasSemicolonToken =
                (m.find("';'") != std::string::npos) ||                                      // common
                (m.find("‘;’") != std::string::npos) ||                                      // GCC fancy quotes
                (m.find(";") != std::string::npos && m.find("before") != std::string::npos); // fallback for "before" cases

            const bool mentionsSemicolonWord =
                (m.find("semicolon") != std::string::npos);

            return hasExpected && (hasSemicolonToken || mentionsSemicolonWord);
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: missing ';' at the end of the statement"
                      << RESET << "\n";

            // Show the code (1 line before/after, caret aligned, truncated if too long)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "C++ requires a semicolon at the end of most statements.\n"
                      << "This error often means the previous line is missing ';'.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    int x = 42      // ✗ missing ';'\n"
                      << "    int x = 42;     // ✔ fixed\n\n"
                      << "Check the previous line (or two) for a missing semicolon.\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";

            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeMissingSemicolonRule()
    {
        return std::make_unique<MissingSemicolonRule>();
    }
} // namespace vix::cli::errors
