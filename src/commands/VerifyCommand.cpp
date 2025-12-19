#include <vix/cli/commands/VerifyCommand.hpp>
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
    // Core helpers (no unused functions)
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
            throw std::runtime_error("Unable to create directory: " + p.string() + " (" + ec.message() + ")");
    }

    std::string read_text_file(const fs::path &p)
    {
        std::ifstream in(p, std::ios::binary);
        if (!in)
            throw std::runtime_error("Unable to read file: " + p.string());

        std::ostringstream oss;
        oss << in.rdbuf();
        return oss.str();
    }

    std::optional<nlohmann::json> read_json_file_safe(const fs::path &p)
    {
        try
        {
            if (!has_file(p))
                return std::nullopt;
            const std::string s = read_text_file(p);
            return nlohmann::json::parse(s);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::string env_or_empty(const char *name)
    {
        const char *v = std::getenv(name);
        if (v && *v)
            return std::string(v);
        return {};
    }

    std::string safe_relative_or_abs(const fs::path &p, const fs::path &base)
    {
        std::error_code ec;
        const fs::path rel = fs::relative(p, base, ec);
        if (!ec)
            return rel.string();
        return p.string();
    }

    // Options
    struct Options
    {
        std::optional<fs::path> path;
        bool strict{false};
        bool verbose{false};
        std::optional<fs::path> pubkey;
        bool noSig{false};
        bool noHash{false};
        bool requireSig{false};
    };

    Options parse_args(const std::vector<std::string> &args)
    {
        Options opt;

        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string &a = args[i];

            if (a == "--path" || a == "-p")
            {
                if (i + 1 >= args.size())
                    throw std::runtime_error("--path requires a value.");
                opt.path = fs::path(args[++i]);
                continue;
            }

            if (a == "--strict")
            {
                opt.strict = true;
                continue;
            }

            if (a == "--verbose")
            {
                opt.verbose = true;
                continue;
            }

            if (a == "--pubkey")
            {
                if (i + 1 >= args.size())
                    throw std::runtime_error("--pubkey requires a path.");
                opt.pubkey = fs::path(args[++i]);
                continue;
            }

            if (a == "--no-sig")
            {
                opt.noSig = true;
                continue;
            }

            if (a == "--no-hash")
            {
                opt.noHash = true;
                continue;
            }
            if (a == "--require-signature")
            {
                opt.requireSig = true;
                continue;
            }

            if (a == "--help" || a == "-h")
            {
                continue; // dispatcher handles
            }

            throw std::runtime_error("Unknown option: " + a);
        }

        return opt;
    }

    // Auto-discovery logic:
    // - If --path provided => use it
    // - Else:
    //    1) if ./manifest.json exists => verify here
    //    2) else if ./dist exists => pick latest dist/<name>@<ver>/ (by manifest mtime)
    std::optional<fs::path> pick_latest_dist_package(const fs::path &projectDir)
    {
        const fs::path dist = projectDir / "dist";
        if (!has_dir(dist))
            return std::nullopt;

        std::optional<fs::path> best;
        std::optional<fs::file_time_type> bestTime;

        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(dist, ec))
        {
            if (ec)
                break;

            std::error_code ec2;
            if (!entry.is_directory(ec2))
                continue;

            const fs::path candidate = entry.path();
            const fs::path manifest = candidate / "manifest.json";
            if (!has_file(manifest))
                continue;

            std::error_code ec3;
            const auto t = fs::last_write_time(manifest, ec3);
            if (ec3)
                continue;

            if (!best.has_value() || !bestTime.has_value() || t > *bestTime)
            {
                best = candidate;
                bestTime = t;
            }
        }

        return best;
    }

    fs::path choose_path_to_verify(const Options &opt)
    {
        if (opt.path.has_value())
            return *opt.path;

        const fs::path cwd = fs::current_path();

        // If user is already in a package folder
        if (has_file(cwd / "manifest.json"))
            return cwd;

        // If user is in project root (pack created dist/), pick latest package
        if (has_file(cwd / "CMakeLists.txt"))
        {
            const auto best = pick_latest_dist_package(cwd);
            if (best.has_value())
                return *best;
        }

        // Last fallback: if ./dist exists but no CMakeLists
        {
            const auto best = pick_latest_dist_package(cwd);
            if (best.has_value())
                return *best;
        }

        return cwd; // will fail with good message later
    }

