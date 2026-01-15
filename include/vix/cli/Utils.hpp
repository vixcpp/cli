#ifndef UTILS_HPP
#define UTILS_HPP

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

    inline void write_text_file(const fs::path &p, std::string_view content)
    {
        std::error_code ec;

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

        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                fs::remove(tmp, ec);
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
        }

        fs::rename(tmp, p, ec);
        if (ec)
        {
            fs::remove(p, ec);
            ec.clear();
            fs::rename(tmp, p, ec);
            if (ec)
            {
                std::error_code ec2;
                fs::remove(tmp, ec2);
                throw std::runtime_error(
                    "Failed to move temp file to destination: " +
                    tmp.string() + " → " + p.string() + " — " + ec.message());
            }
        }
    }

    inline bool is_dir_empty(const fs::path &p) noexcept
    {
        std::error_code ec;

        if (!fs::exists(p, ec))
            return true;
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
                return std::nullopt;
            }

            const std::string prefix(longOpt);
            if (!prefix.empty() && a.rfind(prefix + "=", 0) == 0)
            {
                std::string val = a.substr(prefix.size() + 1);
                if (val.empty())
                    return std::nullopt;
                return val;
            }
        }
        return std::nullopt;
    }

} // namespace vix::cli::util

#endif
