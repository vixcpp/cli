#ifndef RELP_DETAIL_HPP
#define RELP_DETAIL_HPP

#include <string>
#include <cstddef>

namespace vix::cli::repl
{
    struct ReplConfig
    {
        bool enableFileHistory = true;
        std::string historyFile; // resolved at runtime (e.g. ~/.vix_history)
        std::size_t maxHistory = 2000;

        bool showBannerOnStart = true;
        bool showBannerOnClear = true;

        // Enable "calc" shortcuts:
        // - ".calc <expr>"
        // - "= <expr>"
        bool enableCalculator = true;

        // Future: completion (stubbed, no external deps yet)
        bool enableCompletion = false;
    };
}

#endif