#ifndef _WIN32
    // ------------------------------------------------------------
    // POSIX tools
    // ------------------------------------------------------------
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
            data += buffer;
        return data;
    }

    std::string run_capture_trimmed(const std::string &cmd)
    {
        return trim_copy(run_and_capture_posix(cmd));
    }

    bool tool_exists_posix(const std::string &tool)
    {
        const std::string cmd = "command -v " + shell_quote_posix(tool) + " >/dev/null 2>&1 && echo ok";
        return run_and_capture_posix(cmd).find("ok") != std::string::npos;
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

    std::string build_sha256_listing_posix(const fs::path &packRoot,
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

    std::optional<std::string> sha256_of_string_posix(const fs::path &dir, const std::string &data)
    {
        if (!tool_exists_posix("sha256sum"))
            return std::nullopt;

        const int pid = static_cast<int>(::getpid());
        const fs::path tmp = dir / (".vix_verify_tmp_" + std::to_string(pid) + ".txt");

        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f)
                return std::nullopt;
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!f)
                return std::nullopt;
        }

        std::ostringstream oss;
        oss << "sha256sum " << shell_quote_posix(tmp.string()) << " | awk '{print $1}'";
        const std::string hash = run_capture_trimmed(oss.str());

        std::error_code ec;
        fs::remove(tmp, ec);

        if (hash.empty())
            return std::nullopt;
        return hash;
    }

    bool minisign_verify_payload_digest_posix(const fs::path &packRoot, const fs::path &pubkey)
    {
        if (!tool_exists_posix("minisign"))
            return false;

        const fs::path digestPath = packRoot / "meta" / "payload.digest";
        const fs::path sigPath = packRoot / "meta" / "payload.digest.minisig";

        if (!has_file(digestPath) || !has_file(sigPath) || !has_file(pubkey))
            return false;

        std::ostringstream oss;
        oss << "minisign -V -p " << shell_quote_posix(pubkey.string())
            << " -m " << shell_quote_posix(digestPath.string())
            << " -x " << shell_quote_posix(sigPath.string())
            << " >/dev/null 2>&1";

        const int code = std::system(oss.str().c_str());
        return code == 0;
    }

    std::optional<fs::path> extract_vixpkg_to_temp_posix(const fs::path &artifact)
    {
        if (!tool_exists_posix("unzip"))
            return std::nullopt;

        const int pid = static_cast<int>(::getpid());
        fs::path tmp = fs::temp_directory_path() / ("vix_verify_" + std::to_string(pid));

        std::error_code ec;
        fs::remove_all(tmp, ec);
        ensure_dir(tmp);

        std::ostringstream oss;
        oss << "unzip -q " << shell_quote_posix(artifact.string())
            << " -d " << shell_quote_posix(tmp.string());

        const int code = std::system(oss.str().c_str());
        if (code != 0)
        {
            fs::remove_all(tmp, ec);
            return std::nullopt;
        }

        return tmp;
    }
