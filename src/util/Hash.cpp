/**
 *
 *  @file Hash.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/util/Hash.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include <vix/cli/util/Fs.hpp>
#include <vix/crypto/hash.hpp>
#include <vix/crypto/hex.hpp>

namespace vix::cli::util
{
  static constexpr std::uint64_t FNV_OFFSET = 1469598103934665603ull;
  static constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

  std::uint64_t fnv1a64_bytes(const void *data, std::size_t n, std::uint64_t seed)
  {
    const auto *p = static_cast<const std::uint8_t *>(data);
    std::uint64_t h = seed;

    for (std::size_t i = 0; i < n; ++i)
    {
      h ^= static_cast<std::uint64_t>(p[i]);
      h *= FNV_PRIME;
    }

    return h;
  }

  std::uint64_t fnv1a64_str(const std::string &s, std::uint64_t seed)
  {
    return fnv1a64_bytes(s.data(), s.size(), seed);
  }

  std::string hex64(std::uint64_t v)
  {
    std::ostringstream oss;
    oss << std::hex;
    oss.width(16);
    oss.fill('0');
    oss << v;
    return oss.str();
  }

  std::optional<std::string> read_file_hash_hex(const fs::path &p)
  {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs)
      return std::nullopt;

    std::uint64_t h = FNV_OFFSET;

    std::string buf(64 * 1024, '\0');
    while (ifs)
    {
      ifs.read(&buf[0], static_cast<std::streamsize>(buf.size()));
      const std::streamsize got = ifs.gcount();
      if (got <= 0)
        break;

      h = fnv1a64_bytes(buf.data(), static_cast<std::size_t>(got), h);
    }

    return hex64(h);
  }

  std::optional<std::string> sha256_file(const fs::path &p)
  {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs)
      return std::nullopt;

    std::vector<char> buf((std::istreambuf_iterator<char>(ifs)),
                          (std::istreambuf_iterator<char>()));

    std::uint8_t out[32];
    auto res = vix::crypto::sha256(std::string_view(buf.data(), buf.size()), out);
    if (!res)
      return std::nullopt;

    return vix::crypto::hex_lower(out);
  }

  std::optional<std::string> sha256_directory(const fs::path &dir)
  {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
      return std::nullopt;

    std::vector<fs::path> files;
    for (const auto &it : fs::recursive_directory_iterator(dir, ec))
    {
      if (ec)
        break;
      if (it.is_regular_file())
        files.push_back(it.path());
    }

    if (ec)
      return std::nullopt;

    std::sort(files.begin(), files.end());

    std::string combined;
    for (const auto &p : files)
    {
      auto rel = fs::relative(p, dir).string();
      auto h = sha256_file(p);
      if (!h)
        return std::nullopt;
      combined += rel + ":" + *h + "\n";
    }

    std::uint8_t out[32];
    auto res = vix::crypto::sha256(combined, out);
    if (!res)
      return std::nullopt;

    return vix::crypto::hex_lower(out);
  }


  namespace
  {
    std::string shell_quote_hash_path(const fs::path &path)
    {
      std::string s = path.string();
      std::string out;
      out.reserve(s.size() + 2);
      out.push_back('\'');
      for (char c : s)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out.push_back(c);
      }
      out.push_back('\'');
      return out;
    }

    std::optional<std::string> run_capture_binary(const std::string &cmd)
    {
#ifdef _WIN32
      FILE *pipe = _popen(cmd.c_str(), "rb");
#else
      FILE *pipe = popen(cmd.c_str(), "r");
#endif
      if (!pipe)
        return std::nullopt;

      std::string out;
      char buf[8192];
      while (true)
      {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), pipe);
        if (n > 0)
          out.append(buf, n);
        if (n < sizeof(buf))
        {
          if (std::feof(pipe))
            break;
          if (std::ferror(pipe))
            break;
        }
      }

#ifdef _WIN32
      const int rc = _pclose(pipe);
#else
      const int rc = pclose(pipe);
#endif
      if (rc != 0)
        return std::nullopt;

      return out;
    }

    bool package_hash_skip_fallback_path(const fs::path &rel)
    {
      for (const auto &part : rel)
      {
        const std::string item = part.string();
        if (item == ".git" || item == ".vix" || item == "build" ||
            item == "CMakeFiles" || item == "_deps")
          return true;
      }

      const std::string filename = rel.filename().string();
      return filename == "CMakeCache.txt" ||
             filename == "cmake_install.cmake" ||
             filename == "compile_commands.json" ||
             filename == "CTestTestfile.cmake";
    }

    std::string package_file_mode(const fs::path &path, const fs::file_status &status)
    {
      if (fs::is_symlink(status))
        return "120000";

      std::error_code ec;
      const fs::perms perms = fs::status(path, ec).permissions();
      if (!ec && (perms & fs::perms::owner_exec) != fs::perms::none)
        return "100755";

      return "100644";
    }

    std::optional<std::string> package_entry_payload(
        const fs::path &root,
        const std::string &mode,
        const std::string &rel)
    {
      const fs::path path = root / fs::path(rel);
      std::error_code ec;
      const fs::file_status st = fs::symlink_status(path, ec);
      if (ec || st.type() == fs::file_type::not_found)
        return std::nullopt;

      if (fs::is_symlink(st))
      {
        const fs::path target = fs::read_symlink(path, ec);
        if (ec)
          return std::nullopt;
        return rel + ":" + mode + ":symlink:" + target.generic_string() + "\n";
      }

      if (!fs::is_regular_file(st))
        return std::nullopt;

      const auto h = sha256_file(path);
      if (!h)
        return std::nullopt;

      return rel + ":" + mode + ":file:" + *h + "\n";
    }

    std::optional<std::vector<std::pair<std::string, std::string>>>
    git_tracked_entries(const fs::path &dir)
    {
      const std::string cmd = "git -C " + shell_quote_hash_path(dir) +
                              " ls-files -s -z --recurse-submodules 2>/dev/null";
      const auto out = run_capture_binary(cmd);
      if (!out)
        return std::nullopt;

      std::vector<std::pair<std::string, std::string>> entries;
      std::size_t pos = 0;
      while (pos < out->size())
      {
        const std::size_t end = out->find('\0', pos);
        if (end == std::string::npos)
          return std::nullopt;

        const std::string rec = out->substr(pos, end - pos);
        pos = end + 1;
        if (rec.empty())
          continue;

        const std::size_t tab = rec.find('\t');
        if (tab == std::string::npos)
          return std::nullopt;

        const std::string meta = rec.substr(0, tab);
        const std::string rel = rec.substr(tab + 1);
        const std::size_t sp = meta.find(' ');
        if (sp == std::string::npos || rel.empty())
          return std::nullopt;

        entries.emplace_back(rel, meta.substr(0, sp));
      }

      std::sort(entries.begin(), entries.end(),
                [](const auto &a, const auto &b)
                {
                  return a.first < b.first;
                });
      return entries;
    }

    std::optional<std::string> sha256_package_directory_from_git(const fs::path &dir)
    {
      const auto entries = git_tracked_entries(dir);
      if (!entries)
        return std::nullopt;

      std::string combined;
      for (const auto &[rel, mode] : *entries)
      {
        const auto payload = package_entry_payload(dir, mode, rel);
        if (!payload)
          return std::nullopt;
        combined += *payload;
      }

      std::uint8_t out[32];
      auto res = vix::crypto::sha256(combined, out);
      if (!res)
        return std::nullopt;

      return vix::crypto::hex_lower(out);
    }

    std::optional<std::string> sha256_package_directory_fallback(const fs::path &dir)
    {
      std::error_code ec;
      std::vector<std::pair<std::string, std::string>> entries;

      for (const auto &it : fs::recursive_directory_iterator(
               dir,
               fs::directory_options::skip_permission_denied,
               ec))
      {
        if (ec)
          break;

        const fs::path relPath = fs::relative(it.path(), dir, ec);
        if (ec)
          return std::nullopt;
        if (package_hash_skip_fallback_path(relPath))
          continue;

        const fs::file_status st = it.symlink_status(ec);
        if (ec)
          return std::nullopt;

        if (fs::is_regular_file(st) || fs::is_symlink(st))
          entries.emplace_back(relPath.generic_string(), package_file_mode(it.path(), st));
      }

      if (ec)
        return std::nullopt;

      std::sort(entries.begin(), entries.end(),
                [](const auto &a, const auto &b)
                {
                  return a.first < b.first;
                });

      std::string combined;
      for (const auto &[rel, mode] : entries)
      {
        const auto payload = package_entry_payload(dir, mode, rel);
        if (!payload)
          return std::nullopt;
        combined += *payload;
      }

      std::uint8_t out[32];
      auto res = vix::crypto::sha256(combined, out);
      if (!res)
        return std::nullopt;

      return vix::crypto::hex_lower(out);
    }
  }

  std::optional<std::string> sha256_package_directory(const fs::path &dir)
  {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
      return std::nullopt;

    if (auto gitHash = sha256_package_directory_from_git(dir))
      return gitHash;

    return sha256_package_directory_fallback(dir);
  }

  std::string compute_cmake_config_fingerprint(const fs::path &projectDir)
  {
    std::vector<fs::path> files;
    files.reserve(256);

    const fs::path rootCMake = projectDir / "CMakeLists.txt";
    if (file_exists(rootCMake))
      files.push_back(rootCMake);

    collect_files_recursive(projectDir / "cmake", ".cmake", files);

    const fs::path presets = projectDir / "CMakePresets.json";
    if (file_exists(presets))
      files.push_back(presets);

    const fs::path userPresets = projectDir / "CMakeUserPresets.json";
    if (file_exists(userPresets))
      files.push_back(userPresets);

    const fs::path vixJson = projectDir / "vix.json";
    if (file_exists(vixJson))
      files.push_back(vixJson);

    const fs::path vixLock = projectDir / "vix.lock";
    if (file_exists(vixLock))
      files.push_back(vixLock);

    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    std::uint64_t h = FNV_OFFSET;

    for (const auto &p : files)
    {
      std::error_code ec{};
      fs::path rel = fs::relative(p, projectDir, ec);

      if (ec)
        rel = p.filename();

      const std::string pathStr = rel.generic_string();

      const auto hashOpt = read_file_hash_hex(p);
      const std::string line = pathStr + "=" + (hashOpt ? *hashOpt : "<missing>");

      h = fnv1a64_str(line, h);
    }

    return hex64(h);
  }

  bool signature_matches(const fs::path &sigFile, const std::string &sig)
  {
    const std::string old = read_text_file_or_empty(sigFile);
    return !old.empty() && old == sig;
  }

  std::string signature_join(const std::vector<std::pair<std::string, std::string>> &kvs)
  {
    std::ostringstream oss;
    for (const auto &kv : kvs)
      oss << kv.first << "=" << kv.second << "\n";
    return oss.str();
  }

} // namespace vix::cli::util
