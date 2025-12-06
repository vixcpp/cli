#include "vix/cli/commands/run/RunDetail.hpp"
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
    namespace fs = std::filesystem;

    // ------------------- CLI parsing -------------------

    std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
    {
        auto is_opt = [](std::string_view s)
        { return !s.empty() && s.front() == '-'; };

        for (size_t i = 0; i < args.size(); ++i)
        {
            const auto &a = args[i];

            if (a == "-d" || a == "--dir")
            {
                if (i + 1 < args.size() && !is_opt(args[i + 1]))
                    return args[i + 1];
                return std::nullopt;
            }

            constexpr const char pfx[] = "--dir=";
            if (a.rfind(pfx, 0) == 0)
            {
                std::string v = a.substr(sizeof(pfx) - 1);
                if (v.empty())
                    return std::nullopt;
                return v;
            }
        }
        return std::nullopt;
    }

    Options parse(const std::vector<std::string> &args)
    {
        Options o;

        for (size_t i = 0; i < args.size(); ++i)
        {
            const auto &a = args[i];

            // --- Options spécifiques à run ---
            if (a == "--preset" && i + 1 < args.size())
            {
                o.preset = args[++i];
            }
            else if (a == "--run-preset" && i + 1 < args.size())
            {
                o.runPreset = args[++i];
            }
            else if ((a == "-j" || a == "--jobs") && i + 1 < args.size())
            {
                try
                {
                    o.jobs = std::stoi(args[++i]);
                }
                catch (...)
                {
                    o.jobs = 0;
                }
            }
            // --- Contrôle du logging runtime ---
            else if (a == "--quiet" || a == "-q")
            {
                o.quiet = true;
            }
            else if (a == "--verbose")
            {
                o.verbose = true;
            }
            else if ((a == "--log-level" || a == "--loglevel") && i + 1 < args.size())
            {
                o.logLevel = args[++i];
            }
            else if (a.rfind("--log-level=", 0) == 0)
            {
                o.logLevel = a.substr(std::string("--log-level=").size());
            }
            // --- Arguments positionnels (appName, exampleName, etc.) ---
            else if (!a.empty() && a != "--" && a[0] != '-')
            {
                // Premier argument non-option → appName
                if (o.appName.empty())
                {
                    o.appName = a;

                    // Single-file mode: vix run main.cpp
                    std::filesystem::path p{a};
                    if (p.extension() == ".cpp")
                    {
                        o.singleCpp = true;
                        o.cppFile = std::filesystem::absolute(p);
                    }
                }
                // Cas spécial : vix run example <name>
                else if (o.appName == "example" && o.exampleName.empty())
                {
                    o.exampleName = a;
                }
            }
        }

        if (auto d = pick_dir_opt_local(args))
            o.dir = *d;

        return o;
    }

    // ------------------- Runtime exit code -------------------

    void handle_runtime_exit_code(int code, const std::string &context)
    {
        if (code == 0)
            return;

        // 130 = 128 + SIGINT → convention shell "interrupted by user"
        if (code == 130)
        {
            hint("ℹ Server interrupted by user (SIGINT).");
            return;
        }

        error(context + " (exit code " + std::to_string(code) + ").");
        hint("Check the logs above or run the command manually.");
    }

    // ------------------- Quoting -------------------

    std::string quote(const std::string &s)
    {
#ifdef _WIN32
        return "\"" + s + "\"";
#else
        if (s.find_first_of(" \t\"'\\$`") != std::string::npos)
            return "'" + s + "'";
        return s;
#endif
    }

    // ------------------- Build log analysis -------------------
#ifndef _WIN32
    bool has_real_build_work(const std::string &log)
    {
        if (log.find("Building") != std::string::npos)
            return true;
        if (log.find("Linking") != std::string::npos)
            return true;
        if (log.find("Compiling") != std::string::npos)
            return true;
        if (log.find("Scanning dependencies") != std::string::npos)
            return true;

        if (log.find("no work to do") != std::string::npos)
            return false;

        bool hasBuiltTarget = log.find("Built target") != std::string::npos;
        if (hasBuiltTarget)
        {
            return false;
        }

        return true;
    }

    std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
    {
        std::string out;
#if defined(_WIN32)
        FILE *p = _popen(cmd.c_str(), "r");
#else
        FILE *p = popen(cmd.c_str(), "r");
#endif
        if (!p)
        {
            exitCode = -1;
            return out;
        }

        char buf[4096];
        while (fgets(buf, sizeof(buf), p))
            out.append(buf);

#if defined(_WIN32)
        exitCode = _pclose(p);
#else
        exitCode = pclose(p);
#endif
        return out;
    }

    std::string run_and_capture(const std::string &cmd)
    {
        int code = 0;
        return run_and_capture_with_code(cmd, code);
    }
