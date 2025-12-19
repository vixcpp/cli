#include "vix/cli/errors/IErrorRule.hpp"
#include "vix/cli/errors/CodeFrame.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors
{
    class VectorOstreamRule final : public IErrorRule
    {
    public:
        bool match(const CompilerError &err) const override
        {
            const std::string &msg = err.message;

            // GCC:
            //   "no match for ‘operator<<’ (operand types are ‘std::ostream’ ... and ‘std::vector<int>’)"
            //
            // Clang:
            //   "invalid operands to binary expression ('std::ostream' and 'std::vector<int>')"

            const bool hasVector = (msg.find("std::vector") != std::string::npos);

            // Prefer insertion detection:
            // - GCC literally mentions operator<<
            // - Clang may not always mention operator<<, but "invalid operands" is strong
            const bool mentionsOperator =
                (msg.find("operator<<") != std::string::npos) ||
                (msg.find("operator <<") != std::string::npos);

            const bool gccNoMatch =
                (msg.find("no match for") != std::string::npos) ||
                (msg.find("no matching") != std::string::npos);

            const bool clangInvalidOperands =
                (msg.find("invalid operands") != std::string::npos) ||
                (msg.find("invalid operands to binary expression") != std::string::npos);

            // Optional extra signal (not required to avoid false negatives):
            const bool hasStream =
                (msg.find("std::ostream") != std::string::npos) ||
                (msg.find("basic_ostream") != std::string::npos);

            // IMPORTANT:
            // Avoid matching on raw "<<" because it can appear in many unrelated messages.
            // I only use explicit operator<< OR clangInvalidOperands/gccNoMatch signals.
            return hasVector && (gccNoMatch || clangInvalidOperands) && (mentionsOperator || hasStream);
        }

        bool handle(const CompilerError &err, const ErrorContext &ctx) const override
        {
            std::filesystem::path filePath(err.file);
            std::string fileName = filePath.filename().string();

            std::cerr << RED
                      << "error: cannot stream a std::vector with operator<<"
                      << RESET << "\n";

            // NEW: show code frame (1 line before/after, tabs handled, truncation)
            printCodeFrame(err, ctx);

            std::cerr << "\n"
                      << GRAY
                      << "std::vector does not provide a default operator<< overload in the standard library.\n"
                      << "You need to print its elements (or define your own overload).\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "fix:" << RESET << "\n"
                      << GRAY
                      << "    // 1) Print elements\n"
                      << "    for (const auto &x : v) {\n"
                      << "        std::cout << x << \" \";\n"
                      << "    }\n"
                      << "    std::cout << \"\\n\";\n"
                      << RESET << "\n";

            std::cerr << YELLOW << "alternative:" << RESET << "\n"
                      << GRAY
                      << "    // 2) Define an overload (example)\n"
                      << "    template <class T>\n"
                      << "    std::ostream& operator<<(std::ostream& os, const std::vector<T>& vec) {\n"
                      << "        os << \"[\";\n"
                      << "        for (size_t i = 0; i < vec.size(); ++i) {\n"
                      << "            if (i) os << \", \";\n"
                      << "            os << vec[i];\n"
                      << "        }\n"
                      << "        return os << \"]\";\n"
                      << "    }\n"
                      << RESET << "\n";

            std::cerr << GREEN << "source:" << RESET
                      << " " << fileName << ":" << err.line << ":" << err.column << "\n";

            return true;
        }
    };

    std::unique_ptr<IErrorRule> makeVectorOstreamRule()
    {
        return std::make_unique<VectorOstreamRule>();
    }
} // namespace vix::cli::errors
