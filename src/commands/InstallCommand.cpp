/**
 *
 *  @file InstallCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
// ============================================================================
// InstallCommand.cpp — Install a Vix package into a local store (Vix.cpp CLI)
// ----------------------------------------------------------------------------
// Features:
//   - --path <folder|artifact.vixpkg>
//   - --store <dir> (override store root)
//   - --force (overwrite installed package)
//   - --no-verify (skip verification; NOT recommended)
//   - --require-signature, --pubkey <path> (signature verification)
//   - extraction of .vixpkg (POSIX: unzip)
//   - verifies: manifest minimal + payload digest + checksums + minisign signature
//   - atomic install: copy to tmp dir then rename() into final destination
//
// Default store (POSIX):
//   1) $VIX_STORE if set
//   2) $XDG_DATA_HOME/vix
//   3) ~/.local/share/vix
//   store path becomes: <root>/packs/<name>/<version>/<os>-<arch>/
//
// Usage:
//   vix install --path ./dist/blog@1.0.0.vixpkg
//   vix install --path ./dist/blog@1.0.0 --force
//   vix install --path ./blog@1.0.0.vixpkg --require-signature --pubkey ./keys/vix-pack.pub
// ============================================================================

#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>
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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#else
#include <cctype>
#include <cstdlib>
#endif

namespace fs = std::filesystem;

namespace
{
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

  void write_text_file(const fs::path &p, const std::string &s)
  {
    ensure_dir(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    if (!out)
      throw std::runtime_error("Unable to write file: " + p.string());

    out.write(s.data(), static_cast<std::streamsize>(s.size()));
    if (!out)
      throw std::runtime_error("Failed to write file: " + p.string());
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
    const char *v = vix::utils::vix_getenv(name);
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

  // POSIX tools
#ifndef _WIN32
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

    Pipe(Pipe &&other) noexcept : pipe_(other.pipe_) { other.pipe_ = nullptr; }
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
    const std::string cmd =
        "command -v " + shell_quote_posix(tool) + " >/dev/null 2>&1 && echo ok";
    return run_and_capture_posix(cmd).find("ok") != std::string::npos;
  }
#endif

  // SHA256 checksums parser
#ifndef _WIN32
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

  std::optional<std::string> sha256_of_string_posix(const fs::path &dir, const std::string &data)
  {
    if (!tool_exists_posix("sha256sum"))
      return std::nullopt;

    const int pid = static_cast<int>(::getpid());
    const fs::path tmp = dir / (".vix_install_tmp_" + std::to_string(pid) + ".txt");

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
    fs::path tmp = fs::temp_directory_path() / ("vix_install_" + std::to_string(pid));

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

  // Verification report
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
    void warn(const std::string &s) { warnings.push_back(s); }
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
  void verify_payload_digest_posix(
      const fs::path &packRoot,
      const nlohmann::json &manifest,
      bool verbose,
      VerifyReport &r)
  {
    // expected digest from manifest
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
      r.fail("sha256sum not available; cannot verify payload digest.");
      return;
    }

    // excludes (base + manifest payload.excludes)
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
          std::string v = trim_copy(it.get<std::string>());
          if (v.empty())
            continue;
          excludes.push_back(starts_with(v, "./") ? v : ("./" + v));
        }
      }
    }

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

    if (computed != expectedManifest)
    {
      r.fail("Payload digest mismatch (computed != manifest.payload.digest).");
      if (verbose)
      {
        vix::cli::style::info("payload digest:");
        vix::cli::style::step("computed: " + computed);
        vix::cli::style::step("manifest: " + expectedManifest);
      }
      return;
    }

    // ensure meta/payload.digest matches too (if exists)
    const fs::path digestFile = packRoot / "meta" / "payload.digest";
    if (has_file(digestFile))
    {
      const std::string expectedFile = trim_copy(read_text_file(digestFile));
      if (!expectedFile.empty() && expectedFile != expectedManifest)
      {
        r.fail("Payload digest mismatch (meta/payload.digest != manifest.payload.digest).");
        if (verbose)
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
      r.warn("meta/payload.digest missing.");
    }

    if (verbose)
    {
      vix::cli::style::step("payload digest ok");
      vix::cli::style::step("digest: " + expectedManifest);
    }
  }

  void verify_checksums_sha256_posix(const fs::path &packRoot, bool verbose, VerifyReport &r)
  {
    const fs::path sums = packRoot / "checksums.sha256";
    if (!has_file(sums))
    {
      r.warn("checksums.sha256 is missing.");
      return;
    }

    if (!tool_exists_posix("sha256sum"))
    {
      r.warn("sha256sum is not available; skipping checksums verification.");
      return;
    }

    const auto expected = parse_sha256sum_file(sums);
    if (expected.empty())
    {
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

    if (verbose)
      vix::cli::style::step("checksums ok: " + std::to_string(okCount) + " file(s)");
  }

  struct SigOptions
  {
    bool requireSig{false};
    std::optional<fs::path> pubkey;
  };

  void verify_signature_posix(const fs::path &packRoot,
                              bool verbose,
                              SigOptions &sopt,
                              VerifyReport &r)
  {
    const fs::path sig = packRoot / "meta" / "payload.digest.minisig";
    if (!has_file(sig))
    {
      if (sopt.requireSig)
        r.fail("meta/payload.digest.minisig missing (signature required).");
      else
        r.warn("meta/payload.digest.minisig missing (signature not verified).");
      return;
    }

    // pubkey fallback env + defaults
    if (!sopt.pubkey.has_value())
    {
      const std::string env = env_or_empty("VIX_MINISIGN_PUBKEY");
      if (!env.empty())
        sopt.pubkey = fs::path(env);
    }

    if (!sopt.pubkey.has_value())
    {
      const char *home = vix::utils::vix_getenv("HOME");
      if (home && *home)
      {
        const fs::path p1 = fs::path(home) / ".config" / "vix" / "keys" / "vix-pack.pub";
        const fs::path p2 = fs::path(home) / "keys" / "vix" / "vix-pack.pub";
        if (has_file(p1))
          sopt.pubkey = p1;
        else if (has_file(p2))
          sopt.pubkey = p2;
      }
    }

    if (!sopt.pubkey.has_value())
    {
      if (sopt.requireSig)
        r.fail("--pubkey is required (or set VIX_MINISIGN_PUBKEY) to verify signature.");
      else
        r.warn("No pubkey provided; skipping signature verification.");
      return;
    }

    if (!tool_exists_posix("minisign"))
    {
      if (sopt.requireSig)
        r.fail("minisign not available (signature required).");
      else
        r.warn("minisign not available; skipping signature verification.");
      return;
    }

    const bool ok = minisign_verify_payload_digest_posix(packRoot, *sopt.pubkey);
    if (!ok)
      r.fail("minisign verification failed (payload.digest).");
    else if (verbose)
      vix::cli::style::step("signature ok (minisign)");
  }
#endif // !_WIN32

  // Resolve pack root (folder or .vixpkg)
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

  // Store path resolution
  fs::path default_store_root()
  {
    const std::string vixStore = env_or_empty("VIX_STORE");
    if (!vixStore.empty())
      return fs::path(vixStore);

#ifndef _WIN32
    const std::string xdg = env_or_empty("XDG_DATA_HOME");
    if (!xdg.empty())
      return fs::path(xdg) / "vix";

    const std::string home = env_or_empty("HOME");
    if (!home.empty())
      return fs::path(home) / ".local" / "share" / "vix";

    // last fallback
    return fs::temp_directory_path() / "vix";
#else
    // Windows fallback
    const std::string home = env_or_empty("USERPROFILE");
    if (!home.empty())
      return fs::path(home) / ".vix";
    return fs::temp_directory_path() / "vix";
#endif
  }

  // Copy directory tree
  void copy_tree_all(const fs::path &from, const fs::path &to, bool verbose)
  {
    if (!has_dir(from))
      throw std::runtime_error("Source folder not found: " + from.string());

    ensure_dir(to);

    std::error_code ec;
    fs::recursive_directory_iterator it(from, ec);
    if (ec)
      throw std::runtime_error("Failed to iterate directory: " + from.string() + " (" + ec.message() + ")");

    for (const auto &entry : it)
    {
      std::error_code ec2;
      const fs::path rel = fs::relative(entry.path(), from, ec2);
      if (ec2)
        throw std::runtime_error("Failed to compute relative path: " + entry.path().string());

      const fs::path dst = to / rel;

      ec2.clear();
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
          throw std::runtime_error("Failed to copy file: " + entry.path().string() + " -> " +
                                   dst.string() + " (" + ec3.message() + ")");
        }

        if (verbose)
          vix::cli::style::step("copied: " + rel.string());

        continue;
      }

      // ignore symlinks / special files for now (optional: handle later)
    }
  }

  // Atomic install (copy -> rename)
  void atomic_install_folder(
      const fs::path &packRoot,
      const fs::path &dstFinal,
      bool force,
      bool verboseCopy)
  {
    // destination exists?
    if (has_dir(dstFinal) || has_file(dstFinal))
    {
      if (!force)
        throw std::runtime_error("Package already installed: " + dstFinal.string() + " (use --force)");
      std::error_code ec;
      fs::remove_all(dstFinal, ec);
      if (ec)
        throw std::runtime_error("Unable to remove existing package: " + ec.message());
    }

    const int pid =
#ifndef _WIN32
        static_cast<int>(::getpid());
#else
        0;
#endif

    const fs::path dstTmp = dstFinal.parent_path() / (dstFinal.filename().string() + ".tmp." + std::to_string(pid));

    // cleanup stale tmp
    {
      std::error_code ec;
      fs::remove_all(dstTmp, ec);
    }

    // copy to tmp
    copy_tree_all(packRoot, dstTmp, verboseCopy);

    // ensure parent exists
    ensure_dir(dstFinal.parent_path());

    // rename tmp -> final
    std::error_code ec;
    fs::rename(dstTmp, dstFinal, ec);

    if (ec)
    {
      // fallback: cross-device rename can fail (EXDEV). Try copy+remove.
      // We already copied into same parent path, so usually it won't happen,
      // but keep a robust fallback anyway.
      vix::cli::style::hint("rename() failed, fallback to copy+remove: " + ec.message());

      // If final exists (shouldn't), remove it
      std::error_code ec2;
      fs::remove_all(dstFinal, ec2);

      // copy tmp -> final
      copy_tree_all(dstTmp, dstFinal, false);

      // remove tmp
      std::error_code ec3;
      fs::remove_all(dstTmp, ec3);
    }
  }

  // CLI options
  struct Options
  {
    std::optional<fs::path> path;
    std::optional<fs::path> store;
    bool force{false};
    bool noVerify{false};
    bool verbose{false};

    // signature verification options
    bool requireSignature{false};
    std::optional<fs::path> pubkey;
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

      if (a == "--store")
      {
        if (i + 1 >= args.size())
          throw std::runtime_error("--store requires a directory path.");
        opt.store = fs::path(args[++i]);
        continue;
      }

      if (a == "--force")
      {
        opt.force = true;
        continue;
      }

      if (a == "--no-verify")
      {
        opt.noVerify = true;
        continue;
      }

      if (a == "--verbose")
      {
        opt.verbose = true;
        continue;
      }

      if (a == "--require-signature")
      {
        opt.requireSignature = true;
        continue;
      }

      if (a == "--pubkey")
      {
        if (i + 1 >= args.size())
          throw std::runtime_error("--pubkey requires a path.");
        opt.pubkey = fs::path(args[++i]);
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

  fs::path choose_input_path_or_throw(const Options &opt)
  {
    if (opt.path.has_value())
      return *opt.path;

    // If user didn't provide --path, try a sane default:
    // - if ./dist exists, pick latest dist/* by manifest.json mtime? (optional)
    // For now keep strict: require --path.
    throw std::runtime_error("Missing --path. Try: vix install --path <folder|artifact.vixpkg>");
  }

  // Extract package identity from manifest (name/version/os/arch)
  struct PackageId
  {
    std::string name;
    std::string version;
    std::string os;
    std::string arch;

    std::string abi_tag() const { return os + "-" + arch; }
  };

  PackageId read_package_id_or_throw(const nlohmann::json &manifest)
  {
    PackageId id;

    id.name = manifest["package"]["name"].get<std::string>();
    id.version = manifest["package"]["version"].get<std::string>();
    id.os = manifest["abi"]["os"].get<std::string>();
    id.arch = manifest["abi"]["arch"].get<std::string>();

    id.name = trim_copy(id.name);
    id.version = trim_copy(id.version);
    id.os = trim_copy(id.os);
    id.arch = trim_copy(id.arch);

    if (id.name.empty() || id.version.empty() || id.os.empty() || id.arch.empty())
      throw std::runtime_error("manifest.json has empty package/abi fields.");

    return id;
  }

  int print_verify_report_and_return(const VerifyReport &r)
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

namespace vix::commands::InstallCommand
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
      vix::cli::style::error(std::string("install: ") + ex.what());
      vix::cli::style::hint("Try: vix install --help");
      return 1;
    }

    const fs::path input =
        [&]() -> fs::path
    {
      try
      {
        return choose_input_path_or_throw(opt);
      }
      catch (const std::exception &ex)
      {
        vix::cli::style::error(ex.what());
        vix::cli::style::hint("Example: vix install --path ./dist/blog@1.0.0.vixpkg");
        return {};
      }
    }();

    if (input.empty())
      return 1;

    vix::cli::style::section_title(std::cout, "vix install");

    // resolve package root (folder or extracted temp)
    std::optional<fs::path> tmp;
    fs::path packRoot;

    try
    {
      packRoot = resolve_pack_root_from_input(input, tmp);
      // after resolve_pack_root_from_input
      fs::path manifestPath = packRoot / "manifest.json";
      if (!has_file(manifestPath))
      {
        // try single subfolder
        std::vector<fs::path> dirs;
        for (auto &e : fs::directory_iterator(packRoot))
        {
          if (e.is_directory())
            dirs.push_back(e.path());
        }
        if (dirs.size() == 1 && has_file(dirs[0] / "manifest.json"))
        {
          packRoot = dirs[0];
          manifestPath = packRoot / "manifest.json";
        }
      }
    }
    catch (const std::exception &ex)
    {
      vix::cli::style::error(ex.what());
      cleanup_temp_dir(tmp);
      return 1;
    }

    vix::cli::style::info("Source:");
    vix::cli::style::step(safe_relative_or_abs(input, fs::current_path()));

    vix::cli::style::info("Pack root:");
    vix::cli::style::step(packRoot.string());

    // read manifest
    const fs::path manifestPath = packRoot / "manifest.json";
    if (!has_file(manifestPath))
    {
      vix::cli::style::error("manifest.json is missing in pack.");
      vix::cli::style::hint("Make sure you pass a valid package folder or .vixpkg artifact.");
      cleanup_temp_dir(tmp);
      return 1;
    }

    const auto manifestOpt = read_json_file_safe(manifestPath);
    if (!manifestOpt.has_value())
    {
      vix::cli::style::error("manifest.json is invalid JSON.");
      cleanup_temp_dir(tmp);
      return 1;
    }

    const nlohmann::json manifest = *manifestOpt;

    // minimal manifest validation + read ID
    VerifyReport report;
    verify_manifest_minimal(manifest, report);

    PackageId pkg;
    try
    {
      if (report.ok)
        pkg = read_package_id_or_throw(manifest);
    }
    catch (const std::exception &ex)
    {
      report.fail(std::string("Cannot read package identity: ") + ex.what());
    }

#ifndef _WIN32
    // verification (default)
    if (!opt.noVerify)
    {
      vix::cli::style::info("Verifying:");

      verify_payload_digest_posix(packRoot, manifest, opt.verbose, report);
      verify_checksums_sha256_posix(packRoot, opt.verbose, report);

      SigOptions sopt;
      sopt.requireSig = opt.requireSignature;
      sopt.pubkey = opt.pubkey;

      verify_signature_posix(packRoot, opt.verbose, sopt, report);
    }
    else
    {
      vix::cli::style::hint("Verification skipped (--no-verify).");
    }
#else
    if (!opt.noVerify)
      vix::cli::style::hint("Windows: only minimal manifest validation is available right now.");
#endif

    if (!report.ok)
    {
      const int code = print_verify_report_and_return(report);
      cleanup_temp_dir(tmp);
      return code;
    }

    // compute store destination
    const fs::path storeRoot = opt.store.value_or(default_store_root());
    const fs::path dstFinal = storeRoot / "packs" / pkg.name / pkg.version / pkg.abi_tag();

    vix::cli::style::info("Target store:");
    vix::cli::style::step(storeRoot.string());

    vix::cli::style::info("Installing:");
    vix::cli::style::step(pkg.name + "@" + pkg.version + " (" + pkg.abi_tag() + ")");
    vix::cli::style::step(dstFinal.string());

    try
    {
      // atomic install: copy -> rename
      // NOTE: if input is a folder inside dist/, and store is elsewhere,
      // we still do copy into storeRoot and then atomic rename within same parent.
      // The "atomic" part is for finalization of the install.
      atomic_install_folder(packRoot, dstFinal, opt.force, opt.verbose);
    }
    catch (const std::exception &ex)
    {
      vix::cli::style::error(std::string("install failed: ") + ex.what());
      cleanup_temp_dir(tmp);
      return 1;
    }

    cleanup_temp_dir(tmp);

    vix::cli::style::success("Package installed:");
    vix::cli::style::step(dstFinal.string());

    vix::cli::style::hint("Next: vix get " + pkg.name + "@" + pkg.version + " --out ./<folder> (coming soon)");
    return 0;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix install --path <folder|artifact.vixpkg> [options]\n\n";

    out << "Description:\n";
    out << "  Install a Vix package into a local store (cache/store).\n";
    out << "  By default, it verifies payload digest, checksums, and signature (if present).\n\n";

    out << "Options:\n";
    out << "  -p, --path <path>          Package folder or .vixpkg artifact (required)\n";
    out << "  --store <dir>              Override store root (default: VIX_STORE, XDG_DATA_HOME/vix, ~/.local/share/vix)\n";
    out << "  --force                    Overwrite if already installed\n";
    out << "  --no-verify                Skip verification (NOT recommended)\n";
    out << "  --verbose                  Print detailed checks and copied files\n";
    out << "  --require-signature        Fail if signature is missing or cannot be verified\n";
    out << "  --pubkey <path>            minisign public key (or set VIX_MINISIGN_PUBKEY)\n";
    out << "  -h, --help                 Show this help\n\n";

    out << "Store layout:\n";
    out << "  <store>/packs/<name>/<version>/<os>-<arch>/\n\n";

    out << "Examples:\n";
    out << "  vix install --path ./dist/blog@1.0.0.vixpkg\n";
    out << "  vix install --path ./dist/blog@1.0.0 --force\n";
    out << "  vix install --path ./blog@1.0.0.vixpkg --require-signature --pubkey ./keys/vix-pack.pub\n";

    return 0;
  }

} // namespace vix::commands::InstallCommand
