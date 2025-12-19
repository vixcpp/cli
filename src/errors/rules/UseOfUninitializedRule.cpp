#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class UseOfUninitializedRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;

            // Typical diagnostics:
            // - "may be used uninitialized" (GCC)
            // - "is used uninitialized" (some builds)
            // - "use of uninitialized" (clang)
            // - "uninitialized use" (varies)
            const bool mentionsUninit = (m.find("uninitialized") != std::string::npos);

            const bool strongPhrase =
                (m.find("may be used") != std::string::npos) ||
                (m.find("is used") != std::string::npos) ||
                (m.find("use of uninitialized") != std::string::npos) ||
                (m.find("uninitialized use") != std::string::npos);

            return mentionsUninit && strongPhrase;
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            const std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: use of an uninitialized value"
                      << RESET << "\n";

            // Show code + caret (often points to the read site)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "A variable is read before being given a known value.\n"
                      << "This can produce random behaviour and hard-to-debug bugs.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✗ int x;            // uninitialized\n"
                      << "      std::cout << x;   // reads garbage\n\n"
                      << "    ✔ int x = 0;        // initialized\n"
                      << "      std::cout << x;\n\n"
                      << "    ✔ prefer initialization with braces:\n"
                      << "      int y{};\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";
            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeUseOfUninitializedRule()
    {
        return std::make_unique<UseOfUninitializedRule>();
    }
} // namespace vix::cli::errors