#endif // !_WIN32

    // ------------------------------------------------------------
    // Verify report
    // ------------------------------------------------------------
    struct VerifyReport
    {
        bool ok{true};
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        void fail(const std::string &s)
        {
            ok = false;
            errors.push_back(s);
        }

        void warn(const std::string &s)
        {
            warnings.push_back(s);
        }
    };

    bool json_has_string(const nlohmann::json &j, const std::string &key)
    {
        return j.contains(key) && j[key].is_string() && !j[key].get<std::string>().empty();
    }

    void verify_manifest_minimal(const nlohmann::json &m, VerifyReport &r)
    {
        if (!m.is_object())
        {
            r.fail("manifest.json is not a JSON object.");
            return;
        }

        if (!json_has_string(m, "schema"))
            r.fail("manifest.schema missing or empty.");
        else if (m["schema"].get<std::string>() != "vix.manifest.v2")
            r.fail("manifest.schema must be 'vix.manifest.v2'.");

        if (!m.contains("package") || !m["package"].is_object())
            r.fail("manifest.package missing or not an object.");
        else
        {
            const auto &p = m["package"];
            if (!json_has_string(p, "name"))
                r.fail("manifest.package.name missing or empty.");
            if (!json_has_string(p, "version"))
                r.fail("manifest.package.version missing or empty.");
        }

        if (!m.contains("abi") || !m["abi"].is_object())
            r.fail("manifest.abi missing or not an object.");
        else
        {
            const auto &a = m["abi"];
            if (!json_has_string(a, "os"))
                r.fail("manifest.abi.os missing or empty.");
            if (!json_has_string(a, "arch"))
                r.fail("manifest.abi.arch missing or empty.");
        }

        if (!m.contains("payload") || !m["payload"].is_object())
            r.fail("manifest.payload missing or not an object.");
        else
        {
            const auto &p = m["payload"];
            if (!json_has_string(p, "digest_algorithm"))
                r.fail("manifest.payload.digest_algorithm missing or empty.");
            if (!json_has_string(p, "digest"))
                r.fail("manifest.payload.digest missing or empty.");
        }
    }

