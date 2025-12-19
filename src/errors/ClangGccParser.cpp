#include "vix/cli/errors/ClangGccParser.hpp"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <regex>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <sstream>

namespace vix::cli::errors
{
    std::vector<CompilerError> ClangGccParser::parse(const std::string &buildLog)
    {
        std::vector<CompilerError> out;

        // Example:
        // /path/main.cpp:3:5: error: use of undeclared identifier 'std'
        // /path/foo.hpp:1:10: fatal error: 'h.hpp' file not found
        std::regex re(R"((.+?):(\d+):(\d+):\s*(fatal error|error|warning):\s*(.+))");

        std::istringstream iss(buildLog);
        std::string line;
        while (std::getline(iss, line))
        {
            // Ignore make/ninja noise
            if (line.rfind("gmake", 0) == 0 ||
                line.rfind("make", 0) == 0 ||
                line.rfind("ninja", 0) == 0)
            {
                continue;
            }

            std::smatch m;
            if (std::regex_match(line, m, re))
            {
                CompilerError e;
                e.file = m[1].str();
                e.line = std::stoi(m[2].str());
                e.column = std::stoi(m[3].str());
                // m[4] = "fatal error" | "error" | "warning"
                e.message = m[5].str();
                e.raw = line;

                if (m[4].str() == "error" || m[4].str() == "fatal error")
                {
                    out.push_back(std::move(e));
                }
            }
        }

        return out;
    }
} // namespace vix::cli::errors
