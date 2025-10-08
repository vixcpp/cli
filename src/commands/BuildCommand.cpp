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
        std::optional<fs::path> find_vix_root(fs::path start)
        {
            start = fs::weakly_canonical(start);
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
            std::string appName;            // "" => build racine vix/
            std::string config = "Release"; // Debug / Release / RelWithDebInfo / MinSizeRel
            std::string target;             // cible cmake optionnelle
            std::string generator;          // "Ninja", "Unix Makefiles", etc. (optionnel)
            int jobs = 0;                   // 0 => auto
            bool clean = false;             // rm -rf build/ avant build
        };

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
                else if (a == "--clean")
                {
                    o.clean = true;
                }
                else if (o.appName.empty() && !a.empty() && a != "--")
                {
                    // premier argument libre = nom d’app
                    o.appName = a;
                }
                else
                {
                    // ignorer le reste (ou future extension)
                }
            }
            return o;
        }

        std::string quote(const std::string &s)
        {
#ifdef _WIN32
            return "\"" + s + "\"";
#else
            // simple quote pour espaces
            if (s.find_first_of(" \t\"'\\$`") != std::string::npos)
                return "'" + s + "'";
            return s;
#endif
        }
    } // namespace

    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();
        const Options opt = parse_args(args);

        // Localiser la racine vix/
        auto vixRootOpt = find_vix_root(fs::current_path());
        if (!vixRootOpt)
        {
            logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                             "Impossible de localiser le dossier racine 'vix/'. Lance la commande depuis le repo vix ou un sous-dossier.");
            return 1;
        }
        const fs::path vixRoot = *vixRootOpt;

        // Projet ciblé
        fs::path projectDir = opt.appName.empty() ? vixRoot : (vixRoot / opt.appName);
        if (!fs::exists(projectDir / "CMakeLists.txt"))
        {
            logger.logModule("BuildCommand", Vix::Logger::Level::ERROR,
                             "CMakeLists.txt introuvable dans '{}'.", projectDir.string());
            if (!opt.appName.empty())
                logger.logModule("BuildCommand", Vix::Logger::Level::INFO,
                                 "Astuce: crée d'abord l'app avec `vix new {}`.", opt.appName);
            return 1;
        }

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

        // Étape 1 : CMake configuration
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake ..";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake ..";
#endif
            if (!opt.generator.empty())
            {
                oss << " -G " << quote(opt.generator);
            }
            // Rien n’empêche d’ajouter d’autres -D ici si besoin
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

        // Étape 2 : Build
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake --build . --config " << opt.config;
            if (!opt.target.empty())
                oss << " --target " << quote(opt.target);
            // jobs non standardisés sur MSBuild, cmake les gère parfois via -- /m
            // on laisse simple ici
            oss << "\"";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
            if (!opt.target.empty())
                oss << " --target " << quote(opt.target);
            oss << " -j";
            if (opt.jobs > 0)
                oss << " " << opt.jobs;
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
