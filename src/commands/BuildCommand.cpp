#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace Vix::Commands::BuildCommand
{
    namespace
    {
        // ---------------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------------

        // Trouve la racine du monorepo vix/ (si on est dedans)
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
            std::string preset = "dev-ninja"; // --preset (si CMakePresets.json)
            std::string dir;                  // --dir/-d chemin explicite du projet
            int jobs = 0;                     // -j / --jobs
            bool clean = false;               // --clean
        };

        // Petit parseur pour --dir/-d si tu n’inclus pas déjà ta version utilitaire.
        // (Nom différent pour éviter l’ODR si tu as déjà une implémentation ailleurs.)
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
                {
                    o.config = args[++i];
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
                else if (a == "--target" && i + 1 < args.size())
                {
                    o.target = args[++i];
                }
                else if (a == "--generator" && i + 1 < args.size())
                {
                    o.generator = args[++i];
                }
                else if (a == "--preset" && i + 1 < args.size())
                {
                    o.preset = args[++i];
                }
                else if (a == "--clean")
                {
                    o.clean = true;
                }
                else if (o.appName.empty() && !a.empty() && a != "--")
                {
                    o.appName = a;
                }
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
            return fs::exists(projectDir / "CMakePresets.json") || fs::exists(projectDir / "CMakeUserPresets.json");
        }

        // Renvoie project dir “intelligent”
        std::optional<fs::path> choose_project_dir(const Options &opt, const fs::path &cwd)
        {
            auto canonical_if = [](const fs::path &p) -> std::optional<fs::path>
            {
                std::error_code ec;
                if (fs::exists(p / "CMakeLists.txt", ec))
                    return fs::weakly_canonical(p, ec);
                return std::nullopt;
            };

            // 1) --dir explicite prime
            if (!opt.dir.empty())
                if (auto p = canonical_if(fs::path(opt.dir)))
                    return p;

            // 2) dossier courant
            if (auto p = canonical_if(cwd))
                return p;

            // 3) appName en chemin absolu/relatif
            if (!opt.appName.empty())
            {
                if (auto p = canonical_if(fs::path(opt.appName)))
                    return p; // tel quel
                if (auto p = canonical_if(cwd / opt.appName))
                    return p;                       // relatif
                if (auto root = find_vix_root(cwd)) // monorepo vix/<app>
                    if (auto p = canonical_if(*root / opt.appName))
                        return p;
            }

            // 4) monorepo vix/ (racine)
            if (auto root = find_vix_root(cwd))
                if (auto p = canonical_if(*root))
                    return p;

            return std::nullopt;
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

        // 🔎 Choisir automatiquement le dossier projet (apps n’importe où)
        auto projectDirOpt = choose_project_dir(opt, cwd);
        if (!projectDirOpt)
        {
            logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                             "Impossible de déterminer le dossier projet.\n"
                             "Essayez: `vix build --dir <chemin>` ou lancez la commande depuis le dossier du projet.");
            return 1;
        }
        const fs::path projectDir = *projectDirOpt;

        // 0) Chemin "presets" si disponibles
        if (has_presets(projectDir))
        {
            // Configure via preset
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

            // Build via preset
            {
                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(opt.preset);
                if (!opt.target.empty())
                    oss << " --target " << quote(opt.target);
                if (!opt.config.empty())
                    oss << " --config " << quote(opt.config);
                oss << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(opt.preset);
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
                                     "Erreur compilation (preset '{}', code {}).", opt.preset, code);
                    return code;
                }
            }

            logger.logModule("BuildCommand", Vix::Logger::Level::INFO,
                             "✅ Build terminé (preset: {}).", opt.preset);
            return 0;
        }

        // 1) Fallback: pas de presets → build/ + cmake .. ; cmake --build .
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
