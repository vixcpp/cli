#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vix::cli::manifest
{
    struct Manifest
    {
        int version = 1;

        // app
        std::string appKind; // "project" | "script"
        std::string appName;
        std::string appDir = ".";
        std::string appEntry; // for script

        // build
        std::string preset = "dev-ninja";
        std::string runPreset;
        int jobs = 0;
        std::string san = "off"; // off|ubsan|asan_ubsan
        std::vector<std::string> buildFlags;

        // dev
        bool watch = false;
        std::string force = "auto"; // auto|server|script
        std::string clear = "auto"; // auto|always|never

        // logging
        std::string logLevel;
        std::string logFormat;
        std::string logColor;
        bool noColor = false;
        bool quiet = false;
        bool verbose = false;

        // run
        std::vector<std::string> runArgs;
        std::vector<std::string> runEnv;
        int timeoutSec = 0;
    };

    struct ManifestError
    {
        std::string message;
    };

    // Load and validate. Returns error if invalid.
    std::optional<ManifestError> load_manifest(const std::filesystem::path &file, Manifest &out);
    // apply env pairs ("K=V") to current process env
    void apply_env_pairs(const std::vector<std::string> &pairs);

} // namespace vix::cli::manifest
