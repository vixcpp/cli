#include "vix/cli/commands/run/RunDetail.hpp"
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <cctype>
#include <fstream>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
    namespace fs = std::filesystem;

    std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
    {
        auto is_opt = [](std::string_view s)
        { return !s.empty() && s.front() == '-'; };

        for (size_t i = 0; i < args.size(); ++i)
        {
            const auto &a = args[i];

            if (a == "-d" || a == "--dir")
            {
                if (i + 1 < args.size() && !is_opt(args[i + 1]))
                    return args[i + 1];
                return std::nullopt;
            }

            constexpr const char pfx[] = "--dir=";
            if (a.rfind(pfx, 0) == 0)
            {
                std::string v = a.substr(sizeof(pfx) - 1);
                if (v.empty())
                    return std::nullopt;
                return v;
            }
        }
        return std::nullopt;
    }

    std::filesystem::path manifest_entry_cpp(const std::filesystem::path &manifestFile)
    {
        namespace fs = std::filesystem;

        const fs::path root = manifestFile.parent_path();

        auto abs_if_exists = [&](fs::path p) -> std::optional<fs::path>
        {
            std::error_code ec;
            if (p.is_relative())
                p = root / p;
            p = fs::weakly_canonical(p, ec);
            if (ec)
                p = fs::absolute(p);

            if (fs::exists(p, ec) && !ec)
                return p;

            return std::nullopt;
        };

        auto trim = [](std::string s) -> std::string
        {
            auto is_space = [](unsigned char c)
            { return std::isspace(c) != 0; };

            while (!s.empty() && is_space((unsigned char)s.front()))
                s.erase(s.begin());
            while (!s.empty() && is_space((unsigned char)s.back()))
                s.pop_back();
            return s;
        };

        auto strip_quotes = [&](std::string s) -> std::string
        {
            s = trim(std::move(s));
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr(1, s.size() - 2);
            return s;
        };

        // Better fallbacks for typical layouts
        const fs::path fallback1 = root / "main.cpp";
        const fs::path fallback2 = root / "src" / "main.cpp";

        // If manifest missing, best-effort fallback
        {
            std::error_code ec;
            if (!fs::exists(manifestFile, ec) || ec)
            {
                if (auto p = abs_if_exists(fallback2))
                    return *p;
                if (auto p = abs_if_exists(fallback1))
                    return *p;
                return fs::absolute(fallback2); // last resort (even if missing)
            }
        }

        // Parse minimal: find `entry = "..."` anywhere (ignore comments, spaces)
        std::ifstream in(manifestFile.string());
        std::string line;

        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == '#')
                continue;

            // drop inline comment: foo = "bar" # comment
            if (auto hash = line.find('#'); hash != std::string::npos)
                line = trim(line.substr(0, hash));

            // accept: entry = "main.cpp" or entry=main.cpp
            // also accept: entry : "main.cpp" (nice-to-have)
            if (line.rfind("entry", 0) != 0)
                continue;

            auto pos_eq = line.find('=');
            auto pos_cl = line.find(':');
            auto pos = (pos_eq == std::string::npos) ? pos_cl : pos_eq;
            if (pos == std::string::npos)
                continue;

            std::string rhs = strip_quotes(line.substr(pos + 1));
            if (rhs.empty())
                continue;

            if (auto p = abs_if_exists(fs::path(rhs)))
                return *p;
        }

        // Default fallbacks
        if (auto p = abs_if_exists(fallback2))
            return *p;
        if (auto p = abs_if_exists(fallback1))
            return *p;

        // last resort: keep consistent output
        return fs::absolute(fallback2);
    }

    Options parse(const std::vector<std::string> &args)
    {
        Options o;

        for (size_t i = 0; i < args.size(); ++i)
        {
            const auto &a = args[i];

            if (a == "--")
            {
                for (size_t j = i + 1; j < args.size(); ++j)
                    o.scriptFlags.push_back(args[j]);
                break;
            }

            if (a == "--preset" && i + 1 < args.size())
            {
                o.preset = args[++i];
            }
            else if (a == "--run-preset" && i + 1 < args.size())
            {
                o.runPreset = args[++i];
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
            else if (a == "--quiet" || a == "-q")
            {
                o.quiet = true;
            }
            else if (a == "--verbose")
            {
                o.verbose = true;
            }
            else if ((a == "--log-level" || a == "--loglevel") && i + 1 < args.size())
            {
                o.logLevel = args[++i];
            }
            else if (a.rfind("--log-level=", 0) == 0)
            {
                o.logLevel = a.substr(std::string("--log-level=").size());
            }
            else if (a == "--log-format" && i + 1 < args.size())
            {
                o.logFormat = args[++i];
            }
            else if (a.rfind("--log-format=", 0) == 0)
            {
                o.logFormat = a.substr(std::string("--log-format=").size());
            }
            else if (a == "--log-color" && i + 1 < args.size())
            {
                o.logColor = args[++i]; // auto|always|never
            }
            else if (a.rfind("--log-color=", 0) == 0)
            {
                o.logColor = a.substr(std::string("--log-color=").size());
            }
            else if (a == "--no-color")
            {
                o.noColor = true;
            }

            else if (a == "--watch" || a == "--reload")
            {
                o.watch = true;
            }
            else if (a == "--force-server")
            {
                o.forceServerLike = true;
            }
            else if (a == "--force-script")
            {
                o.forceScriptLike = true;
            }

            else if (a == "--san")
            {
                o.enableSanitizers = true;
                o.enableUbsanOnly = false;
            }
            else if (a == "--ubsan")
            {
                o.enableUbsanOnly = true;
                o.enableSanitizers = false;
            }

            else if (a == "--clear" && i + 1 < args.size())
            {
                o.clearMode = args[++i];
            }
            else if (a.rfind("--clear=", 0) == 0)
            {
                o.clearMode = a.substr(std::string("--clear=").size());
            }
            else if (a == "--no-clear")
            {
                o.clearMode = "never";
            }

            else if (!a.empty() && a[0] != '-')
            {
                if (o.appName.empty())
                {
                    o.appName = a;

                    std::filesystem::path p{a};
                    if (p.extension() == ".cpp")
                    {
                        o.singleCpp = true;
                        o.cppFile = std::filesystem::absolute(p);
                    }
                }
                else if (o.appName == "example" && o.exampleName.empty())
                {
                    o.exampleName = a;
                }

                std::filesystem::path p{a};

                if (p.extension() == ".vix")
                {
                    o.manifestMode = true;
                    o.manifestFile = std::filesystem::absolute(p);

                    // On ne force pas singleCpp ici
                    // Le manifest va décider project/script
                }
                else if (p.extension() == ".cpp")
                {
                    o.singleCpp = true;
                    o.cppFile = std::filesystem::absolute(p);
                }
            }
        }

        if (auto d = pick_dir_opt_local(args))
            o.dir = *d;

        if (o.forceServerLike && o.forceScriptLike)
        {
            hint("Both --force-server and --force-script were provided; "
                 "preferring --force-server.");
            o.forceScriptLike = false;
        }

        // normalize clearMode
        for (auto &c : o.clearMode)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (o.clearMode != "auto" && o.clearMode != "always" && o.clearMode != "never")
        {
            hint("Invalid value for --clear. Using 'auto'. Valid: auto|always|never.");
            o.clearMode = "auto";
        }

        return o;
    }

    // Runtime exit code
    void handle_runtime_exit_code(int code, const std::string &context)
    {
        // code is expected to be an already-normalized exit code (0..255 or 128+signal)
        if (code == 0)
            return;

        if (code == 130)
        {
            hint("ℹ Server interrupted by user (SIGINT).");
            return;
        }

        error(context + " (exit code " + std::to_string(code) + ").");
        hint("Check the logs above or run the command manually.");
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

    // Build log analysis
#ifndef _WIN32
    bool has_real_build_work(const std::string &log)
    {
        if (log.find("Building") != std::string::npos)
            return true;
        if (log.find("Linking") != std::string::npos)
            return true;
        if (log.find("Compiling") != std::string::npos)
            return true;
        if (log.find("Scanning dependencies") != std::string::npos)
            return true;

        if (log.find("no work to do") != std::string::npos)
            return false;

        bool hasBuiltTarget = log.find("Built target") != std::string::npos;
        if (hasBuiltTarget)
        {
            return false;
        }

        return true;
    }

    std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
    {
        std::string out;
#if defined(_WIN32)
        FILE *p = _popen(cmd.c_str(), "r");
#else
        FILE *p = popen(cmd.c_str(), "r");
#endif
        if (!p)
        {
            exitCode = -1;
            return out;
        }

        char buf[4096];
        while (fgets(buf, sizeof(buf), p))
            out.append(buf);

#if defined(_WIN32)
        exitCode = _pclose(p);
#else
        exitCode = normalize_exit_code(pclose(p));
#endif
        return out;
    }

    std::string run_and_capture(const std::string &cmd)
    {
        int code = 0;
        return run_and_capture_with_code(cmd, code);
    }
#else
    bool has_real_build_work(const std::string &)
    {
        return true;
    }
    std::string run_and_capture_with_code(const std::string &cmd, int &exitCode)
    {
        (void)cmd;
        exitCode = 0;
        return {};
    }
    std::string run_and_capture(const std::string &)
    {
        return {};
    }
#endif

    // Presets & project selection

    bool has_presets(const fs::path &projectDir)
    {
        std::error_code ec;
        return fs::exists(projectDir / "CMakePresets.json", ec) ||
               fs::exists(projectDir / "CMakeUserPresets.json", ec);
    }

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

    std::string choose_run_preset(const fs::path &dir,
                                  const std::string &configurePreset,
                                  const std::string &userRunPreset)
    {
        auto runs = list_presets(dir, "build");
        auto has = [&](const std::string &n)
        {
            return std::find(runs.begin(), runs.end(), n) != runs.end();
        };

        if (!userRunPreset.empty() && (runs.empty() || has(userRunPreset)))
            return userRunPreset;

        if (!runs.empty())
        {
            if (has("run-" + configurePreset))
                return "run-" + configurePreset;

            // dev-ninja → run-ninja
            if (configurePreset.rfind("dev-", 0) == 0)
            {
                std::string mapped = "run-" + configurePreset.substr(4);
                if (has(mapped))
                    return mapped;
            }

            if (has("run-ninja"))
                return "run-ninja";

            if (has("build-ninja"))
                return "build-ninja";

            return runs.front();
        }

        if (configurePreset.rfind("dev-", 0) == 0)
            return std::string("run-") + configurePreset.substr(4);
        return "run-ninja";
    }

    bool has_cmake_cache(const fs::path &buildDir)
    {
        std::error_code ec;
        return fs::exists(buildDir / "CMakeCache.txt", ec);
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

        return cwd;
    }

    static std::string lower(std::string s)
    {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    void apply_log_level_env(const Options &opt)
    {
        std::string level;

        if (!opt.logLevel.empty())
            level = lower(opt.logLevel);
        else if (opt.quiet)
            level = "warn";
        else if (opt.verbose)
            level = "debug";

        if (level.empty())
            return;

        if (level == "never" || level == "silent" || level == "0")
            level = "off";

        if (level == "unset" || level == "default")
        {
#if defined(_WIN32)
            _putenv_s("VIX_LOG_LEVEL", "");
#else
            ::unsetenv("VIX_LOG_LEVEL");
#endif
            return;
        }

        if (level == "none")
            level = "off";

        if (level == "on")
            level = "info";

        if (level != "trace" && level != "debug" && level != "info" &&
            level != "warn" && level != "error" && level != "critical" &&
            level != "off")
        {
            hint("Invalid value for --log-level. Using 'info'. Valid: trace|debug|info|warn|error|critical|off.");
            level = "info";
        }

#if defined(_WIN32)
        _putenv_s("VIX_LOG_LEVEL", level.c_str());
#else
        ::setenv("VIX_LOG_LEVEL", level.c_str(), 1);
#endif
    }

    void apply_log_format_env(const Options &opt)
    {
        if (opt.logFormat.empty())
            return;

        std::string fmt = lower(opt.logFormat);

        // aliases
        if (fmt == "pretty" || fmt == "pretty-json" || fmt == "pretty_json")
            fmt = "json-pretty";

        if (fmt != "kv" && fmt != "json" && fmt != "json-pretty")
        {
            hint("Invalid value for --log-format. Using 'kv'. Valid: kv|json|json-pretty.");
            fmt = "kv";
        }

#if defined(_WIN32)
        _putenv_s("VIX_LOG_FORMAT", fmt.c_str());
#else
        ::setenv("VIX_LOG_FORMAT", fmt.c_str(), 1);
#endif
    }

    void apply_log_color_env(const Options &opt)
    {
        // --no-color gagne sur tout
        if (opt.noColor)
        {
#if defined(_WIN32)
            _putenv_s("VIX_COLOR", "never");
#else
            ::setenv("VIX_COLOR", "never", 1);
#endif
            return;
        }

        if (opt.logColor.empty())
            return;

        std::string v = lower(opt.logColor);
        if (v != "auto" && v != "always" && v != "never")
        {
            hint("Invalid value for --log-color. Using 'auto'. Valid: auto|always|never.");
            v = "auto";
        }

#if defined(_WIN32)
        _putenv_s("VIX_COLOR", v.c_str());
#else
        ::setenv("VIX_COLOR", v.c_str(), 1);
#endif
    }

} // namespace vix::commands::RunCommand::detail