#ifndef _WIN32
    void verify_checksums_sha256_posix(const fs::path &packRoot, const Options &opt, VerifyReport &r)
    {
        const fs::path sums = packRoot / "checksums.sha256";
        if (!has_file(sums))
        {
            if (opt.strict)
                r.fail("checksums.sha256 is missing (strict mode).");
            else
                r.warn("checksums.sha256 is missing.");
            return;
        }

        if (!tool_exists_posix("sha256sum"))
        {
            if (opt.strict)
                r.fail("sha256sum is not available (strict mode).");
            else
                r.warn("sha256sum is not available; skipping checksums verification.");
            return;
        }

        const auto expected = parse_sha256sum_file(sums);
        if (expected.empty())
        {
            if (opt.strict)
                r.fail("checksums.sha256 is empty or invalid (strict mode).");
            else
                r.warn("checksums.sha256 is empty or invalid.");
            return;
        }

        std::size_t okCount = 0;
        for (const auto &kv : expected)
        {
            const fs::path fp = packRoot / kv.first;
            if (!has_file(fp))
            {
                r.fail("Missing file listed in checksums.sha256: " + kv.first);
                continue;
            }

            const auto h = sha256_of_file_posix(fp);
            if (!h.has_value())
            {
                r.fail("Unable to compute sha256 for: " + kv.first);
                continue;
            }

            if (*h != kv.second)
            {
                r.fail("SHA256 mismatch: " + kv.first);
                continue;
            }

            ++okCount;
        }

        if (opt.verbose)
            vix::cli::style::step("checksums ok: " + std::to_string(okCount) + " file(s)");
    }

    void verify_payload_digest_posix(const fs::path &packRoot,
                                     const nlohmann::json &manifest,
                                     const Options &opt,
                                     VerifyReport &r)
    {
        // 1) Read expected digest from manifest (required for v2)
        std::string expectedManifest;
        if (manifest.contains("payload") && manifest["payload"].is_object())
        {
            const auto &p = manifest["payload"];
            if (p.contains("digest") && p["digest"].is_string())
                expectedManifest = p["digest"].get<std::string>();
        }
        expectedManifest = trim_copy(expectedManifest);

        if (expectedManifest.empty())
        {
            r.fail("manifest.payload.digest missing or empty.");
            return;
        }

        if (!tool_exists_posix("sha256sum"))
        {
            if (opt.strict)
                r.fail("sha256sum not available (strict mode).");
            else
                r.warn("sha256sum not available; skipping payload digest check.");
            return;
        }

        // 2) Build excludes (base + manifest payload.excludes)
        std::vector<std::string> excludes = {
            "./manifest.json",
            "./checksums.sha256",
            "./meta/payload.digest",
            "./meta/payload.digest.minisig",
        };

        if (manifest.contains("payload") && manifest["payload"].is_object())
        {
            const auto &p = manifest["payload"];
            if (p.contains("excludes") && p["excludes"].is_array())
            {
                for (const auto &it : p["excludes"])
                {
                    if (!it.is_string())
                        continue;

                    const std::string v0 = it.get<std::string>();
                    const std::string v = trim_copy(v0);
                    if (v.empty())
                        continue;

                    excludes.push_back(starts_with(v, "./") ? v : ("./" + v));
                }
            }
        }

        // 3) Compute digest from current payload
        const std::string listing = build_sha256_listing_posix(packRoot, excludes);
        if (listing.empty())
        {
            r.fail("Unable to build sha256 listing for payload.");
            return;
        }

        const auto computedOpt = sha256_of_string_posix(packRoot, listing);
        if (!computedOpt.has_value())
        {
            r.fail("Unable to compute payload digest.");
            return;
        }

        const std::string computed = trim_copy(*computedOpt);

        // 4) Compare computed vs manifest digest (authoritative)
        if (computed != expectedManifest)
        {
            r.fail("Payload digest mismatch (computed != manifest.payload.digest).");
            if (opt.verbose)
            {
                vix::cli::style::info("payload digest:");
                vix::cli::style::step("computed: " + computed);
                vix::cli::style::step("manifest: " + expectedManifest);
            }
            return;
        }

        // 5) If meta/payload.digest exists, ensure it's consistent too
        const fs::path digestFile = packRoot / "meta" / "payload.digest";
        if (has_file(digestFile))
        {
            const std::string expectedFile = trim_copy(read_text_file(digestFile));
            if (!expectedFile.empty() && expectedFile != expectedManifest)
            {
                r.fail("Payload digest mismatch (meta/payload.digest != manifest.payload.digest).");
                if (opt.verbose)
                {
                    vix::cli::style::info("payload digest:");
                    vix::cli::style::step("file    : " + expectedFile);
                    vix::cli::style::step("manifest: " + expectedManifest);
                }
                return;
            }
        }
        else
        {
            if (opt.strict)
                r.fail("meta/payload.digest missing (strict mode).");
            else
                r.warn("meta/payload.digest missing.");
            return;
        }

        if (opt.verbose)
        {
            vix::cli::style::step("payload digest ok");
            vix::cli::style::step("digest: " + expectedManifest);
        }
    }

    void verify_signature_posix(const fs::path &packRoot, Options &opt, VerifyReport &r)
    {
        if (opt.noSig)
        {
            if (opt.requireSig || opt.strict)
                r.fail("--no-sig cannot be used with --require-signature/--strict.");
            else if (opt.verbose)
                vix::cli::style::step("signature check skipped (--no-sig)");
            return;
        }

        const fs::path sig = packRoot / "meta" / "payload.digest.minisig";
        if (!has_file(sig))
        {
            if (opt.requireSig || opt.strict)
            {
                r.fail("meta/payload.digest.minisig missing (signature required).");
            }
            else
            {
                r.warn("meta/payload.digest.minisig missing (signature not verified). "
                       "To generate it, set VIX_MINISIGN_SECKEY when running `vix pack`.");
            }
            return;
        }

        // pubkey fallback env + defaults
        if (!opt.pubkey.has_value())
        {
            const std::string env = env_or_empty("VIX_MINISIGN_PUBKEY");
            if (!env.empty())
                opt.pubkey = fs::path(env);
        }

        if (!opt.pubkey.has_value())
        {
            const char *home = std::getenv("HOME");
            if (home && *home)
            {
                const fs::path p1 = fs::path(home) / ".config" / "vix" / "keys" / "vix-pack.pub";
                const fs::path p2 = fs::path(home) / "keys" / "vix" / "vix-pack.pub";
                if (has_file(p1))
                    opt.pubkey = p1;
                else if (has_file(p2))
                    opt.pubkey = p2;
            }
        }

        if (!opt.pubkey.has_value())
        {
            if (opt.requireSig || opt.strict)
                r.fail("--pubkey is required (or set VIX_MINISIGN_PUBKEY) to verify signature.");
            else
                r.warn("No pubkey provided; skipping signature verification. Use --pubkey or VIX_MINISIGN_PUBKEY.");
            return;
        }

        if (!tool_exists_posix("minisign"))
        {
            if (opt.requireSig || opt.strict)
                r.fail("minisign not available (signature required).");
            else
                r.warn("minisign not available; skipping signature verification.");
            return;
        }

        const bool ok = minisign_verify_payload_digest_posix(packRoot, *opt.pubkey);
        if (!ok)
            r.fail("minisign verification failed (payload.digest).");
        else if (opt.verbose)
            vix::cli::style::step("signature ok (minisign)");
    }

