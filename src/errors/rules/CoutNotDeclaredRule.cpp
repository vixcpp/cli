#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class CoutNotDeclaredRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;
            return (m.find("undeclared identifier") != std::string::npos && m.find("'cout'") != std::string::npos) ||
                   (m.find("was not declared in this scope") != std::string::npos && m.find("'cout'") != std::string::npos);
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: 'cout' is not declared"
                      << RESET << "\n";

            // Show code + caret
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "'cout' belongs to the C++ standard library namespace.\n"
                      << "Include <iostream> and use std::cout.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✗ cout << \"Hello\";\n"
                      << "    ✔ #include <iostream>\n"
                      << "    ✔ std::cout << \"Hello\" << std::endl;\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";

            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeCoutNotDeclaredRule()
    {
        return std::make_unique<CoutNotDeclaredRule>();
    }
} // namespace vix::cli::errors
