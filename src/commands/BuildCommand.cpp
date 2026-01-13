#include <vix/cli/Style.hpp>
#include <vix/cli/commands/BuildCommand.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

        static std::string trim(std::string s)
        {
            auto is_ws = [](unsigned char c)
            {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            };

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

        static std::string read_text_file_or_empty(const fs::path &p)
        {
            std::ifstream ifs(p, std::ios::binary);
            if (!ifs)
                return {};
            std::ostringstream oss;
            oss << ifs.rdbuf();
            return oss.str();
        }

        static bool is_cmake_configure_summary_line(const std::string &line)
        {
            return (line.rfind("-- Configuring done", 0) == 0) ||
                   (line.rfind("-- Generating done", 0) == 0) ||
                   (line.find("Build files have been written to:") != std::string::npos);
        }

        static bool is_configure_cmd(const std::vector<std::string> &argv)
        {
            bool hasS = false, hasB = false;
            for (const auto &a : argv)
            {
                if (a == "-S")
                    hasS = true;
                if (a == "-B")
                    hasB = true;
            }
            return !argv.empty() && argv[0] == "cmake" && hasS && hasB;
        }

        static bool write_text_file_atomic(const fs::path &p,
                                           const std::string &content)
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

        // POSIX shell single-quote safe quoting (used only for display, not exec)
        static std::string quote_for_display(const std::string &s)
        {
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

#ifdef _WIN32
        static bool executable_on_path(const std::string &exeName)
        {
            // Minimal Windows PATH detection (best-effort)
            std::string cmd = "where " + exeName + " >nul 2>&1";
            return std::system(cmd.c_str()) == 0;
        }
#else
        static bool executable_on_path(const std::string &exeName)
        {
            // Fast PATH search without spawning a shell
            const char *pathEnv = std::getenv("PATH");
            if (!pathEnv)
                return false;

            std::string pathStr(pathEnv);
            size_t start = 0;
            while (start <= pathStr.size())
            {
                size_t end = pathStr.find(':', start);
                if (end == std::string::npos)
                    end = pathStr.size();

                std::string dir = pathStr.substr(start, end - start);
                if (!dir.empty())
                {
                    fs::path candidate = fs::path(dir) / exeName;
                    std::error_code ec{};
                    auto st = fs::status(candidate, ec);
                    if (!ec && fs::exists(st))
                    {
                        // check executable bit
                        struct stat sb{};
                        if (::stat(candidate.c_str(), &sb) == 0)
                        {
                            if ((sb.st_mode & S_IXUSR) || (sb.st_mode & S_IXGRP) ||
                                (sb.st_mode & S_IXOTH))
                                return true;
                        }
                    }
                }

                start = end + 1;
            }
            return false;
        }
#endif

        static std::vector<std::string> detect_available_targets()
        {
            static const std::vector<std::string> known = {
                "x86_64-linux-gnu", "aarch64-linux-gnu", "arm-linux-gnueabihf",
                "riscv64-linux-gnu"};

            std::vector<std::string> out;
            for (const auto &t : known)
            {
                if (executable_on_path(t + "-gcc") && executable_on_path(t + "-g++"))
                    out.push_back(t);
            }
            return out;
        }

        static void log_header_if(bool quiet, const std::string &title)
        {
            if (quiet)
                return;
            info(title);
        }

        static void log_bullet_if(bool quiet, const std::string &line)
        {
            if (quiet)
                return;
            step(line);
        }

        static void log_hint_if(bool quiet, const std::string &msg)
        {
            if (quiet)
                return;
            hint(msg);
        }

        static void status_line(bool quiet, const std::string &tag,
                                const std::string &msg)
        {
            if (quiet)
                return;
            std::cout << PAD << BOLD << CYAN << tag << RESET << " " << msg << "\n";
        }

        static std::string format_seconds(long long ms)
        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss.precision(1);
            oss << (static_cast<double>(ms) / 1000.0) << "s";
            return oss.str();
        }

        // Fast tail (O(tail_size))
        static void print_log_tail_fast(const fs::path &logPath, size_t maxLines)
        {
            std::ifstream ifs(logPath, std::ios::binary);
            if (!ifs)
                return;

            ifs.seekg(0, std::ios::end);
            std::streamoff size = ifs.tellg();
            if (size <= 0)
                return;

            const std::streamoff chunkSize = 64 * 1024; // 64 KB
            std::string buffer;
            buffer.reserve(
                static_cast<size_t>(std::min<std::streamoff>(size, chunkSize)));

            std::streamoff pos = size;
            size_t linesFound = 0;
            std::string acc;

            while (pos > 0 && linesFound <= maxLines)
            {
                std::streamoff readSize = std::min<std::streamoff>(chunkSize, pos);
                pos -= readSize;

                ifs.seekg(pos, std::ios::beg);
                buffer.assign(static_cast<size_t>(readSize), '\0');
                ifs.read(&buffer[0], readSize);
                if (!ifs)
                    break;

                // prepend this chunk
                acc.insert(0, buffer);

                // count lines
                linesFound = 0;
                for (char c : acc)
                    if (c == '\n')
                        ++linesFound;

                // stop early if enough lines accumulated
                if (linesFound > maxLines)
                    break;
            }

            // split last maxLines
            std::vector<std::string> lines;
            {
                std::istringstream is(acc);
                std::string line;
                while (std::getline(is, line))
                    lines.push_back(line);
            }

            size_t start = (lines.size() > maxLines) ? (lines.size() - maxLines) : 0;

            std::cerr << "\n--- " << logPath.string() << " (last "
                      << (lines.size() - start) << " lines) ---\n";
            for (size_t i = start; i < lines.size(); ++i)
                std::cerr << lines[i] << "\n";
            std::cerr << "--- end ---\n\n";
        }

        // Hashing for signature (FNV-1a 64)
        static uint64_t fnv1a64_bytes(const void *data, size_t n)
        {
            const uint8_t *p = static_cast<const uint8_t *>(data);
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; i < n; ++i)
            {
                h ^= static_cast<uint64_t>(p[i]);
                h *= 1099511628211ull;
            }
            return h;
        }

        static uint64_t fnv1a64_str(const std::string &s)
        {
            return fnv1a64_bytes(s.data(), s.size());
        }

        static std::string hex64(uint64_t v)
        {
            std::ostringstream oss;
            oss << std::hex;
            oss.width(16);
            oss.fill('0');
            oss << v;
            return oss.str();
        }

        static std::optional<std::string> read_file_hash_hex(const fs::path &p)
        {
            std::ifstream ifs(p, std::ios::binary);
            if (!ifs)
                return std::nullopt;

            uint64_t h = 1469598103934665603ull;
            std::string buf(64 * 1024, '\0');
            while (ifs)
            {
                ifs.read(&buf[0], static_cast<std::streamsize>(buf.size()));
                std::streamsize got = ifs.gcount();
                if (got <= 0)
                    break;
                h = fnv1a64_bytes(buf.data(), static_cast<size_t>(got)) ^
                    (h * 1099511628211ull); // mix
            }
            return hex64(h);
        }

        static void collect_files_recursive(const fs::path &root,
                                            const std::string &ext,
                                            std::vector<fs::path> &out)
        {
            std::error_code ec{};
            if (!dir_exists(root))
                return;

            for (auto it = fs::recursive_directory_iterator(root, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec)
                    break;

                if (!it->is_regular_file(ec))
                    continue;

                const fs::path p = it->path();
                if (ext.empty() || p.extension() == ext)
                    out.push_back(p);
            }
        }

        struct ExecResult
        {
            int exitCode = 0;
            std::string displayCommand;
            bool producedOutput = false;
            std::string capturedFirstLine;
        };

        static std::string join_display_cmd(const std::vector<std::string> &argv)
        {
            std::ostringstream oss;
            for (size_t i = 0; i < argv.size(); ++i)
            {
                if (i)
                    oss << " ";
                oss << quote_for_display(argv[i]);
            }
            return oss.str();
        }

