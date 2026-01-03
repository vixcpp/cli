#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/Style.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <map>

#include <sys/wait.h>

namespace fs = std::filesystem;
using namespace vix::cli::style;

namespace vix::commands::BuildCommand
{
    namespace
    {
        static int normalize_exit_code(int raw) noexcept
        {
#ifdef __linux__
            if (raw == -1)
                return 127;
            if (WIFEXITED(raw))
                return WEXITSTATUS(raw);
            if (WIFSIGNALED(raw))
                return 128 + WTERMSIG(raw);
            return raw;
#else
            return raw;
#endif
        }

        static std::string quote(const std::string &s)
        {
            // POSIX shell single-quote safe quoting: ' -> '\'' sequence
            if (s.empty())
                return "''";

            bool needs = false;
            for (char c : s)
            {
                if (c == ' ' || c == '\t' || c == '\n' || c == '"' || c == '\'' ||
                    c == '\\' || c == '$' || c == '`')
                {
                    needs = true;
                    break;
                }
            }
            if (!needs)
                return s;

            std::string out;
            out.reserve(s.size() + 2);
            out.push_back('\'');
            for (char c : s)
            {
                if (c == '\'')
                    out.append("'\\''");
                else
                    out.push_back(c);
            }
            out.push_back('\'');
            return out;
        }

        static std::string infer_processor_from_triple(const std::string &triple)
        {
            // Examples:
            //  - aarch64-linux-gnu -> aarch64
            //  - arm-linux-gnueabihf -> arm
            //  - x86_64-linux-gnu -> x86_64
            //  - riscv64-linux-gnu -> riscv64
            if (triple.rfind("aarch64", 0) == 0)
                return "aarch64";
            if (triple.rfind("arm", 0) == 0)
                return "arm";
            if (triple.rfind("x86_64", 0) == 0)
                return "x86_64";
            if (triple.rfind("riscv64", 0) == 0)
                return "riscv64";
            return "unknown";
        }

        static bool executable_on_path(const std::string &exeName)
        {
            std::string cmd = "sh -lc " + quote("command -v " + exeName + " >/dev/null 2>&1");
            int raw = std::system(cmd.c_str());
            return normalize_exit_code(raw) == 0;
        }

        static std::vector<std::string> detect_available_targets()
        {
            static const std::vector<std::string> known = {
                "x86_64-linux-gnu",
                "aarch64-linux-gnu",
                "arm-linux-gnueabihf",
                "riscv64-linux-gnu"};

            std::vector<std::string> out;

            for (const auto &t : known)
            {
                if (executable_on_path(t + "-gcc") &&
                    executable_on_path(t + "-g++"))
                {
                    out.push_back(t);
                }
            }
            return out;
        }

