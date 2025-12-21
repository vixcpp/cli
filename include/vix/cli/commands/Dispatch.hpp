#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <optional>

namespace vix::cli::dispatch
{
    using Args = std::vector<std::string>;
    using RunFn = std::function<int(const Args &)>;
    using HelpFn = std::function<int()>;

    struct Entry
    {
        std::string name;
        std::string category; // "Project", "Packaging", "Info", ...
        std::string summary;  // one-liner shown in REPL help list

        RunFn run;
        HelpFn help; // must exist
    };

    class Dispatcher
    {
    public:
        Dispatcher();

        bool has(const std::string &cmd) const;

        // normal execution
        int run(const std::string &cmd, const Args &args) const;

        // help
        int help(const std::string &cmd) const;

        // list all commands (for REPL help + completion later)
        const std::unordered_map<std::string, Entry> &entries() const noexcept;

    private:
        std::unordered_map<std::string, Entry> map_;
    };

    // Global singleton accessor (optional but convenient)
    Dispatcher &global();
}