#ifndef _WIN32
        static ExecResult run_process_live_to_log(
            const std::vector<std::string> &argv,
            const std::vector<std::pair<std::string, std::string>> &extraEnv,
            const fs::path &logPath, bool quiet, bool cmakeVerbose)
        {
            ExecResult r;
            r.displayCommand = join_display_cmd(argv);
            const bool filterCMakeSummary = is_configure_cmd(argv) && !cmakeVerbose;
            std::string consoleLineBuf;
            consoleLineBuf.reserve(4096);

            int pipefd[2];
            if (::pipe(pipefd) != 0)
            {
                r.exitCode = 127;
                return r;
            }

            // open log file
            int logfd = ::open(logPath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (logfd < 0)
            {
                ::close(pipefd[0]);
                ::close(pipefd[1]);
                r.exitCode = 127;
                return r;
            }

            pid_t pid = ::fork();
            if (pid == 0)
            {
                // child
                ::dup2(pipefd[1], STDOUT_FILENO);
                ::dup2(pipefd[1], STDERR_FILENO);

                ::close(pipefd[0]);
                ::close(pipefd[1]);

                // apply env
                for (const auto &kv : extraEnv)
                    ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

                // build argv for execvp
                std::vector<char *> cargv;
                cargv.reserve(argv.size() + 1);
                for (const auto &s : argv)
                    cargv.push_back(const_cast<char *>(s.c_str()));
                cargv.push_back(nullptr);

                ::execvp(cargv[0], cargv.data());
                _exit(127);
            }

            // parent
            ::close(pipefd[1]);

            std::string firstLine;
            bool gotFirstLine = false;

            std::string buf(16 * 1024, '\0');
            while (true)
            {
                ssize_t n = ::read(pipefd[0], &buf[0], buf.size());
                if (n > 0)
                {
                    r.producedOutput = true;

                    (void)::write(logfd, buf.data(), static_cast<size_t>(n));

                    if (!quiet)
                    {
                        if (!filterCMakeSummary)
                        {
                            (void)::write(STDOUT_FILENO, buf.data(), static_cast<size_t>(n));
                        }
                        else
                        {
                            consoleLineBuf.append(buf.data(), static_cast<size_t>(n));

                            size_t start = 0;
                            while (true)
                            {
                                size_t nl = consoleLineBuf.find('\n', start);
                                if (nl == std::string::npos)
                                    break;

                                std::string line = consoleLineBuf.substr(start, nl - start);
                                if (!is_cmake_configure_summary_line(line))
                                {
                                    line.push_back('\n');
                                    (void)::write(STDOUT_FILENO, line.data(), line.size());
                                }

                                start = nl + 1;
                            }

                            if (start > 0)
                                consoleLineBuf.erase(0, start);
                        }
                    }

                    if (!gotFirstLine)
                    {
                        for (ssize_t i = 0; i < n; ++i)
                        {
                            char c = buf[static_cast<size_t>(i)];
                            if (c == '\n')
                            {
                                gotFirstLine = true;
                                break;
                            }
                            if (firstLine.size() < 200)
                                firstLine.push_back(c);
                        }
                    }
                }
                else
                {
                    break;
                }
            }

            ::close(pipefd[0]);
            ::close(logfd);

            int status = 0;
            if (::waitpid(pid, &status, 0) < 0)
            {
                r.exitCode = 127;
                return r;
            }

            r.exitCode = normalize_exit_code(status);
            r.capturedFirstLine = trim(firstLine);
            return r;
        }

        static ExecResult run_process_capture(
            const std::vector<std::string> &argv,
            const std::vector<std::pair<std::string, std::string>> &extraEnv,
            std::string &outText)
        {
            ExecResult r;
            r.displayCommand = join_display_cmd(argv);

            int pipefd[2];
            if (::pipe(pipefd) != 0)
            {
                r.exitCode = 127;
                return r;
            }

            pid_t pid = ::fork();
            if (pid == 0)
            {
                ::dup2(pipefd[1], STDOUT_FILENO);
                ::dup2(pipefd[1], STDERR_FILENO);
                ::close(pipefd[0]);
                ::close(pipefd[1]);

                for (const auto &kv : extraEnv)
                    ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

                std::vector<char *> cargv;
                cargv.reserve(argv.size() + 1);
                for (const auto &s : argv)
                    cargv.push_back(const_cast<char *>(s.c_str()));
                cargv.push_back(nullptr);

                ::execvp(cargv[0], cargv.data());
                _exit(127);
            }

            ::close(pipefd[1]);

            std::string buf(8 * 1024, '\0');
            while (true)
            {
                ssize_t n = ::read(pipefd[0], &buf[0], buf.size());
                if (n > 0)
                    outText.append(buf.data(), static_cast<size_t>(n));
                else
                    break;
            }

            ::close(pipefd[0]);

            int status = 0;
            if (::waitpid(pid, &status, 0) < 0)
            {
                r.exitCode = 127;
                return r;
            }
            r.exitCode = normalize_exit_code(status);
            return r;
        }
#else
        static ExecResult run_process_live_to_log(
            const std::vector<std::string> &argv,
            const std::vector<std::pair<std::string, std::string>> & /*extraEnv*/,
            const fs::path &logPath, bool quiet)
        {
            ExecResult r;
            r.displayCommand = join_display_cmd(argv);

            std::ostringstream oss;
            for (size_t i = 0; i < argv.size(); ++i)
            {
                if (i)
                    oss << " ";
                oss << "\"" << argv[i] << "\"";
            }

            std::string cmd = oss.str();
            std::string full = cmd + " > \"" + logPath.string() + "\" 2>&1";
            int raw = std::system(full.c_str());
            r.exitCode = normalize_exit_code(raw);

            if (!quiet)
            {
                std::cerr << read_text_file_or_empty(logPath);
            }
            return r;
        }

        static ExecResult run_process_capture(
            const std::vector<std::string> &argv,
            const std::vector<std::pair<std::string, std::string>> & /*extraEnv*/,
            std::string &outText)
        {
            ExecResult r;
            r.displayCommand = join_display_cmd(argv);

            fs::path tmp = fs::temp_directory_path() / "vix_build_capture.tmp";
            (void)tmp;

            std::ostringstream oss;
            for (size_t i = 0; i < argv.size(); ++i)
            {
                if (i)
                    oss << " ";
                oss << "\"" << argv[i] << "\"";
            }

            std::string cmd = oss.str() + " > \"" + tmp.string() + "\" 2>&1";
            int raw = std::system(cmd.c_str());
            r.exitCode = normalize_exit_code(raw);

            outText = read_text_file_or_empty(tmp);
            std::error_code ec{};
            fs::remove(tmp, ec);
            return r;
        }
#endif

        static std::string toolchain_contents_for_triple(const std::string &triple,
                                                         const std::string &sysroot)
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
            tc << "set(VIX_TARGET_TRIPLE \"" << triple
               << "\" CACHE STRING \"Vix target triple\")\n\n";
            tc << "set(CMAKE_C_COMPILER   \"" << triple
               << "-gcc\" CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_CXX_COMPILER \"" << triple
               << "-g++\" CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_AR           \"" << triple
               << "-ar\"  CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_RANLIB       \"" << triple
               << "-ranlib\" CACHE FILEPATH \"\" FORCE)\n";
            tc << "set(CMAKE_STRIP        \"" << triple
               << "-strip\"  CACHE FILEPATH \"\" FORCE)\n\n";
            tc << "set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n";

            return tc.str();
        }

        enum class LinkerMode
        {
            Auto,
            Default,
            Mold,
            Lld,
        };

        enum class LauncherMode
        {
            Auto,
            None,
            Sccache,
            Ccache,
        };

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

            // performance switches (the 10 improvements)
            bool fast = false;    // --fast (prefer early exits + aggressive cache)
            bool useCache = true; // --no-cache to disable signature shortcut
            LinkerMode linker = LinkerMode::Auto;
            LauncherMode launcher = LauncherMode::Auto;
            bool status = true;        // --no-status to disable NINJA_STATUS
            bool dryUpToDate = true;   // up-to-date detection via ninja -n
            bool cmakeVerbose = false; // --cmake-verbose to show raw CMake summary lines
            std::string buildTarget;   // --build-target <name>
            std::vector<std::string> cmakeArgs;
        };

        struct Preset
        {
            std::string name;
            std::string generator;    // "Ninja"
            std::string buildType;    // "Debug"/"Release"
            std::string buildDirName; // "build-dev-ninja"
        };

        static std::map<std::string, Preset> builtin_presets()
        {
            std::map<std::string, Preset> m;

            m.emplace("dev", Preset{"dev", "Ninja", "Debug", "build-dev"});
            m.emplace("dev-ninja", Preset{"dev-ninja", "Ninja", "Debug", "build-ninja"});
            m.emplace("release", Preset{"release", "Ninja", "Release", "build-release"});

            return m;
        }

        static std::optional<Preset> resolve_preset(const std::string &name)
        {
            const auto presets = builtin_presets();
            auto it = presets.find(name);
            if (it == presets.end())
                return std::nullopt;
            return it->second;
        }

        static bool is_option(const std::string &s)
        {
            return !s.empty() && s.front() == '-';
        }

        static std::optional<std::string>
        take_value(const std::vector<std::string> &args, size_t &i)
        {
            if (i + 1 >= args.size())
                return std::nullopt;
            if (is_option(args[i + 1]))
                return std::nullopt;
            ++i;
            return args[i];
        }

        static std::optional<LinkerMode> parse_linker_mode(const std::string &v)
        {
            std::string s = v;
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (s == "auto")
                return LinkerMode::Auto;
            if (s == "default")
                return LinkerMode::Default;
            if (s == "mold")
                return LinkerMode::Mold;
            if (s == "lld")
                return LinkerMode::Lld;
            return std::nullopt;
        }

        static std::optional<LauncherMode> parse_launcher_mode(const std::string &v)
        {
            std::string s = v;
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (s == "auto")
                return LauncherMode::Auto;
            if (s == "none")
                return LauncherMode::None;
            if (s == "sccache")
                return LauncherMode::Sccache;
            if (s == "ccache")
                return LauncherMode::Ccache;
            return std::nullopt;
        }

        static Options parse_args_or_exit(const std::vector<std::string> &args,
                                          int &exitCode)
        {
            Options o;
            exitCode = 0;

            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::string &a = args[i];

                if (a == "--")
                {
                    for (size_t j = i + 1; j < args.size(); ++j)
                        o.cmakeArgs.push_back(args[j]);
                    break;
                }

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
                else if (a == "--build-target")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for --build-target <name>");
                        exitCode = 2;
                        return o;
                    }
                    o.buildTarget = *v;
                }
                else if (a.rfind("--build-target=", 0) == 0)
                {
                    o.buildTarget = a.substr(std::string("--build-target=").size());
                    if (o.buildTarget.empty())
                    {
                        error("Missing value for --build-target <name>");
                        exitCode = 2;
                        return o;
                    }
                }
                else if (a == "--targets")
                {
                    auto targets = detect_available_targets();

                    info("Detected build targets:");
                    for (const auto &t : targets)
                    {
                        if (t == "x86_64-linux-gnu")
                            step(t + " (native)");
                        else
                            step(t + " (cross)");
                    }

                    exitCode = -1;
                    return o;
                }
                else if (a == "--cmake-verbose")
                {
                    o.cmakeVerbose = true;
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
                else if (a == "--fast")
                {
                    o.fast = true;
                }
                else if (a == "--no-cache")
                {
                    o.useCache = false;
                }
                else if (a == "--linker")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for --linker <auto|default|mold|lld>");
                        exitCode = 2;
                        return o;
                    }
                    auto parsed = parse_linker_mode(*v);
                    if (!parsed)
                    {
                        error("Invalid value for --linker: " + *v);
                        hint("Valid: auto, default, mold, lld");
                        exitCode = 2;
                        return o;
                    }
                    o.linker = *parsed;
                }
                else if (a.rfind("--linker=", 0) == 0)
                {
                    auto v = a.substr(std::string("--linker=").size());
                    auto parsed = parse_linker_mode(v);
                    if (!parsed)
                    {
                        error("Invalid value for --linker: " + v);
                        hint("Valid: auto, default, mold, lld");
                        exitCode = 2;
                        return o;
                    }
                    o.linker = *parsed;
                }
                else if (a == "--launcher")
                {
                    auto v = take_value(args, i);
                    if (!v)
                    {
                        error("Missing value for --launcher <auto|none|sccache|ccache>");
                        exitCode = 2;
                        return o;
                    }
                    auto parsed = parse_launcher_mode(*v);
                    if (!parsed)
                    {
                        error("Invalid value for --launcher: " + *v);
                        hint("Valid: auto, none, sccache, ccache");
                        exitCode = 2;
                        return o;
                    }
                    o.launcher = *parsed;
                }
                else if (a.rfind("--launcher=", 0) == 0)
                {
                    auto v = a.substr(std::string("--launcher=").size());
                    auto parsed = parse_launcher_mode(v);
                    if (!parsed)
                    {
                        error("Invalid value for --launcher: " + v);
                        hint("Valid: auto, none, sccache, ccache");
                        exitCode = 2;
                        return o;
                    }
                    o.launcher = *parsed;
                }
                else if (a == "--no-status")
                {
                    o.status = false;
                }
                else if (a == "--no-up-to-date")
                {
                    o.dryUpToDate = false;
                }
                else
                {
                    error("Unknown argument: " + a);
                    hint("Run: vix build --help");
                    exitCode = 2;
                    return o;
                }
            }

            return o;
        }

        static std::optional<std::string> detect_launcher(const Options &opt)
        {
            switch (opt.launcher)
            {
            case LauncherMode::None:
                return std::nullopt;
            case LauncherMode::Sccache:
                return executable_on_path("sccache") ? std::optional<std::string>("sccache")
                                                     : std::nullopt;
            case LauncherMode::Ccache:
                return executable_on_path("ccache") ? std::optional<std::string>("ccache")
                                                    : std::nullopt;
            case LauncherMode::Auto:
            default:
                if (executable_on_path("sccache"))
                    return std::optional<std::string>("sccache");
                if (executable_on_path("ccache"))
                    return std::optional<std::string>("ccache");
                return std::nullopt;
            }
        }

        static std::optional<std::string> detect_fast_linker_flag(const Options &opt)
        {
#ifdef _WIN32
            (void)opt;
            return std::nullopt;
#else
            const bool has_mold = executable_on_path("mold");
            const bool has_ld_lld = executable_on_path("ld.lld");

            if (opt.linker == LinkerMode::Default)
                return std::nullopt;

            if (opt.linker == LinkerMode::Mold)
                return has_mold ? std::optional<std::string>("-fuse-ld=mold")
                                : std::nullopt;

            if (opt.linker == LinkerMode::Lld)
                return has_ld_lld ? std::optional<std::string>("-fuse-ld=lld")
                                  : std::nullopt;

            if (has_mold)
                return std::optional<std::string>("-fuse-ld=mold");
            if (has_ld_lld)
                return std::optional<std::string>("-fuse-ld=lld");

            return std::nullopt;
#endif
        }

        static std::string run_tool_version_line(const std::string &tool)
        {
            if (!executable_on_path(tool))
                return tool + ":<missing>\n";

            std::string out;
#ifdef _WIN32
            (void)run_process_capture({tool, "--version"}, {}, out);
#else
            (void)run_process_capture({tool, "--version"}, {}, out);
#endif
            out = trim(out);
            if (out.empty())
                return tool + ":<unknown>\n";

            auto pos = out.find('\n');
            if (pos != std::string::npos)
                out = out.substr(0, pos);

            return tool + ":" + out + "\n";
        }

        static std::string
        compute_project_files_fingerprint(const fs::path &projectDir)
        {
            std::vector<fs::path> files;

            files.push_back(projectDir / "CMakeLists.txt");
            collect_files_recursive(projectDir / "cmake", ".cmake", files);

            fs::path presets = projectDir / "CMakePresets.json";
            if (file_exists(presets))
                files.push_back(presets);

            std::sort(files.begin(), files.end());

            uint64_t h = 1469598103934665603ull;
            for (const auto &p : files)
            {
                std::error_code ec{};
                fs::path rp = fs::weakly_canonical(p, ec);
                std::string pathStr = ec ? p.string() : rp.string();

                auto hashOpt = read_file_hash_hex(p);
                std::string line = pathStr + "=" + (hashOpt ? *hashOpt : "<missing>");
                h ^= fnv1a64_str(line);
                h *= 1099511628211ull;
            }

            return hex64(h);
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

            std::optional<std::string> launcher;
            std::optional<std::string> fastLinkerFlag;
            std::string projectFingerprint;
        };

        static bool has_cmake_cache(const fs::path &buildDir)
        {
            return file_exists(buildDir / "CMakeCache.txt");
        }

        static bool signature_matches(const fs::path &sigFile, const std::string &sig)
        {
            const std::string old = read_text_file_or_empty(sigFile);
            return !old.empty() && old == sig;
        }

        static std::vector<std::pair<std::string, std::string>>
        build_cmake_vars(const Preset &p, const Options &opt,
                         const fs::path &toolchainFile,
                         const std::optional<std::string> &launcher,
                         const std::optional<std::string> &fastLinkerFlag)
        {
            std::vector<std::pair<std::string, std::string>> vars;
            vars.reserve(32);

            vars.emplace_back("CMAKE_BUILD_TYPE", p.buildType);
            vars.emplace_back("CMAKE_EXPORT_COMPILE_COMMANDS", "ON");

            if (!opt.targetTriple.empty())
                vars.emplace_back("CMAKE_TOOLCHAIN_FILE", toolchainFile.string());

            if (opt.linkStatic)
                vars.emplace_back("VIX_LINK_STATIC", "ON");

            if (!opt.targetTriple.empty())
                vars.emplace_back("VIX_TARGET_TRIPLE", opt.targetTriple);

            if (launcher && !launcher->empty())
            {
                vars.emplace_back("CMAKE_C_COMPILER_LAUNCHER", *launcher);
                vars.emplace_back("CMAKE_CXX_COMPILER_LAUNCHER", *launcher);
            }

            if (fastLinkerFlag && !fastLinkerFlag->empty())
            {
                vars.emplace_back("CMAKE_EXE_LINKER_FLAGS", *fastLinkerFlag);
                vars.emplace_back("CMAKE_SHARED_LINKER_FLAGS", *fastLinkerFlag);
                vars.emplace_back("CMAKE_MODULE_LINKER_FLAGS", *fastLinkerFlag);
            }

            std::sort(vars.begin(), vars.end(),
                      [](const auto &a, const auto &b)
                      { return a.first < b.first; });

            return vars;
        }

        static std::string
        signature_join(const std::vector<std::pair<std::string, std::string>> &kvs)
        {
            std::ostringstream oss;
            for (const auto &kv : kvs)
                oss << kv.first << "=" << kv.second << "\n";
            return oss.str();
        }

        static std::string make_signature(const Plan &plan, const Options &opt,
                                          const std::string &toolchainContent)
        {
            std::ostringstream oss;

            // Core config
            oss << "preset=" << plan.preset.name << "\n";
            oss << "generator=" << plan.preset.generator << "\n";
            oss << "buildType=" << plan.preset.buildType << "\n";
            oss << "static=" << (opt.linkStatic ? "1" : "0") << "\n";
            oss << "targetTriple=" << opt.targetTriple << "\n";
            oss << "sysroot=" << opt.sysroot << "\n";
            oss << "fast=" << (opt.fast ? "1" : "0") << "\n";
            oss << "useCache=" << (opt.useCache ? "1" : "0") << "\n";
            oss << "linker=" << static_cast<int>(opt.linker) << "\n";
            oss << "launcher=" << static_cast<int>(opt.launcher) << "\n";

            oss << "tools:\n";
            oss << run_tool_version_line("cmake");
            oss << run_tool_version_line("ninja");
#ifndef _WIN32
            oss << run_tool_version_line("c++");
            oss << run_tool_version_line("clang++");
            oss << run_tool_version_line("g++");
            oss << run_tool_version_line("mold");
            oss << run_tool_version_line("ld.lld");
#endif
            if (plan.launcher)
                oss << "launcherTool:" << *plan.launcher << "\n";
            if (plan.fastLinkerFlag)
                oss << "linkerFlag:" << *plan.fastLinkerFlag << "\n";

            oss << "projectFingerprint=" << plan.projectFingerprint << "\n";

            oss << "vars:\n";
            oss << signature_join(plan.cmakeVars);

            if (!opt.targetTriple.empty())
            {
                oss << "toolchain:\n";
                oss << toolchainContent;
                if (!toolchainContent.empty() && toolchainContent.back() != '\n')
                    oss << "\n";
            }

            return trim(oss.str()) + "\n";
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

            plan.launcher = detect_launcher(opt);
            plan.fastLinkerFlag = detect_fast_linker_flag(opt);
            plan.projectFingerprint = compute_project_files_fingerprint(plan.projectDir);

            if (!opt.targetTriple.empty())
                plan.buildDir =
                    plan.projectDir / (plan.preset.buildDirName + "-" + opt.targetTriple);
            else
                plan.buildDir = plan.projectDir / plan.preset.buildDirName;

            plan.configureLog = plan.buildDir / "configure.log";
            plan.buildLog = plan.buildDir / "build.log";
            plan.sigFile = plan.buildDir / ".vix-config.sig";
            plan.toolchainFile = plan.buildDir / "vix-toolchain.cmake";

            std::string toolchainContent;
            if (!opt.targetTriple.empty())
                toolchainContent =
                    toolchain_contents_for_triple(opt.targetTriple, opt.sysroot);

            plan.cmakeVars = build_cmake_vars(plan.preset, opt, plan.toolchainFile,
                                              plan.launcher, plan.fastLinkerFlag);
            plan.signature = make_signature(plan, opt, toolchainContent);

            return plan;
        }

        static bool need_configure(const Options &opt, const Plan &plan)
        {
            if (!opt.useCache)
                return true;
            if (opt.clean)
                return true;
            if (!has_cmake_cache(plan.buildDir))
                return true;
            if (!signature_matches(plan.sigFile, plan.signature))
                return true;
            return false;
        }

        static std::vector<std::string> cmake_configure_argv(const Plan &plan,
                                                             const Options &opt)
        {
            std::vector<std::string> argv;
            argv.reserve(32);

            argv.push_back("cmake");

            // utilise opt → plus de warning
            argv.push_back(opt.cmakeVerbose ? "--log-level=VERBOSE" : "--log-level=WARNING");

            argv.push_back("-S");
            argv.push_back(plan.projectDir.string());
            argv.push_back("-B");
            argv.push_back(plan.buildDir.string());
            argv.push_back("-G");
            argv.push_back(plan.preset.generator);

            for (const auto &kv : plan.cmakeVars)
                argv.push_back("-D" + kv.first + "=" + kv.second);

            for (const auto &a : opt.cmakeArgs)
                argv.push_back(a);

            return argv;
        }

        static int default_jobs()
        {
            unsigned int hc = std::thread::hardware_concurrency();
            if (hc == 0)
                return 4;
            if (hc > 64)
                hc = 64;
            return static_cast<int>(hc);
        }

        static std::vector<std::string> cmake_build_argv(const Plan &plan,
                                                         const Options &opt)
        {
            std::vector<std::string> argv;
            argv.reserve(16);

            argv.push_back("cmake");
            argv.push_back("--build");
            argv.push_back(plan.buildDir.string());

            int jobs = opt.jobs;
            if (jobs <= 0)
                jobs = default_jobs();

            if (!opt.buildTarget.empty())
            {
                argv.push_back("--target");
                argv.push_back(opt.buildTarget);
            }

            argv.push_back("--");
            argv.push_back("-j");
            argv.push_back(std::to_string(jobs));

            return argv;
        }

        static std::vector<std::string> ninja_dry_run_argv(const Plan &plan,
                                                           const Options &opt)
        {
            (void)opt;
            return {"ninja", "-C", plan.buildDir.string(), "-n"};
        }

        static std::vector<std::pair<std::string, std::string>>
        ninja_env(const Options &opt, const Plan &plan)
        {
            std::vector<std::pair<std::string, std::string>> env;

            if (!opt.status)
                return env;

            if (plan.preset.generator == "Ninja")
                env.emplace_back("NINJA_STATUS", "[%f/%t %p%%] ");

            return env;
        }

        static bool ninja_is_up_to_date(const Options &opt, const Plan &plan)
        {
            if (!opt.dryUpToDate)
                return false;
            if (plan.preset.generator != "Ninja")
                return false;

            std::string out;
            ExecResult r = run_process_capture(ninja_dry_run_argv(plan, opt),
                                               ninja_env(opt, plan), out);
            if (r.exitCode != 0)
                return false;

            out = trim(out);
            return out.empty();
        }

        static void print_preset_summary(const Options &opt, const Plan &plan)
        {
            if (opt.quiet)
                return;

            if (plan.launcher)
                step(std::string("compiler cache: ") + *plan.launcher);
            if (plan.fastLinkerFlag)
                step(std::string("fast linker: ") + *plan.fastLinkerFlag);

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
                    error("Unable to determine the project directory (missing "
                          "CMakeLists.txt?)");
                    hint("Run from your project root, or pass: vix build --dir <path>");
                    return 1;
                }
                plan_ = *planOpt;

