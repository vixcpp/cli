#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class ReturnLocalRefRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;
            const bool mentionsReturn = (m.find("return") != std::string::npos) || (m.find("returned") != std::string::npos);
            const bool localAddr =
                (m.find("address of local") != std::string::npos) ||
                (m.find("local variable") != std::string::npos && m.find("returned") != std::string::npos);
            const bool stackRef =
                (m.find("reference to stack") != std::string::npos) ||
                (m.find("stack memory") != std::string::npos);
            return mentionsReturn && (localAddr || stackRef);
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            const std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: returning reference/pointer to a local object"
                      << RESET << "\n";

            // Show code + caret
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "A local variable is destroyed when the function returns.\n"
                      << "Returning a reference/pointer to it produces a dangling reference.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✗ const std::string& f() {\n"
                      << "          std::string s = \"hi\";\n"
                      << "          return s;  // dangling\n"
                      << "      }\n\n"
                      << "    ✔ return by value:\n"
                      << "      std::string f() { return \"hi\"; }\n\n"
                      << "    ✔ or return a static object (careful with thread-safety)\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";
            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeReturnLocalRefRule()
    {
        return std::make_unique<ReturnLocalRefRule>();
    }
} // namespace vix::cli::errors
