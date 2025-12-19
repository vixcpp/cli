#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <regex>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class UseAfterMoveRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &msg = err.message;

            // Clang commonly:
            //   "use of 'x' after it was moved"
            //
            // Some variants can contain "use of moved value" etc.
            const bool afterMoved = (msg.find("after it was moved") != std::string::npos);
            const bool useOf = (msg.find("use of") != std::string::npos);

            const bool movedValue =
                (msg.find("use of moved") != std::string::npos) ||
                (msg.find("moved-from") != std::string::npos);

            return (afterMoved && useOf) || movedValue;
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            std::string fileName = filePath.filename().string();

            std::string varName = "object";
            {
                std::regex re(R"(use of '([^']+)' after it was moved)");
                std::smatch m;
                if (std::regex_search(err.message, m, re) && m.size() >= 2)
                {
                    varName = m[1].str();
                }
            }

            std::cerr << RED
                      << "error: use-after-move"
                      << RESET << "\n";

            // Show code + caret
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "The object was moved and is now in a moved-from (empty) state.\n"
                      << "Using it after std::move(...) is undefined behaviour.\n"
                      << RESET << "\n";

            std::cerr << GRAY
                      << "    moved object: '" << varName << "'\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✔ avoid moving if you still need it\n"
                      << "        // process(" << varName << ");  // remove std::move\n\n"
                      << "    ✔ check before using it (for pointers like std::unique_ptr)\n"
                      << "        if (" << varName << ") {\n"
                      << "            " << varName << "->run();\n"
                      << "        }\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";

            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeUseAfterMoveRule()
    {
        return std::make_unique<UseAfterMoveRule>();
    }
} // namespace vix::cli::errors
