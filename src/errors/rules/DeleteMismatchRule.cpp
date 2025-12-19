#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class DeleteMismatchRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &m = err.message;
            const bool hasMismatch =
                (m.find("mismatched delete") != std::string::npos) ||
                (m.find("mismatched-new-delete") != std::string::npos) ||
                (m.find("mismatched new/delete") != std::string::npos);

            const bool allocWithNewArray =
                (m.find("allocated with") != std::string::npos && m.find("new[]") != std::string::npos);

            const bool deleteArrayMention =
                (m.find("delete[]") != std::string::npos) ||
                (m.find("operator delete[]") != std::string::npos);

            const bool deleteMention =
                (m.find("delete") != std::string::npos) ||
                (m.find("operator delete") != std::string::npos);

            // Patterns like:
            // - allocated with new[] + delete
            // - allocated with new + delete[]
            const bool mismatchPair =
                (m.find("allocated with") != std::string::npos && m.find("new[]") != std::string::npos && deleteMention) ||
                (m.find("allocated with") != std::string::npos && m.find("new") != std::string::npos && deleteArrayMention);

            return hasMismatch || allocWithNewArray || mismatchPair;
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            const std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: mismatched delete (delete vs delete[])"
                      << RESET << "\n";

            // Show code + caret (if possible)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "Memory allocated with new[] must be freed with delete[].\n"
                      << "Memory allocated with new must be freed with delete.\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "tip:" << RESET << "\n"
                      << GRAY
                      << "    ✗ int* a = new int[10];\n"
                      << "      delete a;           // wrong\n\n"
                      << "    ✔ int* a = new int[10];\n"
                      << "      delete[] a;         // correct\n\n"
                      << "    ✔ prefer RAII:\n"
                      << "      std::vector<int> v(10);\n"
                      << "      auto p = std::make_unique<int[]>(10);\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";
            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeDeleteMismatchRule()
    {
        return std::make_unique<DeleteMismatchRule>();
    }
} // namespace vix::cli::errors
