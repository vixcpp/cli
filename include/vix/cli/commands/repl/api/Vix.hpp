#ifndef VIX_HPP
#define VIX_HPP

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace vix::cli::repl
{
    class History;
}

namespace vix::cli::repl::api
{
    struct VixResult
    {
        int code = 0;
        std::string message;
    };

    class Vix
    {
    public:
        explicit Vix(vix::cli::repl::History *hist);

        int pid() const;
        void exit(int code = 0);
        bool exit_requested() const;
        int exit_code() const;

        std::filesystem::path cwd() const;
        VixResult cd(const std::string &path);
        VixResult mkdir(const std::string &path, bool recursive = true);
        std::optional<std::string> env(const std::string &key) const;
        const std::vector<std::string> &args() const;
        void set_args(std::vector<std::string> a);
        VixResult history();
        VixResult history_clear();

    private:
        vix::cli::repl::History *history_ = nullptr;

        bool exitRequested_ = false;
        int exitCode_ = 0;

        std::vector<std::string> args_;
    };
}

#endif
