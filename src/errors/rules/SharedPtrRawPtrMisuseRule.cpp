#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class SharedPtrRawPtrMisuseRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;

            const bool mentionsShared =
                (m.find("std::shared_ptr") != std::string::npos) ||
                (m.find("shared_ptr") != std::string::npos);

            // Match known warning/error phrases across toolchains/lints
            const bool rawPtrMisuse =
                (m.find("constructed from raw pointer") != std::string::npos) ||
                (m.find("construction from raw pointer") != std::string::npos) ||
                (m.find("double delete") != std::string::npos) ||
                (m.find("double-delete") != std::string::npos) ||
                (m.find("may lead to") != std::string::npos && m.find("delete") != std::string::npos) ||
                (m.find("will be deleted") != std::string::npos && m.find("shared_ptr") != std::string::npos);

            return mentionsShared && rawPtrMisuse;
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            const std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: std::shared_ptr raw-pointer ownership misuse"
                      << RESET << "\n";

            // Show code + caret if available (often points to shared_ptr construction)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "A shared_ptr must own a pointer exactly once.\n"
                      << "Creating multiple shared_ptr from the same raw pointer can cause double-delete.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✗ T* p = new T();\n"
                      << "      std::shared_ptr<T> a(p);\n"
                      << "      std::shared_ptr<T> b(p);   // double-delete risk\n\n"
                      << "    ✔ auto a = std::make_shared<T>();\n"
                      << "    ✔ auto b = a;                // share ownership safely\n\n"
                      << "    ✔ if you already have a shared_ptr, never wrap its raw pointer again\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";
            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeSharedPtrRawPtrMisuseRule()
    {
        return std::make_unique<SharedPtrRawPtrMisuseRule>();
    }
} // namespace vix::cli::errors
