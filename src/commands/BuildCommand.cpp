#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <cstdio> // popen/pclose
#include <algorithm>

namespace fs = std::filesystem;

namespace vix::commands::BuildCommand
{
    namespace
    {
        // ---------------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------------

        std::optional<fs::path> find_vix_root(fs::path start)
        {
            std::error_code ec;
            start = fs::weakly_canonical(start, ec);
            if (ec)
                return std::nullopt;

            for (fs::path p = start; !p.empty(); p = p.parent_path())
            {
                if (p.filename() == "vix" && fs::exists(p / "modules"))
                    return p;
                if (fs::exists(p / "modules") && fs::exists(p / "modules" / "cli"))
                    if (p.filename() == "vix" || fs::exists(p / "CMakeLists.txt"))
                        return p;
            }
            return std::nullopt;
        }

        struct Options
        {
            std::string appName;              // "" => auto-détection
            std::string config = "Release";   // Debug / Release / RelWithDebInfo / MinSizeRel
            std::string target;               // --target <name>
            std::string generator;            // -G "Ninja", "Unix Makefiles", etc.
            std::string preset = "dev-ninja"; // configure preset (CMakePresets.json)
            std::string buildPreset;          // build preset (facultatif)
            std::string dir;                  // --dir/-d chemin explicite du projet
            int jobs = 0;                     // -j / --jobs
            bool clean = false;               // --clean
        };

        std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
        {
            auto is_option = [](std::string_view sv)
            { return !sv.empty() && sv.front() == '-'; };
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string &a = args[i];
                if (a == "-d" || a == "--dir")
                {
                    if (i + 1 < args.size() && !is_option(args[i + 1]))
                        return args[i + 1];
                    return std::nullopt;
                }
                constexpr const char prefix[] = "--dir=";
                if (a.rfind(prefix, 0) == 0)
                {
                    std::string val = a.substr(sizeof(prefix) - 1);
                    if (val.empty())
                        return std::nullopt;
                    return val;
                }
            }
            return std::nullopt;
        }

        Options parse_args(const std::vector<std::string> &args)
        {
            Options o;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string &a = args[i];
                if (a == "--config" && i + 1 < args.size())
                    o.config = args[++i];
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
                else if (a == "--target" && i + 1 < args.size())
                    o.target = args[++i];
                else if (a == "--generator" && i + 1 < args.size())
                    o.generator = args[++i];
                else if (a == "--preset" && i + 1 < args.size())
                    o.preset = args[++i]; // configure
                else if (a == "--build-preset" && i + 1 < args.size())
                    o.buildPreset = args[++i]; // NEW
                else if (a == "--clean")
                    o.clean = true;
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

        bool has_presets(const fs::path &projectDir)
        {
            return fs::exists(projectDir / "CMakePresets.json") ||
                   fs::exists(projectDir / "CMakeUserPresets.json");
        }

        std::optional<fs::path> choose_project_dir(const Options &opt, const fs::path &cwd)
        {
            auto canonical_if = [](const fs::path &p) -> std::optional<fs::path>
            {
                std::error_code ec;
                if (fs::exists(p / "CMakeLists.txt", ec))
                    return fs::weakly_canonical(p, ec);
                return std::nullopt;
            };

            if (!opt.dir.empty())
                if (auto p = canonical_if(fs::path(opt.dir)))
                    return p;
            if (auto p = canonical_if(cwd))
                return p;

            if (!opt.appName.empty())
            {
                if (auto p = canonical_if(fs::path(opt.appName)))
                    return p;
                if (auto p = canonical_if(cwd / opt.appName))
                    return p;
                if (auto root = find_vix_root(cwd))
                    if (auto p = canonical_if(*root / opt.appName))
                        return p;
            }

            if (auto root = find_vix_root(cwd))
                if (auto p = canonical_if(*root))
                    return p;
            return std::nullopt;
        }

        // ---- util: exécuter et capturer stdout (POSIX)
#ifndef _WIN32
        static std::string run_and_capture(const std::string &cmd)
        {
            std::string out;
            FILE *pipe = popen(cmd.c_str(), "r");
            if (!pipe)
                return out;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe))
                out.append(buf);
            pclose(pipe);
            return out;
        }
