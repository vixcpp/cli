#include <vix/cli/commands/PackCommand.hpp>
#include <vix/cli/Style.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <cctype>   // std::isspace
#include <cstdio>   // popen, pclose, fgets
#include <cstdlib>  // std::system, std::getenv
#include <unistd.h> // getpid
#else
#include <cctype>  // std::isspace
#include <cstdlib> // std::getenv
#endif

namespace fs = std::filesystem;

namespace
{
    std::string trim_copy(const std::string &s);
    bool starts_with(const std::string &s, const std::string &prefix);

    bool has_file(const fs::path &p);
    bool has_dir(const fs::path &p);
    void ensure_dir(const fs::path &p);
    void write_text_file(const fs::path &p, const std::string &s);

    std::string env_or_default(const char *name, const std::string &fallback);
    std::string guess_abi_os_tag();
    std::string detect_arch_tag();

#ifndef _WIN32
    std::string shell_quote_posix(const std::string &s);
    std::string run_and_capture_posix(const std::string &cmd);
    bool tool_exists_posix(const std::string &tool);

    std::string run_capture_trimmed(const std::string &cmd);

    std::optional<std::string> sha256_of_string_posix(const fs::path &dir, const std::string &data);

    std::string build_sha256_listing_posix(
        const fs::path &packRoot,
        const std::vector<std::string> &exclude_rel_paths);

    void write_checksums_sha256_posix_excluding_self_and_manifest(const fs::path &packRoot);

    bool minisign_sign_payload_digest_posix(const fs::path &packRoot,
                                            const std::string &seckeyPath,
                                            bool verbose,
                                            bool allowInteractive);

    std::unordered_map<std::string, std::string> parse_sha256sum_file(const fs::path &p);
    std::optional<std::string> sha256_of_file_posix(const fs::path &p);

    bool zip_package_posix(const fs::path &packRoot, const fs::path &outFile);

    std::string first_line(const std::string &s);
    std::string detect_cmd_first_line_or_empty(const std::string &cmd);
#endif

    enum class SignMode
    {
        Auto,
        Never,
        Required
    };

    struct Options
    {
        std::optional<fs::path> dir;
        std::optional<fs::path> outDir;
        std::optional<std::string> name;
        std::optional<std::string> version;
        bool noZip{false};
        bool noHash{false};
        bool verbose{false};

        // bool sign{false};
        SignMode signMode{SignMode::Auto};
    };

    // TOML reader for vix.toml
    std::string strip_comment(const std::string &line)
    {
        bool inQuotes = false;
        std::string out;
        out.reserve(line.size());

        for (std::size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];

            if (c == '"')
                inQuotes = !inQuotes;

            if (!inQuotes && c == '#')
                break;

            out.push_back(c);
        }
        return out;
    }

    std::optional<std::string> parse_quoted_string(const std::string &raw)
    {
        const std::string s = trim_copy(raw);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return std::nullopt;
    }

    std::vector<std::string> parse_string_array(const std::string &raw)
    {
        std::vector<std::string> items;

        std::string s = trim_copy(raw);
        if (s.size() < 2 || s.front() != '[' || s.back() != ']')
            return items;

        s = trim_copy(s.substr(1, s.size() - 2));

        std::string token;
        bool inQuotes = false;

        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];

            if (c == '"')
            {
                inQuotes = !inQuotes;
                token.push_back(c);
                continue;
            }

            if (!inQuotes && c == ',')
            {
                const auto q = parse_quoted_string(trim_copy(token));
                if (q.has_value())
                    items.push_back(*q);
                token.clear();
                continue;
            }

            token.push_back(c);
        }

        if (!token.empty())
        {
            const auto q = parse_quoted_string(trim_copy(token));
            if (q.has_value())
                items.push_back(*q);
        }

        return items;
    }

    struct TomlData
    {
        // package
        std::optional<std::string> name;
        std::optional<std::string> version;
        std::optional<std::string> kind;
        std::optional<std::string> license;

        // exports.items
        std::vector<std::string> exports;

        // dependencies table
        std::unordered_map<std::string, std::string> deps;

        // toolchain table
        std::optional<std::string> cxx_standard;
        std::optional<std::string> cmake_generator;
    };

