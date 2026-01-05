#include "vix/cli/manifest/RunManifestMerge.hpp"

#include <filesystem>

namespace vix::cli::manifest
{
    namespace fs = std::filesystem;

    static bool has_value_str(const std::string &s)
    {
        return !s.empty();
    }

    static fs::path abs_from_manifest(const fs::path &manifestFile, const fs::path &p)
    {
        auto base = manifestFile.parent_path();
        auto out = p.is_absolute() ? p : (base / p);
        return fs::weakly_canonical(out);
    }

    Options merge_options(const Manifest &mf, const Options &cli)
    {
        Options o = cli;

        const bool cliForcesScript = cli.singleCpp;
        const fs::path manifestFile = cli.manifestFile;

        if (!cliForcesScript)
        {
            if (mf.appKind == "script")
            {
                o.singleCpp = true;

                if (!mf.appEntry.empty())
                    o.cppFile = abs_from_manifest(manifestFile, mf.appEntry);
                else
                    o.cppFile = abs_from_manifest(manifestFile, "main.cpp");
            }
            else
            {
                o.singleCpp = false;
                o.cppFile.clear();

                if (cli.dir.empty() && !mf.appDir.empty())
                    o.dir = abs_from_manifest(manifestFile, mf.appDir).string();

                if (cli.appName.empty() && !mf.appName.empty())
                    o.appName = mf.appName;

                if (!o.scriptFlags.empty())
                {
                    o.runArgs.insert(o.runArgs.end(),
                                     o.scriptFlags.begin(),
                                     o.scriptFlags.end());
                    o.scriptFlags.clear();
                }
            }
        }

        if (!has_value_str(cli.preset) && has_value_str(mf.preset))
            o.preset = mf.preset;

        if (cli.runPreset.empty() && !mf.runPreset.empty())
            o.runPreset = mf.runPreset;

        if (cli.jobs == 0 && mf.jobs > 0)
            o.jobs = mf.jobs;

        const bool cliSan = cli.enableSanitizers || cli.enableUbsanOnly;
        if (!cliSan)
        {
            if (mf.san == "ubsan")
            {
                o.enableUbsanOnly = true;
                o.enableSanitizers = false;
            }
            else if (mf.san == "asan_ubsan")
            {
                o.enableSanitizers = true;
                o.enableUbsanOnly = false;
            }
        }

        if (o.singleCpp && cli.scriptFlags.empty() && !mf.buildFlags.empty())
            o.scriptFlags = mf.buildFlags;

        if (!cli.watch && mf.watch)
            o.watch = true;

        if (!cli.forceServerLike && !cli.forceScriptLike)
        {
            if (mf.force == "server")
                o.forceServerLike = true;
            if (mf.force == "script")
                o.forceScriptLike = true;
        }

        if ((cli.clearMode.empty() || cli.clearMode == "auto") && !mf.clear.empty())
            o.clearMode = mf.clear;

        if (cli.logLevel.empty() && !mf.logLevel.empty())
            o.logLevel = mf.logLevel;

        if (cli.logFormat.empty() && !mf.logFormat.empty())
            o.logFormat = mf.logFormat;

        if (cli.logColor.empty() && !mf.logColor.empty())
            o.logColor = mf.logColor;

        if (!cli.noColor && mf.noColor)
            o.noColor = true;

        if (!cli.quiet && mf.quiet)
            o.quiet = true;

        if (!cli.verbose && mf.verbose)
            o.verbose = true;

        if (cli.runArgs.empty() && !mf.runArgs.empty())
            o.runArgs = mf.runArgs;

        if (cli.runEnv.empty() && !mf.runEnv.empty())
            o.runEnv = mf.runEnv;

        if (cli.timeoutSec == 0 && mf.timeoutSec > 0)
            o.timeoutSec = mf.timeoutSec;

        return o;
    }

} // namespace vix::cli::manifest
