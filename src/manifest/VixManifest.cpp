#include "vix/cli/manifest/VixManifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <cstdlib>
#else
#include <unistd.h>
#endif

namespace vix::cli::manifest
{
    namespace fs = std::filesystem;

    static std::string trim(std::string s)
    {
        auto is_ws = [](unsigned char c)
        { return std::isspace(c) != 0; };

        while (!s.empty() && is_ws((unsigned char)s.front()))
            s.erase(s.begin());
        while (!s.empty() && is_ws((unsigned char)s.back()))
            s.pop_back();
        return s;
    }

    static std::string lower(std::string s)
    {
        for (auto &c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    }

    static bool starts_with(const std::string &s, const std::string &pfx)
    {
        return s.rfind(pfx, 0) == 0;
    }

    static std::optional<std::string> parse_quoted_string(const std::string &v)
    {
        std::string s = trim(v);
        if (s.size() < 2)
            return std::nullopt;

        char q = s.front();
        if (q != '"' && q != '\'')
            return std::nullopt;
        if (s.back() != q)
            return std::nullopt;

        std::string out;
        out.reserve(s.size());

        for (size_t i = 1; i + 1 < s.size(); ++i)
        {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size() - 1)
            {
                char n = s[++i];
                switch (n)
                {
                case 'n':
                    out.push_back('\n');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '"':
                    out.push_back('"');
                    break;
                case '\'':
                    out.push_back('\'');
                    break;
                default:
                    out.push_back(n);
                    break;
                }
            }
            else
                out.push_back(c);
        }
        return out;
    }

    static std::optional<bool> parse_bool(const std::string &v)
    {
        std::string s = lower(trim(v));
        if (s == "true" || s == "1" || s == "yes" || s == "on")
            return true;
        if (s == "false" || s == "0" || s == "no" || s == "off")
            return false;
        return std::nullopt;
    }

