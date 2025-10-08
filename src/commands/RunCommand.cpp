#include <vix/cli/commands/RunCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>

namespace fs = std::filesystem;

namespace Vix::Commands::RunCommand
{
    namespace
    {
        // Cherche le dossier "vix" racine en remontant depuis cwd
        std::optional<fs::path> find_vix_root(fs::path start)
        {
            start = fs::weakly_canonical(start);
            for (fs::path p = start; !p.empty(); p = p.parent_path())
            {
                // Heuristique: dossier nommé "vix" et contient "modules"
                if (p.filename() == "vix" && fs::exists(p / "modules"))
                    return p;

                // Autre cas: on est peut-être déjà dans "vix"
                if (fs::exists(p / "modules") && fs::exists(p / "modules" / "cli"))
                    if (p.filename() == "vix" || fs::exists(p / "CMakeLists.txt"))
                        return p;
            }
            return std::nullopt;
        }

        // Détecte le binaire de sortie le plus probable
        std::optional<fs::path> detect_binary(const fs::path &buildDir,
                                              const std::string &preferredName = "")
        {
            // 1) Nom exact si fourni
            if (!preferredName.empty())
            {
#ifdef _WIN32
                fs::path exe = buildDir / (preferredName + ".exe");
                if (fs::exists(exe))
                    return exe;
#endif
                fs::path bin = buildDir / preferredName;
                if (fs::exists(bin))
                    return bin;
            }

            // 2) Sinon: 1er fichier "exécutable plausible"
            for (auto &entry : fs::directory_iterator(buildDir))
            {
                if (!entry.is_regular_file())
                    continue;

                const auto ext = entry.path().extension().string();
#ifdef _WIN32
                if (ext == ".exe")
                    return entry.path();
#else
                // Pas d’extension ou .out : souvent un exécutable
                if (ext.empty() || ext == ".out")
                    return entry.path();
#endif
            }
            return std::nullopt;
        }

        // Concatène des arguments shell échappés très simplement (Unix/Windows basique)
        std::string join_args(const std::vector<std::string> &argv)
        {
            std::string out;
            for (const auto &a : argv)
            {
#ifdef _WIN32
                // En simplifié: entourer de guillemets si espace
                if (a.find(' ') != std::string::npos)
                    out += "\"" + a + "\" ";
                else
                    out += a + " ";
#else
                // Unix: entourer si espace/shell-char simples
                if (a.find_first_of(" \t\"'\\$`") != std::string::npos)
                    out += "'" + a + "' ";
                else
                    out += a + " ";
#endif
            }
            if (!out.empty() && out.back() == ' ')
                out.pop_back();
            return out;
        }
    } // namespace

    int run(const std::vector<std::string> &args)
    {
        auto &logger = Vix::Logger::getInstance();

        // Parse: [appName?] [-- appArgs...]
        std::string appName;
        std::vector<std::string> appArgs;
        {
            bool afterDashDash = false;
            for (const auto &a : args)
            {
                if (!afterDashDash && a == "--")
                {
                    afterDashDash = true;
                    continue;
                }
                if (!afterDashDash && appName.empty())
                    appName = a;
                else
                    appArgs.push_back(a);
            }
        }

        // Trouver la racine vix/
        auto vixRootOpt = find_vix_root(fs::current_path());
        if (!vixRootOpt)
        {
            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "Impossible de localiser le dossier racine 'vix/'. Lance la commande depuis le repo vix ou un sous-dossier.");
            return 1;
        }
        const fs::path vixRoot = *vixRootOpt;

        // Déterminer le projet à builder/exécuter :
        // - si appName vide → projet racine vix/
        // - si appName rempli → projet généré par `vix new` dans vix/<appName>/
        fs::path projectDir = appName.empty() ? vixRoot : (vixRoot / appName);

        // Vérifier CMakeLists.txt du projet ciblé
        if (!fs::exists(projectDir / "CMakeLists.txt"))
        {
            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "CMakeLists.txt introuvable dans '{}'.", projectDir.string());
            if (!appName.empty())
                logger.logModule("RunCommand", Vix::Logger::Level::INFO,
                                 "Astuce: crée d'abord l'app avec `vix new {}`.", appName);
            return 1;
        }

        // Dossier build du projet ciblé
        fs::path buildDir = projectDir / "build";
        std::error_code ec;
        fs::create_directories(buildDir, ec);
        if (ec)
        {
            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "Impossible de créer le dossier build: {}", ec.message());
            return 1;
        }

        logger.logModule("RunCommand", Vix::Logger::Level::INFO,
                         "Configuration CMake: {}", projectDir.string());

        // Étape 1 : cmake ..
        {
            std::string cmd = "cd \"" + buildDir.string() + "\" && cmake ..";
#ifdef _WIN32
            cmd = "cmd /C \"cd /D \"" + buildDir.string() + "\" && cmake ..\"";
#endif
            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                 "Échec de la configuration CMake (code {}).", code);
                return code;
            }
        }

        // Étape 2 : build
        {
            std::string cmd = "cd \"" + buildDir.string() + "\" && cmake --build . -j";
#ifdef _WIN32
            cmd = "cmd /C \"cd /D \"" + buildDir.string() + "\" && cmake --build . --config Release\"";
#endif
            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                 "Erreur lors de la compilation (code {}).", code);
                return code;
            }
        }

        // Étape 3 : détecter le binaire
        auto binOpt = detect_binary(buildDir, appName);
        if (!binOpt || !fs::exists(*binOpt))
        {
            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "Aucun binaire trouvé dans '{}'.", buildDir.string());
            return 1;
        }
        const fs::path bin = *binOpt;

        // Étape 4 : Exécuter (+ arguments utilisateur)
        const std::string argStr = join_args(appArgs);
        std::string runCmd;
#ifdef _WIN32
        runCmd = "cmd /C \"" + bin.string() + (argStr.empty() ? "" : " " + argStr) + "\"";
#else
        runCmd = bin.string() + (argStr.empty() ? "" : " " + argStr);
#endif

        logger.logModule("RunCommand", Vix::Logger::Level::INFO,
                         "Exécution: {}{}", bin.string(), (argStr.empty() ? "" : " " + argStr));
        return std::system(runCmd.c_str());
    }
}
