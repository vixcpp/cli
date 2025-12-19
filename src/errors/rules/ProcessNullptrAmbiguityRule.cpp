#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class ProcessNullptrAmbiguityRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;

            // Clang/GCC usually:
            //   "no matching function for call to 'process'"
            // Sometimes:
            //   "call to 'process' is ambiguous"
            //   "ambiguous call to ... process"
            const bool noMatching = (m.find("no matching function for call") != std::string::npos) &&
                                    (m.find("process") != std::string::npos);

            const bool ambiguous = (m.find("ambiguous") != std::string::npos) &&
                                   (m.find("process") != std::string::npos);

            return noMatching || ambiguous;
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: ambiguous call to function 'process'"
                      << RESET << "\n";

            // Show code + caret (helps a lot here)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "The compiler cannot resolve which overload should be called.\n"
                      << "nullptr can match multiple overloads (pointer overloads, or sometimes integral conversions).\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "    process(nullptr);   // ambiguous\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✔ process(0);                 // selects int overload\n"
                      << "    ✔ process((int*)nullptr);     // selects pointer overload\n"
                      << "    ✔ process(static_cast<Foo*>(nullptr)); // select exact pointer type\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";

            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeProcessNullptrAmbiguityRule()
    {
        return std::make_unique<ProcessNullptrAmbiguityRule>();
    }
} // namespace vix::cli::errors