    static std::optional<int> parse_int(const std::string &v)
    {
        std::string s = trim(v);
        if (s.empty())
            return std::nullopt;
        try
        {
            size_t idx = 0;
            int x = std::stoi(s, &idx, 10);
            if (idx != s.size())
                return std::nullopt;
            return x;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    static std::optional<std::vector<std::string>> parse_string_array(const std::string &v)
    {
        std::string s = trim(v);
        if (s.size() < 2 || s.front() != '[' || s.back() != ']')
            return std::nullopt;

        s = trim(s.substr(1, s.size() - 2));
        std::vector<std::string> out;

        // empty
        if (s.empty())
            return out;

        // tokenizer: expects ["a","b"] or ['a','b'] with optional spaces.
        size_t i = 0;
        auto skip_ws = [&]()
        {
            while (i < s.size() && std::isspace((unsigned char)s[i]))
                ++i;
        };

        while (i < s.size())
        {
            skip_ws();
            if (i >= s.size())
                break;

            char q = s[i];
            if (q != '"' && q != '\'')
                return std::nullopt;

            std::string token;
            ++i;
            while (i < s.size())
            {
                char c = s[i++];
                if (c == '\\' && i < s.size())
                {
                    char n = s[i++];
                    switch (n)
                    {
                    case 'n':
                        token.push_back('\n');
                        break;
                    case 't':
                        token.push_back('\t');
                        break;
                    case '\\':
                        token.push_back('\\');
                        break;
                    case '"':
                        token.push_back('"');
                        break;
                    case '\'':
                        token.push_back('\'');
                        break;
                    default:
                        token.push_back(n);
                        break;
                    }
                    continue;
                }
                if (c == q)
                    break;
                token.push_back(c);
            }

            out.push_back(token);

            skip_ws();
            if (i >= s.size())
                break;
            if (s[i] == ',')
            {
                ++i;
                continue;
            }
            return std::nullopt;
        }

        return out;
    }

    struct RawTable
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data;
    };

    static std::optional<ManifestError> parse_ini_like(const fs::path &file, RawTable &out)
    {
        std::ifstream ifs(file);
        if (!ifs)
            return ManifestError{"Unable to open .vix file: " + file.string()};

        std::string section = "global";
        std::string line;
        size_t lineNo = 0;

        while (std::getline(ifs, line))
        {
            ++lineNo;
            std::string s = trim(line);
            if (s.empty())
                continue;
            if (starts_with(s, "#") || starts_with(s, ";"))
                continue;

            if (s.front() == '[' && s.back() == ']')
            {
                section = lower(trim(s.substr(1, s.size() - 2)));
                if (section.empty())
                    return ManifestError{"Invalid empty section at line " + std::to_string(lineNo)};
                continue;
            }

            auto eq = s.find('=');
            if (eq == std::string::npos)
                return ManifestError{"Invalid line (missing '=') at " + std::to_string(lineNo)};

            std::string key = lower(trim(s.substr(0, eq)));
            std::string val = trim(s.substr(eq + 1));

            if (key.empty())
                return ManifestError{"Invalid empty key at line " + std::to_string(lineNo)};

            out.data[section][key] = val;
        }

        return std::nullopt;
    }

    static std::optional<std::string> get(const RawTable &t, const std::string &sec, const std::string &key)
    {
        auto itS = t.data.find(sec);
        if (itS == t.data.end())
            return std::nullopt;
        auto itK = itS->second.find(key);
        if (itK == itS->second.end())
            return std::nullopt;
        return itK->second;
    }

    static std::optional<ManifestError> decode_into_manifest(const RawTable &t, Manifest &m)
    {
        // version
        if (auto v = get(t, "global", "version"))
        {
            auto vi = parse_int(*v);
            if (!vi)
                return ManifestError{"Invalid 'version' (expected int)."};
            m.version = *vi;
        }

        // app.kind
        if (auto k = get(t, "app", "kind"))
        {
            auto qs = parse_quoted_string(*k);
            std::string kk = qs ? *qs : trim(*k);
            kk = lower(kk);

            if (kk != "project" && kk != "script")
                return ManifestError{"[app] kind must be 'project' or 'script'."};
            m.appKind = kk;
        }
        else
        {
            return ManifestError{"Missing required key: [app] kind."};
        }

        if (auto n = get(t, "app", "name"))
        {
            auto qs = parse_quoted_string(*n);
            m.appName = qs ? *qs : trim(*n);
        }

        if (auto d = get(t, "app", "dir"))
        {
            auto qs = parse_quoted_string(*d);
            m.appDir = qs ? *qs : trim(*d);
            if (m.appDir.empty())
                m.appDir = ".";
        }

        if (auto e = get(t, "app", "entry"))
        {
            auto qs = parse_quoted_string(*e);
            m.appEntry = qs ? *qs : trim(*e);
        }

        if (m.appKind == "script")
        {
            if (m.appEntry.empty())
                return ManifestError{"[app] entry is required when kind='script'."};
        }

        // build
        if (auto p = get(t, "build", "preset"))
        {
            auto qs = parse_quoted_string(*p);
            m.preset = qs ? *qs : trim(*p);
        }
        if (auto rp = get(t, "build", "run_preset"))
        {
            auto qs = parse_quoted_string(*rp);
            m.runPreset = qs ? *qs : trim(*rp);
        }
        if (auto j = get(t, "build", "jobs"))
        {
            auto ji = parse_int(*j);
            if (!ji)
                return ManifestError{"Invalid [build] jobs (expected int)."};
            m.jobs = *ji;
        }
        if (auto s = get(t, "build", "san"))
        {
            auto qs = parse_quoted_string(*s);
            std::string v = lower(qs ? *qs : trim(*s));
            if (v != "off" && v != "ubsan" && v != "asan_ubsan")
                return ManifestError{"Invalid [build] san (off|ubsan|asan_ubsan)."};
            m.san = v;
        }
        if (auto f = get(t, "build", "flags"))
        {
            auto arr = parse_string_array(*f);
            if (!arr)
                return ManifestError{"Invalid [build] flags (expected [\"a\",\"b\"])."};
            m.buildFlags = *arr;
        }

        // dev
        if (auto w = get(t, "dev", "watch"))
        {
            auto b = parse_bool(*w);
            if (!b)
                return ManifestError{"Invalid [dev] watch (expected bool)."};
            m.watch = *b;
        }
        if (auto fo = get(t, "dev", "force"))
        {
            auto qs = parse_quoted_string(*fo);
            std::string v = lower(qs ? *qs : trim(*fo));
            if (v != "auto" && v != "server" && v != "script")
                return ManifestError{"Invalid [dev] force (auto|server|script)."};
            m.force = v;
        }
        if (auto c = get(t, "dev", "clear"))
        {
            auto qs = parse_quoted_string(*c);
            std::string v = lower(qs ? *qs : trim(*c));
            if (v != "auto" && v != "always" && v != "never")
                return ManifestError{"Invalid [dev] clear (auto|always|never)."};
            m.clear = v;
        }

        // logging
        if (auto lv = get(t, "logging", "level"))
        {
            auto qs = parse_quoted_string(*lv);
            m.logLevel = qs ? *qs : trim(*lv);
        }
        if (auto lf = get(t, "logging", "format"))
        {
            auto qs = parse_quoted_string(*lf);
            m.logFormat = qs ? *qs : trim(*lf);
        }
        if (auto lc = get(t, "logging", "color"))
        {
            auto qs = parse_quoted_string(*lc);
            m.logColor = qs ? *qs : trim(*lc);
        }
        if (auto nc = get(t, "logging", "no_color"))
        {
            auto b = parse_bool(*nc);
            if (!b)
                return ManifestError{"Invalid [logging] no_color (expected bool)."};
            m.noColor = *b;
        }
        if (auto q = get(t, "logging", "quiet"))
        {
            auto b = parse_bool(*q);
            if (!b)
                return ManifestError{"Invalid [logging] quiet (expected bool)."};
            m.quiet = *b;
        }
        if (auto v = get(t, "logging", "verbose"))
        {
            auto b = parse_bool(*v);
            if (!b)
                return ManifestError{"Invalid [logging] verbose (expected bool)."};
            m.verbose = *b;
        }

        // run
        if (auto a = get(t, "run", "args"))
        {
            auto arr = parse_string_array(*a);
            if (!arr)
                return ManifestError{"Invalid [run] args (expected [\"a\",\"b\"])."};
            m.runArgs = *arr;
        }
        if (auto e = get(t, "run", "env"))
        {
            auto arr = parse_string_array(*e);
            if (!arr)
                return ManifestError{"Invalid [run] env (expected [\"K=V\",\"X=1\"])."};
            m.runEnv = *arr;
        }
        if (auto ts = get(t, "run", "timeout_sec"))
        {
            auto ti = parse_int(*ts);
            if (!ti)
                return ManifestError{"Invalid [run] timeout_sec (expected int)."};
            m.timeoutSec = *ti;
        }

        return std::nullopt;
    }

    std::optional<ManifestError> load_manifest(const fs::path &file, Manifest &out)
    {
        RawTable t;
        if (auto err = parse_ini_like(file, t))
            return err;

        Manifest m{};
        if (auto err = decode_into_manifest(t, m))
            return err;

        out = std::move(m);
        return std::nullopt;
    }

    void apply_env_pairs(const std::vector<std::string> &pairs)
    {
        for (const auto &kv : pairs)
        {
            auto eq = kv.find('=');
            if (eq == std::string::npos)
                continue;

            std::string k = kv.substr(0, eq);
            std::string v = kv.substr(eq + 1);
            if (k.empty())
                continue;

#ifdef _WIN32
            _putenv_s(k.c_str(), v.c_str());
#else
            ::setenv(k.c_str(), v.c_str(), 1);
#endif
        }
    }

} // namespace vix::cli::manifest