#endif // !_WIN32

    fs::path resolve_pack_root_from_input(const fs::path &input, std::optional<fs::path> &tmpToCleanup)
    {
#ifndef _WIN32
        if (has_file(input) && input.extension() == ".vixpkg")
        {
            const auto tmp = extract_vixpkg_to_temp_posix(input);
            if (!tmp.has_value())
                throw std::runtime_error("Unable to extract .vixpkg (need unzip).");
            tmpToCleanup = *tmp;
            return *tmp;
        }
#endif
        if (has_dir(input))
            return input;

        throw std::runtime_error("Path not found or unsupported: " + input.string());
    }

    void cleanup_temp_dir(std::optional<fs::path> &tmpToCleanup)
    {
        if (!tmpToCleanup.has_value())
            return;

        std::error_code ec;
        fs::remove_all(*tmpToCleanup, ec);
        tmpToCleanup.reset();
    }

    int print_report_and_return(const VerifyReport &r)
    {
        if (!r.warnings.empty())
        {
            vix::cli::style::info("Warnings:");
            for (const auto &w : r.warnings)
                vix::cli::style::hint(w);
        }

        if (!r.ok)
        {
            vix::cli::style::error("Verification failed.");
            for (const auto &e : r.errors)
                vix::cli::style::error(" - " + e);
            return 1;
        }

        vix::cli::style::success("Verification OK.");
        return 0;
    }

} // namespace

namespace vix::commands::VerifyCommand
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
            vix::cli::style::error(std::string("verify: ") + ex.what());
            vix::cli::style::hint("Try: vix verify --help");
            return 1;
        }

        const fs::path input = choose_path_to_verify(opt);
        const fs::path cwd = fs::current_path();
        const bool autoDetected = !opt.path.has_value() && (input != cwd);

        vix::cli::style::section_title(std::cout, "vix verify");

        VerifyReport report;

        std::optional<fs::path> tmp;
        fs::path packRoot;

        try
        {
            packRoot = resolve_pack_root_from_input(input, tmp);
        }
        catch (const std::exception &ex)
        {
            vix::cli::style::error(ex.what());
            cleanup_temp_dir(tmp);
            return 1;
        }

        vix::cli::style::info("Target:");
        vix::cli::style::step(packRoot.string());
        if (opt.verbose && autoDetected)
        {
            vix::cli::style::info("Auto-detected package:");
            vix::cli::style::step(safe_relative_or_abs(packRoot, cwd));
        }

        const fs::path manifestPath = packRoot / "manifest.json";
        if (!has_file(manifestPath))
        {
            report.fail("manifest.json is missing.");
            if (!opt.path.has_value())
            {
                vix::cli::style::hint("If you are in a project folder, run: vix verify --path ./dist/<name>@<version>");
                vix::cli::style::hint("Or run `vix pack` first.");
            }
            const int code = print_report_and_return(report);
            cleanup_temp_dir(tmp);
            return code;
        }

        const auto manifestOpt = read_json_file_safe(manifestPath);
        if (!manifestOpt.has_value())
        {
            report.fail("manifest.json is invalid JSON.");
            const int code = print_report_and_return(report);
            cleanup_temp_dir(tmp);
            return code;
        }

        const nlohmann::json manifest = *manifestOpt;

        verify_manifest_minimal(manifest, report);

