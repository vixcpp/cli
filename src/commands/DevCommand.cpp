#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace vix::cli::style;

namespace vix::commands::DevCommand
{
    namespace
    {
        bool has_watch_flag(const std::vector<std::string> &args)
        {
            return std::any_of(args.begin(), args.end(),
                               [](const std::string &s)
                               {
                                   return s == "--watch" || s == "--reload";
                               });
        }
    } // namespace

    int run(const std::vector<std::string> &args)
    {
        // Forward the args as-is to `run` (without the word "dev").
        std::vector<std::string> forwarded;
        forwarded.reserve(args.size() + 1);
        forwarded.insert(forwarded.end(), args.begin(), args.end());

        // Dev mode = watch enabled by default (scripts + projects),
        //    unless the user already specified --watch / --reload.
        if (!has_watch_flag(forwarded))
        {
            forwarded.emplace_back("--watch");
        }

        info("Starting Vix dev mode.");
        hint("Tip: use `Ctrl+C` to stop dev mode; edit your files and Vix will rebuild & restart automatically.");

        // Delegate all logic to RunCommand
        return vix::commands::RunCommand::run(forwarded);
    }

    int help()
    {
        std::ostream &out = std::cout;

        out << "Usage:\n";
        out << "  vix dev [name] [options] [-- app-args...]\n\n";

        out << "Description:\n";
        out << "  Developer-friendly entrypoint that configures, builds and runs a Vix.cpp\n";
        out << "  application in dev mode with auto-reload.\n";
        out << "  Internally, this is equivalent to calling `vix run` with `--watch` enabled\n";
        out << "  (for both single-file scripts and CMake-based projects).\n\n";

        out << "Options:\n";
        out << "  --force-server           Force classification as 'dev server'\n";
        out << "  --force-script           Force classification as 'script'\n";
        out << "  --watch, --reload        Enable hot reload (enabled by default in dev)\n";
        out << "  -j, --jobs <n>           Number of parallel compile jobs\n";
        out << "  --log-level <level>      Override log verbosity (trace, debug, info, warn, error, critical)\n";
        out << "  --verbose                Shortcut for debug logs\n";
        out << "  -q, --quiet              Only show warnings and errors\n\n";

        out << "Examples:\n";
        out << "  vix dev                      # run current app with auto-reload\n";
        out << "  vix dev api                  # run 'api' app in dev mode\n";
        out << "  vix dev server.cpp           # script mode with auto-rebuild on save\n";
        out << "  vix dev server.cpp -- --port 8080\n";
        out << "  vix dev server.cpp --force-server   # treat it as a long-lived server\n";
        out << "  vix dev tool.cpp --force-script     # treat it as a CLI tool\n\n";

        return 0;
    }

} // namespace vix::commands::DevCommand
