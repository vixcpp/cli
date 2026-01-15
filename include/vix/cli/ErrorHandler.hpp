#ifndef ERROR_HANDLER_HPP
#define ERROR_HANDLER_HPP

#include <filesystem>
#include <string>

namespace vix::cli
{
    namespace fs = std::filesystem;

    class ErrorHandler
    {
    public:
        static void printBuildErrors(
            const std::string &buildLog,
            const fs::path &sourceFile,
            const std::string &contextMessage = "Script build failed");
    };
} // namespace vix::cli

#endif
