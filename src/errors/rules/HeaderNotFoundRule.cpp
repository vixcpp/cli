#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class HeaderNotFoundRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;

            // Clang/GCC typical:
            //   "fatal error: 'x.hpp' file not found"
            //   "fatal error: x.hpp: No such file or directory"
            //
            // We support .hpp/.h/.hh/.hxx/.hpp and also "No such file" messages.
            const bool hasNotFound =
                (m.find("file not found") != std::string::npos) ||
                (m.find("No such file or directory") != std::string::npos);

            const bool looksLikeHeader =
                (m.find(".hpp") != std::string::npos) ||
                (m.find(".hh") != std::string::npos) ||
                (m.find(".hxx") != std::string::npos) ||
                (m.find(".h") != std::string::npos);

            return hasNotFound && looksLikeHeader;
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: header file not found"
                      << RESET << "\n";

            // Show code + caret (usually points to the #include line)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "The compiler could not locate one of the included headers.\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "    " << err.message << "\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    • Check the include name and path.\n"
                      << "    • Verify target_include_directories(...) in CMake.\n"
                      << "    • Ensure the header exists in your project tree.\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";

            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeHeaderNotFoundRule()
    {
        return std::make_unique<HeaderNotFoundRule>();
    }
} // namespace vix::cli::errors