#ifndef _WIN32
                if (!executable_on_path("ld"))
                {
                    if (!opt_.quiet)
                    {
                        hint("System linker 'ld' not found. Build may fail at link step.");
                        hint("Fix (recommended): sudo apt install -y binutils build-essential");
                    }
                }

                if (plan_.fastLinkerFlag && *plan_.fastLinkerFlag == "-fuse-ld=lld" &&
                    !executable_on_path("ld.lld"))
                {
                    if (!opt_.quiet)
                    {
                        hint("Requested lld but 'ld.lld' is missing -> falling back to default "
                             "linker.");
                        hint("Install optional speedup: sudo apt install -y lld");
                    }
                    plan_.fastLinkerFlag.reset();
                }

                if (plan_.fastLinkerFlag && *plan_.fastLinkerFlag == "-fuse-ld=mold" &&
                    !executable_on_path("mold"))
                {
                    if (!opt_.quiet)
                    {
                        hint("Requested mold but 'mold' is missing -> falling back to default "
                             "linker.");
                        hint("Install optional speedup: sudo apt install -y mold");
                    }
                    plan_.fastLinkerFlag.reset();
                }

                plan_.cmakeVars = build_cmake_vars(plan_.preset, opt_, plan_.toolchainFile,
                                                   plan_.launcher, plan_.fastLinkerFlag);

                std::string tc;
                if (!opt_.targetTriple.empty())
                    tc = toolchain_contents_for_triple(opt_.targetTriple, opt_.sysroot);

                plan_.signature = make_signature(plan_, opt_, tc);

