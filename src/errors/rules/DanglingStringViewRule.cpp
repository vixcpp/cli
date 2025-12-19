#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class DanglingStringViewRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;
            const bool hasDangling = (m.find("dangling") != std::string::npos);
            const bool hasView =
                (m.find("string_view") != std::string::npos) ||
                (m.find("std::basic_string_view") != std::string::npos);
            const bool hasRef = (m.find("reference") != std::string::npos);
            return hasDangling && (hasView || hasRef);
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            const std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: dangling std::string_view (lifetime issue)"
                      << RESET << "\n";

            // Show code + caret (if the source exists)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "std::string_view does not own data.\n"
                      << "If it points to a temporary or destroyed object, it becomes invalid.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✗ std::string_view sv = std::string(\"hello\");\n"
                      << "      // temporary destroyed -> sv is dangling\n\n"
                      << "    ✔ std::string s = \"hello\";\n"
                      << "      std::string_view sv = s;\n\n"
                      << "    ✔ if you need ownership, use std::string\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";

            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeDanglingStringViewRule()
    {
        return std::make_unique<DanglingStringViewRule>();
    }
} // namespace vix::cli::errors
