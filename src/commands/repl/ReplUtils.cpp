#include <vix/cli/commands/repl/ReplUtils.hpp>

#include <cstdlib>
#include <cctype>
#include <stdexcept>

namespace vix::cli::repl
{
    std::string trim_copy(std::string s)
    {
        auto is_space = [](unsigned char c)
        { return std::isspace(c) != 0; };

        while (!s.empty() && is_space((unsigned char)s.front()))
            s.erase(s.begin());
        while (!s.empty() && is_space((unsigned char)s.back()))
            s.pop_back();
        return s;
    }

    bool starts_with(const std::string &s, const std::string &prefix)
    {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    std::filesystem::path user_home_dir()
    {
#if defined(_WIN32)
        const char *home = std::getenv("USERPROFILE");
        if (home && *home)
            return std::filesystem::path(home);
        const char *drive = std::getenv("HOMEDRIVE");
        const char *path = std::getenv("HOMEPATH");
        if (drive && path)
            return std::filesystem::path(std::string(drive) + std::string(path));
        return std::filesystem::current_path();
#else
        const char *home = std::getenv("HOME");
        if (home && *home)
            return std::filesystem::path(home);
        return std::filesystem::current_path();
#endif
    }

    std::string make_prompt(const std::filesystem::path &cwd)
    {
        (void)cwd;
        return ">>> ";
    }

    void clear_screen()
    {
#if defined(_WIN32)
        std::system("cls");
#else
        std::system("clear");
#endif
    }

    // Quote-aware split
    std::vector<std::string> split_command_line(const std::string &line)
    {
        std::vector<std::string> out;
        std::string cur;
        cur.reserve(line.size());

        bool inQuotes = false;
        char quoteChar = '\0';
        bool escaping = false;

        auto push_cur = [&]()
        {
            if (!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
        };

        for (size_t i = 0; i < line.size(); ++i)
        {
            char c = line[i];

            if (escaping)
            {
                cur.push_back(c);
                escaping = false;
                continue;
            }

            if (c == '\\')
            {
                // allow escaping inside/outside quotes
                escaping = true;
                continue;
            }

            if (inQuotes)
            {
                if (c == quoteChar)
                {
                    inQuotes = false;
                    quoteChar = '\0';
                    continue;
                }
                cur.push_back(c);
                continue;
            }

            if (c == '"' || c == '\'')
            {
                inQuotes = true;
                quoteChar = c;
                continue;
            }

            if (std::isspace((unsigned char)c))
            {
                push_cur();
                continue;
            }

            cur.push_back(c);
        }

        push_cur();
        return out;
    }
}