#else
    bool has_real_build_work(const std::string &)
    {
        return true;
    }
    std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
    {
        (void)cmd;
        exitCode = 0;
        return {};
    }
    std::string run_and_capture(const std::string &)
    {
        return {};
    }
#endif

    // ------------------- Presets & project selection -------------------

    bool has_presets(const fs::path &projectDir)
    {
        std::error_code ec;
        return fs::exists(projectDir / "CMakePresets.json", ec) ||
               fs::exists(projectDir / "CMakeUserPresets.json", ec);
    }

    static std::vector<std::string> list_presets(const fs::path &dir, const std::string &kind)
    {
#ifdef _WIN32
        (void)dir;
        (void)kind;
        return {};
#else
        std::ostringstream oss;
        oss << "cd " << quote(dir.string()) << " && cmake --list-presets=" << kind;
        auto out = run_and_capture(oss.str());
        std::vector<std::string> names;
        std::istringstream is(out);
        std::string line;
        while (std::getline(is, line))
        {
            auto q1 = line.find('\"');
            auto q2 = line.find('\"', q1 == std::string::npos ? q1 : q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                names.emplace_back(line.substr(q1 + 1, q2 - (q1 + 1)));
        }
        return names;
#endif
    }

    std::string choose_run_preset(const fs::path &dir,
                                  const std::string &configurePreset,
                                  const std::string &userRunPreset)
    {
        // Les presets "run-*" sont aussi des buildPresets avec target=["run"]
        auto runs = list_presets(dir, "build");
        auto has = [&](const std::string &n)
        {
            return std::find(runs.begin(), runs.end(), n) != runs.end();
        };

        if (!userRunPreset.empty() && (runs.empty() || has(userRunPreset)))
            return userRunPreset;

        if (!runs.empty())
        {
            // run-dev-ninja
            if (has("run-" + configurePreset))
                return "run-" + configurePreset;

            // dev-ninja → run-ninja
            if (configurePreset.rfind("dev-", 0) == 0)
            {
                std::string mapped = "run-" + configurePreset.substr(4);
                if (has(mapped))
                    return mapped;
            }

            // sinon on tentera "run-ninja"
            if (has("run-ninja"))
                return "run-ninja";

            // fallback: build-ninja, ou le premier
            if (has("build-ninja"))
                return "build-ninja";

            return runs.front();
        }

        // Heuristique si pas de liste dispo
        if (configurePreset.rfind("dev-", 0) == 0)
            return std::string("run-") + configurePreset.substr(4);
        return "run-ninja";
    }

    bool has_cmake_cache(const fs::path &buildDir)
    {
        std::error_code ec;
        return fs::exists(buildDir / "CMakeCache.txt", ec);
    }

    std::optional<fs::path> choose_project_dir(const Options &opt, const fs::path &cwd)
    {
        auto exists_cml = [](const fs::path &p)
        {
            std::error_code ec;
            return fs::exists(p / "CMakeLists.txt", ec);
        };

        if (!opt.dir.empty() && exists_cml(opt.dir))
            return fs::path(opt.dir);

        if (exists_cml(cwd))
            return cwd;

        if (!opt.appName.empty())
        {
            fs::path a = opt.appName;
            if (exists_cml(a))
                return a;
            if (exists_cml(cwd / a))
                return cwd / a;
        }

        return cwd;
    }

    // ---------------------------------------------------------------------
    // Pont CLI -> serveur: injecter VIX_LOG_LEVEL dans l'environnement
    // ---------------------------------------------------------------------
    void apply_log_level_env(const Options &opt)
    {
        // Priorité:
        //  1) --log-level <level>
        //  2) --quiet  => warn
        //  3) --verbose => debug
        //  4) sinon: ne rien toucher (on laisse VIX_LOG_LEVEL existant, ou défaut App)
        std::string level;

        if (!opt.logLevel.empty())
        {
            level = opt.logLevel;
        }
        else if (opt.quiet)
        {
            level = "warn";
        }
        else if (opt.verbose)
        {
            level = "debug";
        }

        if (level.empty())
            return;

#if defined(_WIN32)
        _putenv_s("VIX_LOG_LEVEL", level.c_str());
#else
        ::setenv("VIX_LOG_LEVEL", level.c_str(), 1);
#endif
    }

} // namespace vix::commands::RunCommand::detail
