#include <vix/cli/commands/RunCommand.hpp>
#include <vix/utils/Logger.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;

namespace Vix::Commands::RunCommand
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

        // options
        struct Options
        {
            std::string appName;              // premier argument libre (facultatif)
            std::string dir;                  // --dir/-d chemin explicite du projet
            std::string preset = "dev-ninja"; // --preset (si CMakePresets.json)
            std::string config = "Release";   // --config (MSVC, multi-config)
            int jobs = 0;                     // -j/--jobs (Unix)
            std::vector<std::string> appArgs; // après "--"
        };

        // parse --dir/-d
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
            bool afterDashDash = false;
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string &a = args[i];
                if (afterDashDash)
                {
                    o.appArgs.push_back(a);
                    continue;
                }
                if (a == "--")
                {
                    afterDashDash = true;
                    continue;
                }
                if ((a == "-d" || a == "--dir") && i + 1 < args.size())
                {
                    o.dir = args[++i];
                    continue;
                }
                if (a.rfind("--dir=", 0) == 0)
                {
                    o.dir = a.substr(6);
                    continue;
                }
                if ((a == "--preset") && i + 1 < args.size())
                {
                    o.preset = args[++i];
                    continue;
                }
                if ((a == "--config") && i + 1 < args.size())
                {
                    o.config = args[++i];
                    continue;
                }
                if ((a == "-j" || a == "--jobs") && i + 1 < args.size())
                {
                    try
                    {
                        o.jobs = std::stoi(args[++i]);
                    }
                    catch (...)
                    {
                        o.jobs = 0;
                    }
                    continue;
                }
                if (o.appName.empty() && !a.empty() && a[0] != '-')
                {
                    o.appName = a;
                    continue;
                }
                // ignore le reste (extensions futures)
            }

            if (o.dir.empty())
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

        // Joindre des args shell-quotés (simple)
        std::string join_args(const std::vector<std::string> &argv)
        {
            std::string out;
            for (const auto &a : argv)
            {
#ifdef _WIN32
                if (a.find(' ') != std::string::npos)
                    out += "\"" + a + "\" ";
                else
                    out += a + " ";
#else
                if (a.find_first_of(" \t\"'\\$`") != std::string::npos)
                    out += "'" + a + "' ";
                else
                    out += a + " ";
#endif
            }
            if (!out.empty())
                out.pop_back();
            return out;
        }

        // Détecte un exécutable plausible dans un dossier
        std::optional<fs::path> detect_binary_in_dir(const fs::path &dir, const std::string &preferredName)
        {
            std::error_code ec;
            if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
                return std::nullopt;

            // 1) priorité: nom préféré
            if (!preferredName.empty())
            {
#ifdef _WIN32
                fs::path exep = dir / (preferredName + ".exe");
                if (fs::exists(exep, ec))
                    return exep;
#endif
                fs::path binp = dir / preferredName;
                if (fs::exists(binp, ec))
                    return binp;
            }

            // 2) sinon: premier fichier exécutable plausible
            for (auto &entry : fs::directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file(ec))
                    continue;

                const auto ext = entry.path().extension().string();
#ifdef _WIN32
                if (ext == ".exe")
                    return entry.path();
#else
                if (ext.empty() || ext == ".out")
                    return entry.path();
#endif
            }
            return std::nullopt;
        }

        // Essaie plusieurs répertoires de build courants pour retrouver le binaire
        std::optional<fs::path> detect_binary(const fs::path &projectDir,
                                              const std::string &preferredName,
                                              bool usedPresets,
                                              const std::string &config)
        {
            // Ordre de recherche (raisonnable pour les configs courantes)
            std::vector<fs::path> candidates;

            if (usedPresets)
            {
                candidates.push_back(projectDir / "build-ninja");
                candidates.push_back(projectDir / "build");
                candidates.push_back(projectDir / "build-msvc");
#ifdef _WIN32
                candidates.push_back(projectDir / "build-msvc" / config);
                candidates.push_back(projectDir / "build" / config);
#endif
            }
            else
            {
                candidates.push_back(projectDir / "build");
#ifdef _WIN32
                candidates.push_back(projectDir / "build" / config);
#endif
            }

            for (const auto &d : candidates)
            {
                if (auto p = detect_binary_in_dir(d, preferredName))
                    return p;
            }

            // Dernière chance: racine du projet (peu probable mais pas impossible)
            return detect_binary_in_dir(projectDir, preferredName);
        }

        // Sélection “intelligente” du dossier projet (apps n’importe où)
        std::optional<fs::path> choose_project_dir(const std::string &appName,
                                                   const std::string &dirOpt,
                                                   const fs::path &cwd)
        {
            auto canonical_if = [](const fs::path &p) -> std::optional<fs::path>
            {
                std::error_code ec;
                if (fs::exists(p / "CMakeLists.txt", ec))
                    return fs::weakly_canonical(p, ec);
                return std::nullopt;
            };

            // 1) --dir explicite
            if (!dirOpt.empty())
                if (auto p = canonical_if(fs::path(dirOpt)))
                    return p;

            // 2) dossier courant
            if (auto p = canonical_if(cwd))
                return p;

            // 3) appName en chemin absolu/relatif
            if (!appName.empty())
            {
                if (auto p = canonical_if(fs::path(appName)))
                    return p; // tel quel
                if (auto p = canonical_if(cwd / appName))
                    return p;                       // relatif
                if (auto root = find_vix_root(cwd)) // monorepo vix/<app>
                    if (auto p = canonical_if(*root / appName))
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
        auto projectDirOpt = choose_project_dir(opt.appName, opt.dir, cwd);
        if (!projectDirOpt)
        {
            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "Impossible de déterminer le dossier projet.\n"
                             "Essayez: `vix run --dir <chemin>` ou lancez la commande depuis le dossier du projet.");
            return 1;
        }
        const fs::path projectDir = *projectDirOpt;

        // Nom préféré pour le binaire: <appName> sinon nom du dossier projet
        std::string preferredName = opt.appName.empty() ? projectDir.filename().string()
                                                        : opt.appName;

        // 0) Tenter le chemin "presets" si présents
        const bool usePresets = has_presets(projectDir);
        if (usePresets)
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
                logger.logModule("RunCommand", Vix::Logger::Level::INFO, "Configure (preset): {}", cmd);
                int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                     "Échec configuration avec preset '{}' (code {}).", opt.preset, code);
                    return code;
                }
            }

            // Build via preset (tout)
            {
                std::ostringstream oss;
#ifdef _WIN32
                oss << "cmd /C \"cd /D " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(opt.preset)
                    << " --config " << quote(opt.config) << "\"";
#else
                oss << "cd " << quote(projectDir.string())
                    << " && cmake --build --preset " << quote(opt.preset);
                if (opt.jobs > 0)
                    oss << " -- -j " << opt.jobs; // backend
#endif
                const std::string cmd = oss.str();
                logger.logModule("RunCommand", Vix::Logger::Level::INFO, "Build (preset): {}", cmd);
                int code = std::system(cmd.c_str());
                if (code != 0)
                {
                    logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                     "Erreur compilation (preset '{}', code {}).", opt.preset, code);
                    return code;
                }
            }

            // Exécuter le binaire (on gère les arguments nous-mêmes)
            if (auto binOpt = detect_binary(projectDir, preferredName, /*usedPresets*/ true, opt.config))
            {
                const std::string argStr = join_args(opt.appArgs);
#ifdef _WIN32
                std::string runCmd = "cmd /C " + quote(binOpt->string() + (argStr.empty() ? "" : " " + argStr));
#else
                std::string runCmd = quote(binOpt->string()) + (argStr.empty() ? "" : " " + argStr);
#endif
                logger.logModule("RunCommand", Vix::Logger::Level::INFO,
                                 "Exécution: {}{}", binOpt->string(), (argStr.empty() ? "" : " " + argStr));
                return std::system(runCmd.c_str());
            }

            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "Binaire introuvable après build (preset '{}').", opt.preset);
            return 1;
        }

        // 1) Fallback: pas de presets → build/ + cmake .. ; cmake --build .
        fs::path buildDir = projectDir / "build";
        std::error_code ec;
        fs::create_directories(buildDir, ec);
        if (ec)
        {
            logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                             "Impossible de créer le dossier build: {}", ec.message());
            return 1;
        }

        // Configure
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string()) << " && cmake ..\"";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake ..";
#endif
            const std::string cmd = oss.str();
            logger.logModule("RunCommand", Vix::Logger::Level::INFO, "Configure: {}", cmd);
            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                 "Échec de la configuration CMake (code {}).", code);
                return code;
            }
        }

        // Build
        {
            std::ostringstream oss;
#ifdef _WIN32
            oss << "cmd /C \"cd /D " << quote(buildDir.string())
                << " && cmake --build . --config " << quote(opt.config) << "\"";
#else
            oss << "cd " << quote(buildDir.string()) << " && cmake --build .";
            if (opt.jobs > 0)
                oss << " -j " << opt.jobs;
#endif
            const std::string cmd = oss.str();
            logger.logModule("RunCommand", Vix::Logger::Level::INFO, "Build: {}", cmd);
            int code = std::system(cmd.c_str());
            if (code != 0)
            {
                logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                                 "Erreur lors de la compilation (code {}).", code);
                return code;
            }
        }

        // Détecter et exécuter le binaire
        if (auto binOpt = detect_binary(projectDir, preferredName, /*usedPresets*/ false, opt.config))
        {
            const std::string argStr = join_args(opt.appArgs);
#ifdef _WIN32
            std::string runCmd = "cmd /C " + quote(binOpt->string() + (argStr.empty() ? "" : " " + argStr));
#else
            std::string runCmd = quote(binOpt->string()) + (argStr.empty() ? "" : " " + argStr);
#endif
            logger.logModule("RunCommand", Vix::Logger::Level::INFO,
                             "Exécution: {}{}", binOpt->string(), (argStr.empty() ? "" : " " + argStr));
            return std::system(runCmd.c_str());
        }

        logger.logModule("RunCommand", Vix::Logger::Level::ERROR,
                         "Aucun binaire trouvé dans les dossiers de build.");
        return 1;
    }
}
