#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace vix::cli::util
{
    namespace fs = std::filesystem;

    // Écrit un fichier texte de manière sûre : fichier temporaire puis renommage.
    // - Crée les répertoires parents si nécessaires.
    // - Utilise un fichier .tmp dans le même dossier pour éviter les problèmes de volume.
    // - Tente un remplacement atomique (selon l’OS / FS).
    inline void write_text_file(const fs::path &p, std::string_view content)
    {
        std::error_code ec;

        // 1) Assure l'existence du parent (si applicable)
        const fs::path parent = p.parent_path();
        if (!parent.empty())
        {
            fs::create_directories(parent, ec);
            if (ec)
            {
                throw std::runtime_error(
                    "Cannot create directories for: " + parent.string() +
                    " — " + ec.message());
            }
        }

        // 2) Fichier temporaire à côté de la cible (avec quelques tentatives)
        auto make_tmp_name = [&]()
        {
            std::mt19937_64 rng{std::random_device{}()};
            auto rnd = rng();
            return p.string() + ".tmp-" + std::to_string(rnd);
        };

        fs::path tmp;
        for (int tries = 0; tries < 3; ++tries)
        {
            tmp = make_tmp_name();
            if (!fs::exists(tmp, ec))
                break;
            if (tries == 2)
            {
                throw std::runtime_error(
                    "Cannot generate unique temp file near: " + p.string());
            }
        }

        // 3) Écriture (binaire) + flush + close
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                fs::remove(tmp, ec); // best-effort cleanup
                throw std::runtime_error(
                    "Cannot open temp file for write: " + tmp.string());
            }

            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!ofs)
            {
                ofs.close();
                fs::remove(tmp, ec);
                throw std::runtime_error("Failed to write file: " + tmp.string());
            }

            ofs.flush();
            if (!ofs)
            {
                ofs.close();
                fs::remove(tmp, ec);
                throw std::runtime_error("Failed to flush file: " + tmp.string());
            }
            // fermeture via le destructeur
        }

        // 4) Rename → p (tentative atomique). Sous Windows, si p existe, rename peut échouer.
        fs::rename(tmp, p, ec);
        if (ec)
        {
            // Essaye de supprimer la cible existante puis renommer à nouveau
            fs::remove(p, ec); // ignorer l'erreur (si absent / verrouillé)
            ec.clear();
            fs::rename(tmp, p, ec);
            if (ec)
            {
                std::error_code ec2;
                fs::remove(tmp, ec2); // éviter les orphelins
                throw std::runtime_error(
                    "Failed to move temp file to destination: " +
                    tmp.string() + " → " + p.string() + " — " + ec.message());
            }
        }
    }

    // Renvoie true si le répertoire est vide ou n'existe pas; false sinon.
    // N'émet **pas** d'exception : utile dans les checks préalables.
    inline bool is_dir_empty(const fs::path &p) noexcept
    {
        std::error_code ec;

        if (!fs::exists(p, ec))
            return true; // inexistant = considéré vide
        if (ec)
            return false;
        if (!fs::is_directory(p, ec))
            return false;
        if (ec)
            return false;

        fs::directory_iterator it(p, ec);
        if (ec)
            return false;
        return (it == fs::directory_iterator{});
    }

    // Récupère la valeur de --dir / -d si présente.
    // Supporte: "-d PATH", "--dir PATH", et "--dir=PATH".
    // Évite de prendre une autre option comme valeur (ex: "-d --flag").
    inline std::optional<std::string> pick_dir_opt(
        const std::vector<std::string> &args,
        std::string_view shortOpt = "-d",
        std::string_view longOpt = "--dir")
    {
        auto is_option = [](std::string_view sv)
        {
            return !sv.empty() && sv.front() == '-';
        };

        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::string &a = args[i];

            if (a == shortOpt || a == longOpt)
            {
                if (i + 1 < args.size() && !is_option(args[i + 1]))
                {
                    return args[i + 1];
                }
                // option sans valeur → ignorée (caller gère le défaut)
                return std::nullopt;
            }

            // format --dir=/chemin
            const std::string prefix(longOpt);
            if (!prefix.empty() && a.rfind(prefix + "=", 0) == 0)
            {
                std::string val = a.substr(prefix.size() + 1);
                // autoriser --dir="" (revient à std::nullopt)
                if (val.empty())
                    return std::nullopt;
                return val;
            }
        }
        return std::nullopt;
    }

} // namespace vix::cli::util
