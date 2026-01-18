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
#include <fstream>
#include <sstream>

#include <vix/cli/util/Fs.hpp>

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

  std::string compute_project_files_fingerprint(const fs::path &projectDir)
  {
    std::vector<fs::path> files;
    files.reserve(256);

    files.push_back(projectDir / "CMakeLists.txt");
    collect_files_recursive(projectDir / "cmake", ".cmake", files);

    const fs::path presets = projectDir / "CMakePresets.json";
    if (file_exists(presets))
      files.push_back(presets);

    std::sort(files.begin(), files.end());

    std::uint64_t h = FNV_OFFSET;

    for (const auto &p : files)
    {
      std::error_code ec{};
      const fs::path rp = fs::weakly_canonical(p, ec);
      const std::string pathStr = ec ? p.string() : rp.string();

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