#ifndef _WIN32
    static std::optional<fs::path> find_default_minisign_seckey()
    {
        const char *home = std::getenv("HOME");
        if (!home || !*home)
            return std::nullopt;

        const fs::path p1 = fs::path(home) / ".config" / "vix" / "keys" / "vix-pack.key";
        if (has_file(p1))
            return p1;

        const fs::path p2 = fs::path(home) / "keys" / "vix" / "vix-pack.key";
        if (has_file(p2))
            return p2;

        return std::nullopt;
    }
    static std::optional<std::string> find_signing_key_path()
    {
        std::string sk = env_or_default("VIX_MINISIGN_SECKEY", "");
        if (!sk.empty())
            return sk;

        const auto def = find_default_minisign_seckey();
        if (def.has_value())
            return def->string();

        return std::nullopt;
    }

#endif

    TomlData read_vix_toml_if_exists(const fs::path &projectDir)
    {
        TomlData data;

        const fs::path p = projectDir / "vix.toml";
        if (!has_file(p))
            return data;

        std::ifstream in(p);
        if (!in)
            return data;

        std::string section;
        std::string line;

        while (std::getline(in, line))
        {
            line = trim_copy(strip_comment(line));
            if (line.empty())
                continue;

            if (line.size() >= 3 && line.front() == '[' && line.back() == ']')
            {
                section = trim_copy(line.substr(1, line.size() - 2));
                continue;
            }

            const std::size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            const std::string key = trim_copy(line.substr(0, eq));
            const std::string val = trim_copy(line.substr(eq + 1));

            if (section == "package")
            {
                if (key == "name")
                    data.name = parse_quoted_string(val).value_or(val);
                else if (key == "version")
                    data.version = parse_quoted_string(val).value_or(val);
                else if (key == "kind")
                    data.kind = parse_quoted_string(val).value_or(val);
                else if (key == "license")
                    data.license = parse_quoted_string(val).value_or(val);
            }
            else if (section == "exports")
            {
                if (key == "items")
                    data.exports = parse_string_array(val);
            }
            else if (section == "dependencies")
            {
                const auto q = parse_quoted_string(val);
                data.deps[key] = q.value_or(val);
            }
            else if (section == "toolchain")
            {
                if (key == "cxx_standard")
                    data.cxx_standard = parse_quoted_string(val).value_or(val);
                else if (key == "cmake_generator")
                    data.cmake_generator = parse_quoted_string(val).value_or(val);
            }
        }

        return data;
    }

    // Misc helpers
    std::string safe_default_name_from_dir(const fs::path &projectDir)
    {
        const auto f = projectDir.filename();
        if (!f.empty())
            return f.string();
        return "vix-package";
    }
    static SignMode parse_sign_mode(std::string v)
    {
        v = trim_copy(v);
        for (auto &c : v)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (v == "auto")
            return SignMode::Auto;
        if (v == "never")
            return SignMode::Never;
        if (v == "required")
            return SignMode::Required;

        throw std::runtime_error("Invalid --sign mode: " + v + " (expected: auto|never|required)");
    }

    Options parse_args(const std::vector<std::string> &args)
    {
        Options opt;

        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string &a = args[i];

            if (a == "--dir" || a == "-d")
            {
                if (i + 1 >= args.size())
                    throw std::runtime_error("--dir requires a path.");
                opt.dir = fs::path(args[++i]);
                continue;
            }

            if (a == "--out")
            {
                if (i + 1 >= args.size())
                    throw std::runtime_error("--out requires a directory.");
                opt.outDir = fs::path(args[++i]);
                continue;
            }

            if (a == "--name")
            {
                if (i + 1 >= args.size())
                    throw std::runtime_error("--name requires a value.");
                opt.name = args[++i];
                continue;
            }

            if (a == "--version")
            {
                if (i + 1 >= args.size())
                    throw std::runtime_error("--version requires a value.");
                opt.version = args[++i];
                continue;
            }

            if (a == "--no-zip")
            {
                opt.noZip = true;
                continue;
            }

            if (a == "--no-hash")
            {
                opt.noHash = true;
                continue;
            }

            if (a == "--verbose")
            {
                opt.verbose = true;
                continue;
            }
            if (a == "--sign")
            { // alias required
                opt.signMode = SignMode::Required;
                continue;
            }

            // --sign=auto|never|required
            if (starts_with(a, "--sign="))
            {
                opt.signMode = parse_sign_mode(a.substr(std::string("--sign=").size()));
                continue;
            }

            if (a == "--help" || a == "-h")
            {
                continue; // dispatcher handles it
            }

            throw std::runtime_error("Unknown option: " + a);
        }

        return opt;
    }

    fs::path choose_project_dir(const Options &opt)
    {
        if (opt.dir.has_value())
            return *opt.dir;
        return fs::current_path();
    }

    void copy_tree_if_exists(const fs::path &from, const fs::path &to, bool verboseCopy)
    {
        if (!has_dir(from))
            return;

        ensure_dir(to);

        std::error_code ec;
        fs::recursive_directory_iterator it(from, ec);
        if (ec)
        {
            throw std::runtime_error(
                "Failed to iterate directory: " + from.string() + " (" + ec.message() + ")");
        }

        for (const auto &entry : it)
        {
            std::error_code ec2;

            const fs::path rel = fs::relative(entry.path(), from, ec2);
            if (ec2)
                throw std::runtime_error("Failed to compute relative path: " + entry.path().string());

            const fs::path dst = to / rel;

            if (entry.is_directory(ec2))
            {
                ensure_dir(dst);
                continue;
            }

            ec2.clear();
            if (entry.is_regular_file(ec2))
            {
                ensure_dir(dst.parent_path());

                std::error_code ec3;
                fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, ec3);
                if (ec3)
                {
                    throw std::runtime_error(
                        "Failed to copy file: " + entry.path().string() + " -> " + dst.string() +
                        " (" + ec3.message() + ")");
                }

                if (verboseCopy)
                    vix::cli::style::step("copied: " + rel.string());
            }
        }
    }

    void copy_readme_if_exists(const fs::path &projectDir, const fs::path &packRoot)
    {
        if (!has_file(projectDir / "README.md"))
            return;

        ensure_dir(packRoot / "meta");

        std::error_code ec;
        fs::copy_file(projectDir / "README.md",
                      packRoot / "meta" / "README.md",
                      fs::copy_options::overwrite_existing,
                      ec);

        if (ec)
        {
            throw std::runtime_error("Failed to copy README.md: " + ec.message());
        }
    }

    // Manifest v2 writer
    // - payload digest (sha256 of stable sha256 listing)
    // - signature reference (minisign) (NO embed)
    // - checksums + artifact checksum
    void write_manifest_v2(const fs::path &packRoot,
                           const fs::path &projectDir,
                           const std::string &defaultName,
                           const std::string &defaultVersion,
                           const std::optional<fs::path> &artifactPathOpt,
                           const std::string &payloadDigest,
                           const bool signatureOk)
    {
        const TomlData toml = read_vix_toml_if_exists(projectDir);

        const std::string name = toml.name.value_or(defaultName);
        const std::string version = toml.version.value_or(defaultVersion);

        // exports
        std::vector<std::string> exports = toml.exports;
        if (exports.empty())
            exports.push_back(name);

        // deps
        nlohmann::json deps = nlohmann::json::object();
        for (const auto &kv : toml.deps)
            deps[kv.first] = kv.second;

#ifndef _WIN32
        const std::string cxxPath = env_or_default("CXX", "c++");
        const std::string cxxVersion = detect_cmd_first_line_or_empty(cxxPath + " --version");
        const std::string cmakeVersion = detect_cmd_first_line_or_empty("cmake --version");

        std::string generator = toml.cmake_generator.value_or(env_or_default("CMAKE_GENERATOR", ""));
        if (generator.empty())
        {
            if (has_file(projectDir / "CMakePresets.json"))
                generator = "presets";
        }
#else
        const std::string cxxPath = env_or_default("CXX", "c++");
        const std::string cxxVersion = "";
        const std::string cmakeVersion = "";
        const std::string generator = toml.cmake_generator.value_or("");
#endif

        const std::string cxxStd = toml.cxx_standard.value_or("c++23");

        const nlohmann::json layout = {
            {"include", has_dir(projectDir / "include")},
            {"src", has_dir(projectDir / "src")},
            {"lib", has_dir(projectDir / "lib")},
            {"modules", has_dir(projectDir / "modules")},
            {"readme", has_file(projectDir / "README.md")}};

        // checksums
        nlohmann::json checksums = nlohmann::json::object();

#ifndef _WIN32
        checksums["algorithm"] = "sha256";

        const fs::path sumsPath = packRoot / "checksums.sha256";
        nlohmann::json filesObj = nlohmann::json::object();

        if (has_file(sumsPath))
        {
            const auto m = parse_sha256sum_file(sumsPath);
            for (const auto &kv : m)
                filesObj[kv.first] = kv.second;
        }
        checksums["files"] = filesObj;

        if (artifactPathOpt.has_value() && has_file(*artifactPathOpt))
        {
            nlohmann::json artifact = nlohmann::json::object();
            artifact["path"] = artifactPathOpt->filename().string();

            if (const auto h = sha256_of_file_posix(*artifactPathOpt))
                artifact["sha256"] = *h;

            checksums["artifact"] = artifact;
        }
#else
        (void)artifactPathOpt;
#endif

        checksums["note"] = {
            {"manifest_not_in_files", true},
            {"reason", "manifest.json is generated after checksums.sha256 to avoid self-referential hashes"}};

        // payload digest
        nlohmann::json payload = nlohmann::json::object();
        payload["digest_algorithm"] = "sha256";
        payload["digest"] = payloadDigest;
        payload["digest_available"] = !payloadDigest.empty();
        payload["excludes"] = nlohmann::json::array({
            "manifest.json",
            "checksums.sha256",
            "meta/payload.digest",
            "meta/payload.digest.minisig",
        });

        // signature
        nlohmann::json signature = nlohmann::json::object();
        signature["enabled"] = signatureOk;
        signature["algorithm"] = "ed25519";
        signature["tool"] = "minisign";
        signature["signed"] = "meta/payload.digest";
        signature["file"] = "meta/payload.digest.minisig"; // reference only (no embed)

        // final manifest
        nlohmann::json manifest;

        manifest["schema"] = "vix.manifest.v2";
        manifest["package"] = {
            {"name", name},
            {"version", version},
            {"kind", toml.kind.value_or("package")},
            {"license", toml.license.value_or("")}};

        manifest["abi"] = {
            {"os", guess_abi_os_tag()},
            {"arch", detect_arch_tag()}};

        manifest["exports"] = exports;
        manifest["dependencies"] = deps;

        manifest["toolchain"] = {
            {"cxx",
             {
                 {"path", cxxPath},
                 {"version", cxxVersion},
                 {"standard", cxxStd},
             }},
            {"cmake",
             {
                 {"version", cmakeVersion},
                 {"generator", generator},
             }}};

        manifest["layout"] = layout;
        manifest["payload"] = payload;
        manifest["signature"] = signature;
        manifest["checksums"] = checksums;

        write_text_file(packRoot / "manifest.json", manifest.dump(2));
    }

    // Core
    std::string trim_copy(const std::string &s)
    {
        std::size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
            ++b;

        std::size_t e = s.size();
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
            --e;

        return s.substr(b, e - b);
    }

    bool starts_with(const std::string &s, const std::string &prefix)
    {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    bool has_file(const fs::path &p)
    {
        std::error_code ec;
        return fs::exists(p, ec) && fs::is_regular_file(p, ec);
    }

    bool has_dir(const fs::path &p)
    {
        std::error_code ec;
        return fs::exists(p, ec) && fs::is_directory(p, ec);
    }

    void ensure_dir(const fs::path &p)
    {
        std::error_code ec;
        fs::create_directories(p, ec);
        if (ec)
        {
            throw std::runtime_error(
                "Unable to create directory: " + p.string() + " (" + ec.message() + ")");
        }
    }

    void write_text_file(const fs::path &p, const std::string &s)
    {
        std::ofstream f(p, std::ios::binary);
        if (!f)
            throw std::runtime_error("Unable to write file: " + p.string());

        f.write(s.data(), static_cast<std::streamsize>(s.size()));
        if (!f)
            throw std::runtime_error("Failed to write file: " + p.string());
    }

    std::string env_or_default(const char *name, const std::string &fallback)
    {
        const char *v = std::getenv(name);
        if (v && *v)
            return std::string(v);
        return fallback;
    }

    std::string guess_abi_os_tag()
    {
#ifdef _WIN32
        return "windows";
#elif __APPLE__
        return "macos";
#else
        return "linux";
#endif
    }

    std::string detect_arch_tag()
    {
#if defined(__x86_64__) || defined(_M_X64)
        return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
        return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
        return "x86";
#else
        return "unknown";
#endif
    }

#ifndef _WIN32
    // POSIX helpers
    class Pipe
    {
    public:
        explicit Pipe(const std::string &cmd)
            : pipe_(::popen(cmd.c_str(), "r"))
        {
        }

        ~Pipe()
        {
            if (pipe_)
            {
                ::pclose(pipe_);
                pipe_ = nullptr;
            }
        }

        Pipe(const Pipe &) = delete;
        Pipe &operator=(const Pipe &) = delete;

        Pipe(Pipe &&other) noexcept
            : pipe_(other.pipe_)
        {
            other.pipe_ = nullptr;
        }

        Pipe &operator=(Pipe &&other) noexcept
        {
            if (this != &other)
            {
                if (pipe_)
                    ::pclose(pipe_);
                pipe_ = other.pipe_;
                other.pipe_ = nullptr;
            }
            return *this;
        }

        bool valid() const noexcept { return pipe_ != nullptr; }
        FILE *get() const noexcept { return pipe_; }

    private:
        FILE *pipe_{nullptr};
    };

    std::string shell_quote_posix(const std::string &s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('\'');
        for (const char c : s)
        {
            if (c == '\'')
                out += "'\\''";
            else
                out.push_back(c);
        }
        out.push_back('\'');
        return out;
    }

    std::string run_and_capture_posix(const std::string &cmd)
    {
        Pipe p(cmd);
        if (!p.valid())
            return {};

        std::string data;
        char buffer[4096];

        while (std::fgets(buffer, sizeof(buffer), p.get()) != nullptr)
        {
            data += buffer;
        }
        return data;
    }

    bool tool_exists_posix(const std::string &tool)
    {
        const std::string cmd =
            "command -v " + shell_quote_posix(tool) + " >/dev/null 2>&1 && echo ok";
        const std::string out = run_and_capture_posix(cmd);
        return out.find("ok") != std::string::npos;
    }

    std::string run_capture_trimmed(const std::string &cmd)
    {
        std::string out = run_and_capture_posix(cmd);
        return trim_copy(out);
    }

    std::optional<std::string> sha256_of_string_posix(const fs::path &dir, const std::string &data)
    {
        if (!tool_exists_posix("sha256sum"))
            return std::nullopt;

        // Unique temp file (avoid collisions)
        const int pid = static_cast<int>(::getpid());
        const fs::path tmp = dir / (".vix_payload_tmp_" + std::to_string(pid) + ".txt");

        write_text_file(tmp, data);

        std::ostringstream oss;
        oss << "sha256sum " << shell_quote_posix(tmp.string()) << " | awk '{print $1}'";

        const std::string hash = run_capture_trimmed(oss.str());

        std::error_code ec;
        fs::remove(tmp, ec);

        if (hash.empty())
            return std::nullopt;
        return hash;
    }

    std::string build_sha256_listing_posix(
        const fs::path &packRoot,
        const std::vector<std::string> &exclude_rel_paths)
    {
        if (!tool_exists_posix("sha256sum"))
            return {};

        std::ostringstream cmd;
        cmd << "cd " << shell_quote_posix(packRoot.string()) << " && find . -type f";

        for (const auto &ex : exclude_rel_paths)
            cmd << " ! -path " << shell_quote_posix(ex);

        cmd << " -print0 | sort -z | xargs -0 sha256sum";

        return run_and_capture_posix(cmd.str());
    }

    void write_checksums_sha256_posix_excluding_self_and_manifest(const fs::path &packRoot)
    {
        if (!tool_exists_posix("sha256sum"))
            return;

        // NOTE: We exclude checksums.sha256 and manifest.json to avoid self-ref.
        const std::vector<std::string> exclude = {"./checksums.sha256", "./manifest.json"};
        const std::string listing = build_sha256_listing_posix(packRoot, exclude);

        write_text_file(packRoot / "checksums.sha256", listing);
    }

    bool minisign_sign_payload_digest_posix(const fs::path &packRoot,
                                            const std::string &seckeyPath,
                                            bool verbose,
                                            bool allowInteractive)
    {
        if (!tool_exists_posix("minisign"))
            return false;

        const fs::path digestPath = packRoot / "meta" / "payload.digest";
        const fs::path sigPath = packRoot / "meta" / "payload.digest.minisig";

        if (!has_file(digestPath))
            return false;

        std::ostringstream oss;
        oss << "minisign -S -s " << shell_quote_posix(seckeyPath)
            << " -m " << shell_quote_posix(digestPath.string())
            << " -x " << shell_quote_posix(sigPath.string());

        if (!verbose)
            oss << " >/dev/null 2>&1";

        // IMPORTANT: in auto mode, never block for passphrase
        if (!allowInteractive)
            oss << " </dev/null";

        const int code = std::system(oss.str().c_str());
        return code == 0 && has_file(sigPath);
    }

    std::unordered_map<std::string, std::string> parse_sha256sum_file(const fs::path &p)
    {
        std::unordered_map<std::string, std::string> m;

        std::ifstream in(p);
        if (!in)
            return m;

        std::string line;
        while (std::getline(in, line))
        {
            line = trim_copy(line);
            if (line.size() < 10)
                continue;

            const std::size_t sp = line.find(' ');
            if (sp == std::string::npos)
                continue;

            const std::string hash = line.substr(0, sp);
            std::string rest = trim_copy(line.substr(sp));

            if (!rest.empty() && rest.front() == '*')
                rest.erase(rest.begin());

            rest = trim_copy(rest);
            if (starts_with(rest, "./"))
                rest = rest.substr(2);

            if (!hash.empty() && !rest.empty())
                m[rest] = hash;
        }

        return m;
    }

    std::optional<std::string> sha256_of_file_posix(const fs::path &p)
    {
        if (!tool_exists_posix("sha256sum"))
            return std::nullopt;

        std::ostringstream oss;
        oss << "sha256sum " << shell_quote_posix(p.string()) << " | awk '{print $1}'";
        const std::string out = run_capture_trimmed(oss.str());
        if (out.empty())
            return std::nullopt;
        return out;
    }

    bool zip_package_posix(const fs::path &packRoot, const fs::path &outFile)
    {
        if (!tool_exists_posix("zip"))
            return false;

        std::ostringstream oss;
        oss << "cd " << shell_quote_posix(packRoot.string())
            << " && zip -r " << shell_quote_posix(outFile.string()) << " . >/dev/null";

        const int code = std::system(oss.str().c_str());
        return code == 0;
    }

    std::string first_line(const std::string &s)
    {
        const std::size_t pos = s.find('\n');
        if (pos == std::string::npos)
            return trim_copy(s);
        return trim_copy(s.substr(0, pos));
    }

    std::string detect_cmd_first_line_or_empty(const std::string &cmd)
    {
        const std::string out = run_and_capture_posix(cmd);
        if (out.empty())
            return {};
        return first_line(out);
    }
#endif // !_WIN32

} // namespace

