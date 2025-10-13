// RunCommand.hpp / RunCommand.cpp
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <cstdio>
#include <algorithm>

namespace fs = std::filesystem;

namespace Vix::Commands::RunCommand
{
    namespace
    {
        struct Options
        {
            std::string appName;
            std::string preset = "dev-ninja"; // configure preset
            std::string runPreset;            // run preset (facultatif)
            std::string dir;
            int jobs = 0;
        };

        std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
        {
            auto is_opt = [](std::string_view s)
            { return !s.empty() && s.front() == '-'; };
            for (size_t i = 0; i < args.size(); ++i)
            {
                if (args[i] == "-d" || args[i] == "--dir")
                {
                    if (i + 1 < args.size() && !is_opt(args[i + 1]))
                        return args[i + 1];
                    return std::nullopt;
                }
                constexpr const char pfx[] = "--dir=";
                if (args[i].rfind(pfx, 0) == 0)
                {
                    std::string v = args[i].substr(sizeof(pfx) - 1);
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
                if (a == "--preset" && i + 1 < args.size())
                    o.preset = args[++i];
                else if (a == "--run-preset" && i + 1 < args.size())
                    o.runPreset = args[++i];
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
                else if (o.appName.empty() && !a.empty() && a != "--")
                    o.appName = a;
            }
            if (auto d = pick_dir_opt_local(args))
                o.dir = *d;
            return o;
        }

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

#ifndef _WIN32
        static std::string run_and_capture(const std::string &cmd)
        {
            std::string out;
            FILE *p = popen(cmd.c_str(), "r");
            if (!p)
                return out;
            char buf[4096];
            while (fgets(buf, sizeof(buf), p))
                out.append(buf);
            pclose(p);
            return out;
        }
#endif

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

        static std::string choose_run_preset(const fs::path &dir,
                                             const std::string &configurePreset,
                                             const std::string &userRunPreset)
        {
            auto runs = list_presets(dir, "build"); // run-* sont aussi des buildPresets avec targets=["run"]
            auto has = [&](const std::string &n)
            {
                return std::find(runs.begin(), runs.end(), n) != runs.end();
            };

            if (!userRunPreset.empty() && (runs.empty() || has(userRunPreset)))
                return userRunPreset;

            if (!runs.empty())
            {
                if (has("run-" + configurePreset))
                    return "run-" + configurePreset; // ex: run-dev-ninja (si tu crées cet alias)
                if (configurePreset.rfind("dev-", 0) == 0)
                {
                    std::string mapped = "run-" + configurePreset.substr(4); // dev-ninja → run-ninja
                    if (has(mapped))
                        return mapped;
                }
                // sinon on tentera "run-ninja" si dispo
                if (has("run-ninja"))
                    return "run-ninja";
                // fallback: un build preset existant + target run
                if (has("build-ninja"))
                    return "build-ninja";
                return runs.front();
            }

            // Heuristique si pas de liste
            if (configurePreset.rfind("dev-", 0) == 0)
                return std::string("run-") + configurePreset.substr(4);
            return "run-ninja";
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
            return cwd; // dernier recours
        }
    } // namespace

    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();
        const Options opt = parse(args);
        const fs::path cwd = fs::current_path();

        auto projectDirOpt = choose_project_dir(opt, cwd);
        if (!projectDirOpt)
        {
            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "Impossible de déterminer le dossier projet. Essayez: `vix run --dir <chemin>`.");
            return 1;
        }
        const fs::path projectDir = *projectDirOpt;

        // 1) Configure d'abord (assure que le binaire existe)
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(projectDir.string())
                << " && cmake --preset " << quote(opt.preset) << "\"";
#else
            oss << "cd " << quote(projectDir.string())
                << " && cmake --preset " << quote(opt.preset);
#endif
            const std::string cmd = oss.str();
            logger.logModule("RunCommand", Vix::Logger::Level::INFO, "Configure (preset): {}", cmd);
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                 "Échec configuration avec preset '{}' (code {}).", opt.preset, code);
                return code;
            }
        }

        // 2) Choisir run preset (ou build preset avec target run)
        const std::string runPreset = choose_run_preset(projectDir, opt.preset, opt.runPreset);
        logger.logModule("RunCommand", Vix::Logger::Level::INFO,
                         "Run preset sélectionné: {}", runPreset);

        // 3) Build + run (target run)
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(projectDir.string())
                << " && cmake --build --preset " << quote(runPreset) << " --target run";
            if (opt.jobs > 0)
                oss << " -- -j " << opt.jobs;
            oss << "\"";
#else
            oss << "cd " << quote(projectDir.string())
                << " && cmake --build --preset " << quote(runPreset) << " --target run";
            if (opt.jobs > 0)
                oss << " -- -j " << opt.jobs; // backend args
#endif
            const std::string cmd = oss.str();
            logger.logModule("RunCommand", Vix::Logger::Level::INFO, "Run (preset): {}", cmd);
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                 "Échec exécution (run preset '{}', code {}).", runPreset, code);
                return code;
            }
        }

        logger.logModule("RunCommand", Vix::Logger::Level::INFO, "🏃 Application lancée (preset: {}).", runPreset);
        return 0;
    }
}