        static std::string trim(std::string s)
        {
            auto is_ws = [](unsigned char c)
            { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

            while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
                s.erase(s.begin());
            while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
                s.pop_back();
            return s;
        }

        static bool file_exists(const fs::path &p)
        {
            std::error_code ec{};
            return fs::exists(p, ec) && !ec;
        }

        static bool dir_exists(const fs::path &p)
        {
            std::error_code ec{};
            return fs::exists(p, ec) && fs::is_directory(p, ec) && !ec;
        }

        static std::string read_text_file_or_empty(const fs::path &p)
        {
            std::ifstream ifs(p);
            if (!ifs)
                return {};
            std::ostringstream oss;
            oss << ifs.rdbuf();
            return oss.str();
        }

        static bool write_text_file_atomic(const fs::path &p, const std::string &content)
        {
            std::error_code ec{};
            if (!p.parent_path().empty())
                fs::create_directories(p.parent_path(), ec);

            const fs::path tmp = p.string() + ".tmp";
            {
                std::ofstream ofs(tmp, std::ios::binary);
                if (!ofs)
                    return false;
                ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
                ofs.flush();
                if (!ofs)
                    return false;
            }

            fs::rename(tmp, p, ec);
            if (ec)
            {
                fs::remove(tmp, ec);
                return false;
            }
            return true;
        }

        static bool ensure_dir(const fs::path &p, std::string &err)
        {
            if (dir_exists(p))
                return true;

            std::error_code ec{};
            fs::create_directories(p, ec);
            if (ec)
            {
                err = ec.message();
                return false;
            }
            return true;
        }

        static std::optional<fs::path> find_project_root(fs::path start)
        {
            std::error_code ec{};
            start = fs::weakly_canonical(start, ec);
            if (ec)
                return std::nullopt;

            for (fs::path p = start; !p.empty(); p = p.parent_path())
            {
                if (file_exists(p / "CMakeLists.txt"))
                    return p;

                if (p == p.root_path())
                    break;
            }
            return std::nullopt;
        }

        static std::string toolchain_contents_for_triple(const std::string &triple, const std::string &sysroot)
        {
            const std::string proc = infer_processor_from_triple(triple);

            std::ostringstream tc;
            if (!sysroot.empty())
            {
                tc << "set(CMAKE_SYSROOT \"" << sysroot << "\")\n";
                tc << "set(CMAKE_FIND_ROOT_PATH \"" << sysroot << "\")\n";
                tc << "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n";
                tc << "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n";
                tc << "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n\n";
            }

            tc << "# Auto-generated by Vix (vix build --target " << triple << ")\n";
            tc << "set(CMAKE_SYSTEM_NAME Linux)\n";
            tc << "set(CMAKE_SYSTEM_PROCESSOR \"" << proc << "\")\n\n";
            tc << "set(VIX_TARGET_TRIPLE \"" << triple << "\" CACHE STRING \"Vix target triple\")\n\n";
            tc << "set(CMAKE_C_COMPILER   \"" << triple << "-gcc\" CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_CXX_COMPILER \"" << triple << "-g++\" CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_AR           \"" << triple << "-ar\"  CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_RANLIB       \"" << triple << "-ranlib\" CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_STRIP        \"" << triple << "-strip\"  CACHE FILEPATH \"\" FORCE)\n\n";
            tc << "set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n";

            return tc.str();
        }

        struct Options
        {
            // required by spec
            std::string preset = "dev-ninja"; // dev | dev-ninja | release
            std::string targetTriple;         // --target <triple>
            std::string sysroot;
            bool linkStatic = false; // --static

            // build controls
            int jobs = 0;       // -j / --jobs
            bool clean = false; // --clean (force reconfigure)
            bool quiet = false; // -q / --quiet
            std::string dir;    // --dir/-d (optional)
        };

        struct Preset
        {
            std::string name;         // "dev-ninja"
            std::string generator;    // "Ninja"
            std::string buildType;    // "Debug"/"Release"
            std::string buildDirName; // "build-dev-ninja"
        };

        static std::map<std::string, Preset> builtin_presets()
        {
            std::map<std::string, Preset> m;

            m.emplace("dev", Preset{
                                 "dev",
                                 "Ninja",
                                 "Debug",
                                 "build-dev"});

            m.emplace("dev-ninja", Preset{
                                       "dev-ninja",
                                       "Ninja",
                                       "Debug",
                                       "build-dev-ninja"});

            m.emplace("release", Preset{
                                     "release",
                                     "Ninja",
                                     "Release",
                                     "build-release"});

            return m;
        }

        static bool is_option(const std::string &s)
        {
            return !s.empty() && s.front() == '-';
        }

        static std::optional<std::string> take_value(const std::vector<std::string> &args, size_t &i)
        {
            if (i + 1 >= args.size())
                return std::nullopt;
            if (is_option(args[i + 1]))
                return std::nullopt;
            ++i;
            return args[i];
        }

        static Options parse_args_or_exit(const std::vector<std::string> &args, int &exitCode)
        {
            Options o;
            exitCode = 0;

            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string &a = args[i];

                if (a == "--help" || a == "-h")
                {
                    exitCode = -2;
                    return o;
                }
                else if (a == "--preset")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for --preset");
                        exitCode = 2;
                        return o;
                    }
                    o.preset = *v;
                }
                else if (a.rfind("--preset=", 0) == 0)
                {
                    o.preset = a.substr(std::string("--preset=").size());
                    if (o.preset.empty())
                    {
                        error("Missing value for --preset");
                        exitCode = 2;
                        return o;
                    }
                }
                else if (a == "--target")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for --target <triple>");
                        exitCode = 2;
                        return o;
                    }
                    o.targetTriple = *v;
                }
                else if (a == "--targets")
                {
                    auto targets = detect_available_targets();

                    info("Detected build targets:");
                    for (const auto &t : targets)
                    {
                        if (t == "x86_64-linux-gnu")
                            step("  • " + t + " (native)");
                        else
                            step("  • " + t + " (cross)");
                    }

                    exitCode = -1; // exit cleanly
                    return o;
                }
                else if (a == "--sysroot")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for --sysroot <path>");
                        exitCode = 2;
                        return o;
                    }
                    o.sysroot = *v;
                }
                else if (a.rfind("--sysroot=", 0) == 0)
                {
                    o.sysroot = a.substr(std::string("--sysroot=").size());
                }

                else if (a.rfind("--target=", 0) == 0)
                {
                    o.targetTriple = a.substr(std::string("--target=").size());
                    if (o.targetTriple.empty())
                    {
                        error("Missing value for --target <triple>");
                        exitCode = 2;
                        return o;
                    }
                }
                else if (a == "--static")
                {
                    o.linkStatic = true;
                }
                else if (a == "-j" || a == "--jobs")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for -j/--jobs");
                        exitCode = 2;
                        return o;
                    }
                    try
                    {
                        o.jobs = std::stoi(*v);
                    }
                    catch (...)
                    {
                        error("Invalid integer for -j/--jobs: " + *v);
                        exitCode = 2;
                        return o;
                    }
                }
                else if (a.rfind("--jobs=", 0) == 0)
                {
                    auto v = a.substr(std::string("--jobs=").size());
                    try
                    {
                        o.jobs = std::stoi(v);
                    }
                    catch (...)
                    {
                        error("Invalid integer for --jobs: " + v);
                        exitCode = 2;
                        return o;
                    }
                }
                else if (a == "--clean")
                {
                    o.clean = true;
                }
                else if (a == "--quiet" || a == "-q")
                {
                    o.quiet = true;
                }
                else if (a == "--dir" || a == "-d")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for --dir <path>");
                        exitCode = 2;
                        return o;
                    }
                    o.dir = *v;
                }
                else if (a.rfind("--dir=", 0) == 0)
                {
                    o.dir = a.substr(std::string("--dir=").size());
                    if (o.dir.empty())
                    {
                        error("Missing value for --dir <path>");
                        exitCode = 2;
                        return o;
                    }
                }
                else
                {
                    // Keep the command predictable: reject unknown args
                    error("Unknown argument: " + a);
                    hint("Run: vix build --help");
                    exitCode = 2;
                    return o;
                }
            }

            return o;
        }

        static void log_header_if(const Options &opt, const std::string &title)
        {
            if (opt.quiet)
                return;
            info(title);
        }

        static void log_bullet_if(const Options &opt, const std::string &line)
        {
            if (opt.quiet)
                return;
            step(line);
        }

        static std::string format_seconds(long long ms)
        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(1);
            oss << (static_cast<double>(ms) / 1000.0) << "s";
            return oss.str();
        }

        static std::string ninja_status_env_if_needed(const Preset &p)
        {
            if (p.generator == "Ninja")
                return "NINJA_STATUS='[%f/%t %p%%] '";
            return "";
        }

        static void status_line(const Options &opt, const std::string &tag, const std::string &msg)
        {
            if (opt.quiet)
                return;

            std::cout << PAD << BOLD << CYAN << tag << RESET << " " << msg << "\n";
        }

        static void finished_line(const Options &opt, const std::string &profile, const std::string &took)
        {
            if (opt.quiet)
                return;

            std::cout << PAD << GREEN << "Finished" << RESET
                      << " " << profile << " in " << took << "\n";
        }

        static void log_hint_if(const Options &opt, const std::string &msg)
        {
            if (opt.quiet)
                return;
            hint(msg);
        }

        struct Plan
        {
            fs::path projectDir;
            Preset preset;
            fs::path buildDir;
            fs::path configureLog;
            fs::path buildLog;
            fs::path sigFile;
            fs::path toolchainFile;
            std::vector<std::pair<std::string, std::string>> cmakeVars;
            std::string signature;
        };

        static std::optional<Preset> resolve_preset(const std::string &name)
        {
            const auto presets = builtin_presets();
            auto it = presets.find(name);
            if (it == presets.end())
                return std::nullopt;
            return it->second;
        }

        static std::vector<std::pair<std::string, std::string>> build_cmake_vars(
            const Preset &p,
            const Options &opt,
            const fs::path &toolchainFile)
        {
            std::vector<std::pair<std::string, std::string>> vars;
            vars.reserve(16);

            vars.emplace_back("CMAKE_BUILD_TYPE", "\"" + p.buildType + "\"");
            vars.emplace_back("CMAKE_EXPORT_COMPILE_COMMANDS", "ON");
            if (opt.linkStatic)
                vars.emplace_back("VIX_LINK_STATIC", "ON");
            if (!opt.targetTriple.empty())
                vars.emplace_back("VIX_TARGET_TRIPLE", "\"" + opt.targetTriple + "\"");
            if (!opt.targetTriple.empty())
                vars.emplace_back("CMAKE_TOOLCHAIN_FILE", "\"" + toolchainFile.string() + "\"");
            std::sort(vars.begin(), vars.end(), [](const auto &a, const auto &b)
                      { return a.first < b.first; });

            return vars;
        }

        static std::string signature_join(const std::vector<std::pair<std::string, std::string>> &kvs)
        {
            std::ostringstream oss;
            for (const auto &kv : kvs)
                oss << kv.first << "=" << kv.second << "\n";
            return oss.str();
        }

        static std::string make_signature(
            const Preset &p,
            const Options &opt,
            const std::vector<std::pair<std::string, std::string>> &vars,
            const std::string &toolchainContent)
        {
            std::ostringstream oss;
            oss << "preset=" << p.name << "\n";
            oss << "generator=" << p.generator << "\n";
            oss << "static=" << (opt.linkStatic ? "1" : "0") << "\n";
            oss << "targetTriple=" << opt.targetTriple << "\n";
            oss << "vars:\n";
            oss << signature_join(vars);

            if (!opt.targetTriple.empty())
            {
                oss << "toolchain:\n";
                oss << toolchainContent;
                if (!toolchainContent.empty() && toolchainContent.back() != '\n')
                    oss << "\n";
            }

            return oss.str();
        }

        static std::optional<Plan> make_plan(const Options &opt, const fs::path &cwd)
        {
            fs::path base = cwd;
            if (!opt.dir.empty())
                base = fs::path(opt.dir);

            auto root = find_project_root(base);
            if (!root)
                return std::nullopt;

            auto presetOpt = resolve_preset(opt.preset);
            if (!presetOpt)
                return std::nullopt;

            Plan plan;
            plan.projectDir = *root;
            plan.preset = *presetOpt;
            if (!opt.targetTriple.empty())
            {
                plan.buildDir =
                    plan.projectDir /
                    (plan.preset.buildDirName + "-" + opt.targetTriple);
            }
            else
            {
                plan.buildDir =
                    plan.projectDir / plan.preset.buildDirName;
            }

            plan.configureLog = plan.buildDir / "configure.log";
            plan.buildLog = plan.buildDir / "build.log";
            plan.sigFile = plan.buildDir / ".vix-config.sig";
            plan.toolchainFile = plan.buildDir / "vix-toolchain.cmake";

            std::string toolchainContent;
            if (!opt.targetTriple.empty())
                toolchainContent = toolchain_contents_for_triple(opt.targetTriple, opt.sysroot);

            plan.cmakeVars = build_cmake_vars(plan.preset, opt, plan.toolchainFile);
            plan.signature = trim(make_signature(plan.preset, opt, plan.cmakeVars, toolchainContent)) + "\n";

            return plan;
        }

        static bool has_cmake_cache(const fs::path &buildDir)
        {
            return file_exists(buildDir / "CMakeCache.txt");
        }

        static bool signature_matches(const fs::path &sigFile, const std::string &sig)
        {
            const std::string old = read_text_file_or_empty(sigFile);
            return !old.empty() && old == sig;
        }

        static bool need_configure(const Options &opt, const Plan &plan)
        {
            if (opt.clean)
                return true;

            if (!has_cmake_cache(plan.buildDir))
                return true;

            if (!signature_matches(plan.sigFile, plan.signature))
                return true;

            return false;
        }

        struct ExecResult
        {
            int exitCode = 0;
            std::string command;
        };

        static ExecResult run_shell_live_to_log(const std::string &cmd,
                                                const std::string &envPrefix,
                                                const fs::path &logPath)
        {
            std::ostringstream oss;
            oss << "sh -lc " << quote(envPrefix + " " + cmd + " 2>&1 | tee " + quote(logPath.string()));

            ExecResult r;
            r.command = cmd;
            const int raw = std::system(oss.str().c_str());
            r.exitCode = normalize_exit_code(raw);
            return r;
        }

        static void print_log_tail(const fs::path &logPath, size_t maxLines)
        {
            const std::string content = read_text_file_or_empty(logPath);
            if (content.empty())
                return;

            std::vector<std::string> lines;
            lines.reserve(maxLines + 16);

            std::istringstream is(content);
            std::string line;
            while (std::getline(is, line))
                lines.push_back(line);

            const size_t start = (lines.size() > maxLines) ? (lines.size() - maxLines) : 0;

            std::cerr << "\n--- " << logPath.string() << " (last " << (lines.size() - start) << " lines) ---\n";
            for (size_t i = start; i < lines.size(); ++i)
                std::cerr << lines[i] << "\n";
            std::cerr << "--- end ---\n\n";
        }

        static std::string cmake_configure_cmd(const Plan &plan)
        {
            std::ostringstream oss;
            oss << "cmake"
                << " --log-level=WARNING"
                << " -S " << quote(plan.projectDir.string())
                << " -B " << quote(plan.buildDir.string())
                << " -G " << quote(plan.preset.generator);

            for (const auto &kv : plan.cmakeVars)
                oss << " -D" << kv.first << "=" << kv.second;

            return oss.str();
        }

        static std::string cmake_build_cmd(const Plan &plan, const Options &opt)
        {
            std::ostringstream oss;
            oss << "cmake"
                << " --build " << quote(plan.buildDir.string());

            if (opt.jobs > 0)
                oss << " -- -j " << opt.jobs;

            return oss.str();
        }

        static void print_preset_summary(const Options &opt, const Plan &plan)
        {
            if (opt.quiet)
                return;

            for (const auto &kv : plan.cmakeVars)
                step(kv.first + "=" + kv.second);

            std::cout << "\n";
        }

        class BuildCommand
        {
        public:
            explicit BuildCommand(Options opt) : opt_(std::move(opt)) {}

            int run()
            {
                const fs::path cwd = fs::current_path();

                auto planOpt = make_plan(opt_, cwd);
                if (!planOpt)
                {
                    error("Unable to determine the project directory (missing CMakeLists.txt?)");
                    hint("Run from your project root, or pass: vix build --dir <path>");
                    return 1;
                }
                plan_ = *planOpt;

                if (!opt_.targetTriple.empty())
                {
                    const std::string gcc = opt_.targetTriple + "-gcc";
                    const std::string gxx = opt_.targetTriple + "-g++";

                    if (!executable_on_path(gcc) || !executable_on_path(gxx))
                    {
                        error("Cross toolchain not found on PATH for target: " + opt_.targetTriple);
                        hint("Install the cross compiler and ensure binaries exist:");
                        hint("  " + gcc);
                        hint("  " + gxx);
                        return 1;
                    }
                }

                {
                    std::string err;
                    if (!ensure_dir(plan_.buildDir, err))
                    {
                        error("Unable to create build directory: " + plan_.buildDir.string());
                        if (!err.empty())
                            hint(err);
                        return 1;
                    }
                }

                log_header_if(opt_, "Using project directory:");
                log_bullet_if(opt_, plan_.projectDir.string());
                if (!opt_.quiet)
                    std::cout << "\n";

                if (!opt_.targetTriple.empty())
                {
                    const std::string tc = toolchain_contents_for_triple(opt_.targetTriple, opt_.sysroot);
                    if (!write_text_file_atomic(plan_.toolchainFile, tc))
                    {
                        error("Failed to write toolchain file: " + plan_.toolchainFile.string());
                        hint("Check filesystem permissions.");
                        return 1;
                    }
                }

                if (need_configure(opt_, plan_))
                {
                    status_line(
                        opt_,
                        "Configuring",
                        plan_.projectDir.filename().string() +
                            " (" + plan_.preset.name + ")");

                    print_preset_summary(opt_, plan_);

                    const auto t0 = std::chrono::steady_clock::now();
                    const std::string cmd = cmake_configure_cmd(plan_);

#ifndef _WIN32
                    const ExecResult r = run_shell_live_to_log(cmd, "", plan_.configureLog);
#else
                    const ExecResult r = run_shell_to_log(cmd, plan_.configureLog);
#endif

                    if (r.exitCode != 0)
                    {
                        error("CMake configure failed.");
                        log_hint_if(opt_, "Command:");
                        if (!opt_.quiet)
                            step(cmd);
                        if (!opt_.quiet)
                            print_log_tail(plan_.configureLog, 160);
                        return r.exitCode == 0 ? 2 : r.exitCode;
                    }

                    if (!write_text_file_atomic(plan_.sigFile, plan_.signature))
                    {
                        if (!opt_.quiet)
                            hint("Warning: unable to write config signature file");
                    }

                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - t0)
                                        .count();

                    success("Configured in " + format_seconds(ms));
                    if (!opt_.quiet)
                        std::cout << "\n";
                }
                else
                {
                    log_header_if(opt_, "Using existing configuration (cache-friendly).");
                    log_bullet_if(opt_, plan_.buildDir.string());
                    if (!opt_.quiet)
                        std::cout << "\n";
                }

                {
                    status_line(
                        opt_,
                        "Building",
                        plan_.projectDir.filename().string() +
                            " [" + plan_.preset.name + "]");

                    const auto t0 = std::chrono::steady_clock::now();
                    const std::string cmd = cmake_build_cmd(plan_, opt_);

#ifndef _WIN32
                    const std::string env = ninja_status_env_if_needed(plan_.preset);
                    const ExecResult r = run_shell_live_to_log(cmd, env, plan_.buildLog);
#else
                    const ExecResult r = run_shell_to_log(cmd, plan_.buildLog);
#endif

                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - t0)
                                        .count();

                    if (r.exitCode != 0)
                    {
                        error("Build failed.");
                        log_hint_if(opt_, "Command:");
                        if (!opt_.quiet)
                            step(cmd);
                        if (!opt_.quiet)
                            print_log_tail(plan_.buildLog, 200);
                        return r.exitCode == 0 ? 3 : r.exitCode;
                    }

                    const std::string profile =
                        (plan_.preset.buildType == "Release")
                            ? "release [optimized]"
                            : "dev [unoptimized + debuginfo]";

                    finished_line(opt_, profile, format_seconds(ms));
                    if (!opt_.quiet)
                        std::cout << "\n";
                }

                return 0;
            }

        private:
            Options opt_;
            Plan plan_{};
        };
    } // namespace

    int run(const std::vector<std::string> &args)
    {
        int parseExit = 0;
        Options opt = parse_args_or_exit(args, parseExit);

        if (parseExit == -2)
            return help();
        if (parseExit != 0)
            return parseExit;

        if (!resolve_preset(opt.preset))
        {
            error("Unknown preset: " + opt.preset);
            hint("Available presets: dev, dev-ninja, release");
            return 2;
        }

        BuildCommand cmd(std::move(opt));
        return cmd.run();
    }

    int help()
    {
        std::ostream &out = std::cout;

        out << "Usage:\n";
        out << "  vix build [options]\n\n";

        out << "Description:\n";
        out << "  Configure and build a CMake project using embedded Vix presets.\n";
        out << "  Cache-friendly: reuses build directories and avoids unnecessary reconfigure.\n\n";

        out << "Presets (embedded):\n";
        out << "  dev        -> Ninja + Debug   (build-dev)\n";
        out << "  dev-ninja  -> Ninja + Debug   (build-dev-ninja)\n";
        out << "  release    -> Ninja + Release (build-release)\n\n";

        out << "Options:\n";
        out << "  --preset <name>     Preset to use (dev, dev-ninja, release)\n";
        out << "  --target <triple>   Cross-compilation target triple (auto toolchain + VIX_TARGET_TRIPLE)\n";
        out << "  --static            Request static linking (VIX_LINK_STATIC=ON)\n";
        out << "  -j, --jobs <n>      Parallel build jobs (Ninja backend)\n";
        out << "  --clean             Force reconfigure (ignore cache/signature)\n";
        out << "  -d, --dir <path>    Project directory (where CMakeLists.txt lives)\n";
        out << "  -q, --quiet         Minimal output\n";
        out << "  -h, --help          Show this help\n\n";

        out << "Examples:\n";
        out << "  vix build\n";
        out << "  vix build --preset release\n";
        out << "  vix build --preset release --static\n";
        out << "  vix build --target aarch64-linux-gnu\n";
        out << "  vix build --preset release --target aarch64-linux-gnu\n";
        out << "  vix build -j 8\n\n";

        return 0;
    }
} // namespace vix::commands::BuildCommand