namespace vix::commands::PackCommand
{
    int run(const std::vector<std::string> &args)
    {
        Options opt;

        try
        {
            opt = parse_args(args);
        }
        catch (const std::exception &ex)
        {
            vix::cli::style::error(std::string("pack: ") + ex.what());
            vix::cli::style::hint("Try: vix pack --help");
            return 1;
        }

        const fs::path projectDir = choose_project_dir(opt);

        if (!has_file(projectDir / "CMakeLists.txt"))
        {
            vix::cli::style::error("No CMakeLists.txt found in: " + projectDir.string());
            vix::cli::style::hint("Run from a Vix project folder or use: vix pack --dir <path>");
            return 1;
        }

        const std::string name = opt.name.value_or(safe_default_name_from_dir(projectDir));
        const std::string version = opt.version.value_or("0.1.0");

        const fs::path distDir = opt.outDir.value_or(projectDir / "dist");

        try
        {
            ensure_dir(distDir);
        }
        catch (const std::exception &ex)
        {
            vix::cli::style::error(ex.what());
            return 1;
        }

        const fs::path packRoot = distDir / (name + "@" + version);

        if (has_dir(packRoot))
        {
            std::error_code ec;
            fs::remove_all(packRoot, ec);
        }

        try
        {
            ensure_dir(packRoot);
        }
        catch (const std::exception &ex)
        {
            vix::cli::style::error(ex.what());
            return 1;
        }

        vix::cli::style::section_title(std::cout, "vix pack");
        vix::cli::style::info("Project:");
        vix::cli::style::step(projectDir.string());

        vix::cli::style::info("Output:");
        vix::cli::style::step(distDir.string());

        vix::cli::style::info("Packaging:");
        vix::cli::style::step(name + "@" + version);

        try
        {
            // 1) Copy payload
            copy_tree_if_exists(projectDir / "include", packRoot / "include", opt.verbose);
            copy_tree_if_exists(projectDir / "src", packRoot / "src", opt.verbose);
            copy_tree_if_exists(projectDir / "lib", packRoot / "lib", opt.verbose);
            copy_tree_if_exists(projectDir / "modules", packRoot / "modules", opt.verbose);
            copy_tree_if_exists(projectDir / "tests", packRoot / "tests", opt.verbose);

            // 2) Copy README
            copy_readme_if_exists(projectDir, packRoot);
            // 1bis) Copy project root files (for vix run <folder>)
            if (has_file(projectDir / "CMakeLists.txt"))
            {
                fs::copy_file(
                    projectDir / "CMakeLists.txt",
                    packRoot / "CMakeLists.txt",
                    fs::copy_options::overwrite_existing);

                if (opt.verbose)
                    vix::cli::style::step("copied: CMakeLists.txt");
            }

            if (has_file(projectDir / "CMakePresets.json"))
            {
                fs::copy_file(
                    projectDir / "CMakePresets.json",
                    packRoot / "CMakePresets.json",
                    fs::copy_options::overwrite_existing);

                if (opt.verbose)
                    vix::cli::style::step("copied: CMakePresets.json");
            }

            if (has_file(projectDir / "vix.toml"))
                fs::copy_file(projectDir / "vix.toml", packRoot / "vix.toml", fs::copy_options::overwrite_existing);

            if (has_file(projectDir / "LICENSE"))
                fs::copy_file(projectDir / "LICENSE", packRoot / "LICENSE", fs::copy_options::overwrite_existing);

#ifndef _WIN32
            // 3) payload digest + signature
            ensure_dir(packRoot / "meta");

            const std::vector<std::string> payload_exclude = {
                "./manifest.json",
                "./checksums.sha256",
                "./meta/payload.digest",
                "./meta/payload.digest.minisig",
            };

            const std::string listing = build_sha256_listing_posix(packRoot, payload_exclude);

            std::string payloadDigest;
            if (!listing.empty())
            {
                const auto digestOpt = sha256_of_string_posix(packRoot, listing);
                if (digestOpt.has_value())
                    payloadDigest = *digestOpt;
            }

            write_text_file(packRoot / "meta" / "payload.digest", payloadDigest + "\n");

            bool signatureOk = false;

            const bool minisignOk = tool_exists_posix("minisign");
            const auto keyOpt = find_signing_key_path();
            const bool haveKey = keyOpt.has_value() && !keyOpt->empty();

            auto fail_required = [&](const std::string &reason)
            {
                vix::cli::style::error("pack: signing required but unavailable: " + reason);
                vix::cli::style::hint("Install minisign and/or set VIX_MINISIGN_SECKEY=/path/to/key");
            };

            if (opt.signMode == SignMode::Never)
            {
                signatureOk = false;
            }
            else if (opt.signMode == SignMode::Required)
            {
                if (!minisignOk)
                {
                    fail_required("minisign not found");
                    return 1;
                }
                if (!haveKey)
                {
                    fail_required("no signing key found");
                    return 1;
                }

                const std::string keyPath = *keyOpt;

                // clearer UX: show exactly what will happen
                vix::cli::style::info("Signing:");
                vix::cli::style::step("mode: required");
                vix::cli::style::step("tool: minisign (ed25519)");
                vix::cli::style::step("key: " + keyPath);
                vix::cli::style::step("file: meta/payload.digest");
                vix::cli::style::hint("minisign may prompt for the private key passphrase.");

                signatureOk = minisign_sign_payload_digest_posix(
                    packRoot,
                    keyPath,
                    opt.verbose,
                    /*allowInteractive*/ true);

                if (!signatureOk)
                {
                    fail_required("minisign failed");
                    return 1;
                }
            }
            else // Auto
            {
                if (minisignOk && haveKey)
                {
                    const std::string keyPath = *keyOpt;

                    // auto must NEVER block: deny interactive prompt
                    signatureOk = minisign_sign_payload_digest_posix(
                        packRoot,
                        keyPath,
                        opt.verbose,
                        /*allowInteractive*/ false);

                    if (!signatureOk && opt.verbose)
                        vix::cli::style::hint("Signing skipped (auto): needs passphrase or minisign failed.");
                }
                else
                {
                    if (opt.verbose)
                        vix::cli::style::hint("Signing skipped (auto): minisign/key not available.");
                }
            }

            // 4) checksums.sha256 (exclude self + manifest)
            if (!opt.noHash)
                write_checksums_sha256_posix_excluding_self_and_manifest(packRoot);

            // 5) manifest v2 (write BEFORE zip, so it's included)
            write_manifest_v2(packRoot, projectDir, name, version, /*artifactPathOpt*/ std::nullopt, payloadDigest, signatureOk);

            // 6) zip artifact (optional)
            std::optional<fs::path> artifactPath;

            if (!opt.noZip)
            {
                const fs::path outFile = distDir / (name + "@" + version + ".vixpkg");
                if (zip_package_posix(packRoot, outFile))
                {
                    artifactPath = outFile;
                }
                else
                {
                    vix::cli::style::hint("zip tool not available (or zip failed). Keeping folder package instead.");
                    vix::cli::style::hint("You can install zip, or use: vix pack --no-zip");
                }
            }

            // 6) manifest v2 (final write once)
            write_manifest_v2(packRoot, projectDir, name, version, artifactPath, payloadDigest, signatureOk);

            if (artifactPath.has_value())
            {
                vix::cli::style::success("Package created:");
                vix::cli::style::step(artifactPath->string());
                return 0;
            }

            vix::cli::style::success("Package folder created:");
            vix::cli::style::step(packRoot.string());
            return 0;
#else
            // Windows: folder-only for now
            write_manifest_v2(packRoot, projectDir, name, version, std::nullopt, "", false);

            vix::cli::style::success("Package folder created:");
            vix::cli::style::step(packRoot.string());
            return 0;
#endif
        }
        catch (const std::exception &ex)
        {
            vix::cli::style::error(std::string("pack failed: ") + ex.what());
            return 1;
        }
    }