#ifndef _WIN32
        verify_payload_digest_posix(packRoot, manifest, opt, report);

        if (!opt.noHash)
            verify_checksums_sha256_posix(packRoot, opt, report);
        else if (opt.verbose)
            vix::cli::style::step("checksums verification skipped (--no-hash)");

        verify_signature_posix(packRoot, opt, report);
#else
        if (opt.strict)
            report.warn("Windows: only manifest checks are implemented for now.");
#endif

        const int code = print_report_and_return(report);
        cleanup_temp_dir(tmp);
        return code;
    }

    int help()
    {
        std::ostream &out = std::cout;

        out << "Usage:\n";
        out << "  vix verify [options]\n";
        out << "  vix verify --path <folder|artifact.vixpkg>\n\n";

        out << "Description:\n";
        out << "  Verify a Vix package against the vix.manifest.v2 schema.\n";
        out << "  By default, it auto-detects the latest dist/<name>@<version> when run\n";
        out << "  from a project directory.\n\n";

        out << "Auto-detection:\n";
        out << "  - If current dir contains manifest.json -> verify current dir\n";
        out << "  - Else if current dir contains CMakeLists.txt -> verify latest dist/*/manifest.json\n";
        out << "  - Else if ./dist exists -> verify latest dist/*/manifest.json\n\n";

        out << "Options:\n";
        out << "  -p, --path <path>          Package folder or .vixpkg artifact (default: auto)\n";
        out << "  --pubkey <path>            minisign public key (or set VIX_MINISIGN_PUBKEY)\n";
        out << "  --verbose                  Print detailed checks and diagnostics\n";
        out << "  --strict                   Fail on missing optional security metadata\n";
        out << "  --require-signature        Fail if signature is missing or cannot be verified\n";
        out << "  --no-sig                   Skip signature verification\n";
        out << "  --no-hash                  Skip checksums.sha256 verification\n";
        out << "  -h, --help                 Show this help\n\n";

        out << "Signature behavior:\n";
        out << "  - If meta/payload.digest.minisig is missing: warning by default\n";
        out << "  - With --require-signature or --strict: it becomes an error\n";
        out << "  - Public key resolution order: --pubkey, VIX_MINISIGN_PUBKEY, default locations\n\n";

        out << "Exit codes:\n";
        out << "  0  Verification OK\n";
        out << "  1  Verification failed\n\n";

        out << "Examples:\n";
        out << "  vix verify\n";
        out << "  vix verify --verbose\n";
        out << "  vix verify --path ./dist/blog@1.0.0\n";
        out << "  vix verify --require-signature\n";
#ifndef _WIN32
        out << "  vix verify --path ./dist/blog@1.0.0.vixpkg\n";
        out << "  vix verify --pubkey ./keys/vix-pack.pub --require-signature\n";
        out << "  VIX_MINISIGN_PUBKEY=./keys/vix-pack.pub vix verify --strict\n";
#endif

        return 0;
    }

} // namespace vix::commands::VerifyCommand