#endif

        static std::vector<std::string> list_presets(const fs::path &projectDir, const std::string &kind)
        {
#ifdef _WIN32
            // Windows: cmake --list-presets n'imprime pas aisément parsable via popen ici.
            // On retourne vide → on utilisera le mapping heuristique dev-* → build-*.
            (void)projectDir;
            (void)kind;
            return {};
#else
            std::ostringstream oss;
            oss << "cd " << quote(projectDir.string()) << " && cmake --list-presets=" << kind;
            const auto out = run_and_capture(oss.str());
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

        static std::string choose_build_preset(const fs::path &projectDir,
                                               const std::string &configurePreset,
                                               const std::string &userBuildPreset)
        {
            auto builds = list_presets(projectDir, "build");
            auto has = [&](const std::string &n)
            {
                return std::find(builds.begin(), builds.end(), n) != builds.end();
            };

            if (!userBuildPreset.empty() && (builds.empty() || has(userBuildPreset)))
                return userBuildPreset;

            if (!builds.empty())
            {
                if (has(configurePreset))
                    return configurePreset;

                // dev-* → build-*
                if (configurePreset.rfind("dev-", 0) == 0)
                {
                    std::string mapped = configurePreset;
                    mapped.replace(0, 4, "build-");
                    if (has(mapped))
                        return mapped;
                }

                // préférer un preset contenant "ninja"
                for (auto &n : builds)
                    if (n.find("ninja") != std::string::npos)
                        return n;

                // fallback: premier
                return builds.front();
            }

            // Pas de liste possible → heuristique simple
            if (configurePreset.rfind("dev-", 0) == 0)
            {
                std::string mapped = configurePreset;
                mapped.replace(0, 4, "build-");
                return mapped; // on tentera quand même
            }
            return configurePreset; // dernier recours
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Entrée principale
    // -------------------------------------------------------------------------
    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();
        const Options opt = parse_args(args);
        const fs::path cwd = fs::current_path();

        auto projectDirOpt = choose_project_dir(opt, cwd);
        if (!projectDirOpt)
        {
            logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                             "Impossible de déterminer le dossier projet.\n"
                             "Essayez: `vix build --dir <chemin>` ou lancez la commande depuis le dossier du projet.");
            return 1;
        }
        const fs::path projectDir = *projectDirOpt;

        if (has_presets(projectDir))
        {
            // 1) Configure
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
                logger.logModule("BuildCommand", Vix::Logger::Level::INFO, "Configure (preset): {}", cmd);
                const int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                                     "Échec configuration avec preset '{}' (code {}).", opt.preset, code);
                    return code;
                }
            }

            // 2) Choix build preset
            const std::string buildPreset = choose_build_preset(projectDir, opt.preset, opt.buildPreset);
            logger.logModule("BuildCommand", Vix::Logger::Level::INFO,
                             "Build preset sélectionné: {}", buildPreset);

            // 3) Build
            {
                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(buildPreset);
                if (!opt.target.empty())
                    oss << " --target " << quote(opt.target);
                if (!opt.config.empty())
                    oss << " --config " << quote(opt.config);
                oss << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(buildPreset);
                if (!opt.target.empty())
                    oss << " --target " << quote(opt.target);
                if (!opt.config.empty())
                    oss << " --config " << quote(opt.config);
                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs; // backend args
#endif
                const std::string cmd = oss.str();
                logger.logModule("BuildCommand", Vix::Logger::Level::INFO, "Build (preset): {}", cmd);
                const int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                                     "Erreur compilation (build preset '{}', code {}).", buildPreset, code);
                    return code;
                }
            }

            logger.logModule("BuildCommand", Vix::Logger::Level::INFO,
                             "✅ Build terminé (preset config: {}, preset build: {}).", opt.preset, buildPreset);
            return 0;
        }

        // --- Fallback (pas de presets) ---
        fs::path buildDir = projectDir / "build";

        if (opt.clean && fs::exists(buildDir))
        {
            std::error_code ec;
            fs::remove_all(buildDir, ec);
            if (ec)
            {
                logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                                 "Échec du nettoyage du dossier build: {}", ec.message());
                return 1;
            }
            logger.logModule("BuildCommand", Vix::Logger::Level::INFO, "Dossier build/ nettoyé.");
        }

        std::error_code ec;
        fs::create_directories(buildDir, ec);
        if (ec)
        {
            logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                             "Impossible de créer le dossier build: {}", ec.message());
            return 1;
        }

        // Configure (fallback)
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake ..";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake ..";
#endif
            if (!opt.generator.empty())
                oss << " -G " << quote(opt.generator);
#ifdef _WIN32
            oss << "\"";
#endif
            const std::string cmd = oss.str();

            logger.logModule("BuildCommand", Vix::Logger::Level::INFO, "Configure: {}", cmd);
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                                 "Échec de la configuration CMake (code {}).", code);
                return code;
            }
        }

        // Build (fallback)
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string())
                << " && cmake --build . --config " << opt.config;
            if (!opt.target.empty())
                oss << " --target " << quote(opt.target);
            oss << "\"";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
            if (!opt.target.empty())
                oss << " --target " << quote(opt.target);
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
#endif
            const std::string cmd = oss.str();

            logger.logModule("BuildCommand", Vix::Logger::Level::INFO, "Build: {}", cmd);
            const int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                                 "Erreur lors de la compilation (code {}).", code);
                return code;
            }
        }

        logger.logModule("BuildCommand", Vix::Logger::Level::INFO,
                         "✅ Build terminé pour: {}", projectDir.string());
        return 0;
    }
}