    int help()
    {
        std::ostream &out = std::cout;

        out << "Usage:\n";
        out << "  vix pack [options]\n\n";

        out << "Description:\n";
        out << "  Package a Vix project into dist/<name>@<version> (manifest v2).\n";
        out << "  Optionally creates dist/<name>@<version>.vixpkg.\n\n";

        out << "Options:\n";
        out << "  -d, --dir <path>        Project directory (default: current directory)\n";
        out << "  --out <path>            Output directory (default: <project>/dist)\n";
        out << "  --name <name>           Package name (default: project folder name)\n";
        out << "  --version <ver>         Package version (default: 0.1.0)\n";
        out << "  --no-zip                Do not create .vixpkg (folder package only)\n";
        out << "  --no-hash               Do not generate checksums.sha256\n";
        out << "  --verbose               Show copied files + minisign output (if used)\n";
        out << "  --sign[=mode]           Signing: auto|never|required (default: auto)\n";
        out << "                          Use VIX_MINISIGN_SECKEY=path\n";
        out << "  -h, --help              Show this help\n\n";

        out << "Signing (optional):\n";
        out << "  VIX_MINISIGN_SECKEY=path  Sign meta/payload.digest with minisign\n\n";

        out << "Examples:\n";
        out << "  vix pack\n";
        out << "  vix pack --name blog --version 1.0.0\n";
        out << "  vix pack --verbose\n";
        out << "  vix pack --no-zip\n";
        out << "  vix pack --sign=never\n";
        out << "  vix pack --sign=auto\n";
        out << "  vix pack --sign=required\n";
        out << "  VIX_MINISIGN_SECKEY=./keys/vix-pack.key vix pack --sign\n";

        return 0;
    }

} // namespace vix::commands::PackCommand