#endif

                if (!opt_.targetTriple.empty())
                {
                    const std::string gcc = opt_.targetTriple + "-gcc";
                    const std::string gxx = opt_.targetTriple + "-g++";
                    if (!executable_on_path(gcc) || !executable_on_path(gxx))
                    {
                        error("Cross toolchain not found on PATH for target: " +
                              opt_.targetTriple);
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

                log_header_if(opt_.quiet, "Using project directory:");
                log_bullet_if(opt_.quiet, plan_.projectDir.string());
                if (!opt_.quiet)
                    std::cout << "\n";

                if (!opt_.targetTriple.empty())
                {
                    tc = toolchain_contents_for_triple(opt_.targetTriple, opt_.sysroot);
                    if (!write_text_file_atomic(plan_.toolchainFile, tc))
                    {
                        error("Failed to write toolchain file: " +
                              plan_.toolchainFile.string());
                        hint("Check filesystem permissions.");
                        return 1;
                    }
                }

                if (need_configure(opt_, plan_))
                {
                    status_line(opt_.quiet, "Configuring",
                                plan_.projectDir.filename().string() + " (" +
                                    plan_.preset.name + ")");

                    print_preset_summary(opt_, plan_);

                    const auto t0 = std::chrono::steady_clock::now();
                    auto argv = cmake_configure_argv(plan_, opt_);

                    const ExecResult r = run_process_live_to_log(
                        argv, {}, plan_.configureLog, opt_.quiet, opt_.cmakeVerbose);
                    if (r.exitCode != 0)
                    {
                        error("CMake configure failed.");
                        log_hint_if(opt_.quiet, "Command:");
                        if (!opt_.quiet)
                            step(r.displayCommand);
                        if (!opt_.quiet)
                            print_log_tail_fast(plan_.configureLog, 160);
                        return (r.exitCode == 0) ? 2 : r.exitCode;
                    }

                    if (opt_.useCache)
                    {
                        if (!write_text_file_atomic(plan_.sigFile, plan_.signature))
                        {
                            if (!opt_.quiet)
                                hint("Warning: unable to write config signature file");
                        }
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
                    log_header_if(opt_.quiet,
                                  "Using existing configuration (cache-friendly).");
                    log_bullet_if(opt_.quiet, plan_.buildDir.string());
                    if (!opt_.quiet)
                        std::cout << "\n";
                }

                if (opt_.fast && ninja_is_up_to_date(opt_, plan_))
                {
                    if (!opt_.quiet)
                    {
                        std::cout << PAD << GREEN << "Up to date" << RESET << " ("
                                  << plan_.preset.name << ")\n\n";
                    }
                    return 0;
                }

                {
                    status_line(opt_.quiet, "Building",
                                plan_.projectDir.filename().string() + " [" +
                                    plan_.preset.name + "]");

                    const auto t0 = std::chrono::steady_clock::now();
                    auto argv = cmake_build_argv(plan_, opt_);
                    auto env = ninja_env(opt_, plan_);

                    const ExecResult r = run_process_live_to_log(
                        argv, env, plan_.buildLog, opt_.quiet, opt_.cmakeVerbose);
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - t0)
                                        .count();

                    if (r.exitCode != 0)
                    {
                        error("Build failed.");
                        log_hint_if(opt_.quiet, "Command:");
                        if (!opt_.quiet)
                            step(r.displayCommand);
                        if (!opt_.quiet)
                            print_log_tail_fast(plan_.buildLog, 200);
                        return (r.exitCode == 0) ? 3 : r.exitCode;
                    }

                    const std::string profile = (plan_.preset.buildType == "Release")
                                                    ? "release [optimized]"
                                                    : "dev [unoptimized + debuginfo]";

                    if (!opt_.quiet)
                    {
                        std::cout << PAD << GREEN << "Finished" << RESET << " " << profile
                                  << " in " << format_seconds(ms) << "\n\n";
                    }
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
        out << "  vix build [options] -- [cmake args...]\n\n";

        out << "Description:\n";
        out << "  Configure and build a CMake project using embedded Vix presets.\n";
        out << "  Ultra-fast loops:\n";
        out << "    • No shell/tee overhead (spawn + pipe)\n";
        out << "    • Strong signature cache (tool versions + cmake file hashes)\n";
        out << "    • Optional fast no-op exit via Ninja dry-run (--fast)\n";
        out << "    • Auto sccache/ccache + mold/lld (auto)\n\n";

        out << "Presets (embedded):\n";
        out << "  dev        -> Ninja + Debug   (build-dev)\n";
        out << "  dev-ninja  -> Ninja + Debug   (build-dev-ninja)\n";
        out << "  release    -> Ninja + Release (build-release)\n\n";

        out << "Options:\n";
        out << "  --preset <name>       Preset to use (dev, dev-ninja, release)\n";
        out << "  --target <triple>     Cross-compilation target triple (auto "
               "toolchain)\n";
        out << "  --sysroot <path>      Sysroot for cross toolchain (optional)\n";
        out << "  --static              Request static linking "
               "(VIX_LINK_STATIC=ON)\n";
        out << "  -j, --jobs <n>        Parallel build jobs (default: CPU count, "
               "clamped)\n";
        out << "  --clean               Force reconfigure (ignore cache/signature)\n";
        out << "  --no-cache            Disable signature cache shortcut\n";
        out << "  --fast                Fast loop: if Ninja says up-to-date, exit "
               "immediately\n";
        out << "  --linker <mode>       auto|default|mold|lld (auto prefers mold "
               "then lld)\n";
        out << "  --launcher <mode>     auto|none|sccache|ccache (auto prefers "
               "sccache)\n";
        out << "  --no-status           Disable NINJA_STATUS progress format\n";
        out << "  --no-up-to-date       Disable Ninja dry-run up-to-date detection\n";
        out << "  -d, --dir <path>      Project directory (where CMakeLists.txt "
               "lives)\n";
        out << "  -q, --quiet           Minimal output (still logs to files)\n";
        out << "  --targets             List detected cross toolchains on PATH\n";
        out << "  --cmake-verbose       Show raw CMake configure output (no summary "
               "filtering)\n";
        out << "  --build-target <name> Build only a specific CMake target (ex: "
               "blog)\n";
        out << "  -h, --help            Show this help\n\n";

        out << "Examples:\n";
        out << "  vix build\n";
        out << "  vix build --fast\n";
        out << "  vix build --preset release\n";
        out << "  vix build --preset release --static\n";
        out << "  vix build --launcher sccache --linker mold\n";
        out << "  vix build --target aarch64-linux-gnu\n";
        out << "  vix build --preset release -target aarch64-linux-gnu\n";
        out << "  vix build --linker lld -- -DVIX_SYNC_BUILD_TESTS=ON\n";

        out << "  vix build -j 8\n\n";

        out << "Logs:\n";
        out << "  build-dev*/configure.log\n";
        out << "  build-dev*/build.log\n\n";

        return 0;
    }
} // namespace vix::commands::BuildCommand
