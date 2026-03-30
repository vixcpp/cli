/**
 *
 *  @file ArtifactCache.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Global compiled artifact cache
 *
 */

#include <vix/cli/cache/ArtifactCache.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Strings.hpp>

namespace vix::cli::cache
{
  namespace util = vix::cli::util;

  namespace
  {
    static std::string sanitize_path_component(std::string value)
    {
      for (char &c : value)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        const bool ok =
            std::isalnum(uc) || c == '.' || c == '_' || c == '-' || c == '+';

        if (!ok)
          c = '_';
      }

      if (value.empty())
        return "unknown";

      return value;
    }

    static std::string json_escape(const std::string &value)
    {
      std::string out;
      out.reserve(value.size() + 16);

      for (char c : value)
      {
        switch (c)
        {
        case '\\':
          out += "\\\\";
          break;
        case '"':
          out += "\\\"";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out.push_back(c);
          break;
        }
      }

      return out;
    }

    static std::optional<std::string> user_home_dir()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
      if (home && *home)
        return std::string(home);

      const char *drive = std::getenv("HOMEDRIVE");
      const char *path = std::getenv("HOMEPATH");
      if (drive && *drive && path && *path)
        return std::string(drive) + std::string(path);

      return std::nullopt;
#else
      const char *home = std::getenv("HOME");
      if (home && *home)
        return std::string(home);

      return std::nullopt;
#endif
    }

    static std::string extract_json_string(
        const std::string &content,
        const std::string &key)
    {
      const std::string pattern = "\"" + key + "\"";
      const auto keyPos = content.find(pattern);
      if (keyPos == std::string::npos)
        return "";

      const auto colonPos = content.find(':', keyPos + pattern.size());
      if (colonPos == std::string::npos)
        return "";

      const auto firstQuote = content.find('"', colonPos + 1);
      if (firstQuote == std::string::npos)
        return "";

      std::string out;
      out.reserve(64);

      bool escaped = false;
      for (std::size_t i = firstQuote + 1; i < content.size(); ++i)
      {
        const char c = content[i];

        if (escaped)
        {
          switch (c)
          {
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
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
          default:
            out.push_back(c);
            break;
          }
          escaped = false;
          continue;
        }

        if (c == '\\')
        {
          escaped = true;
          continue;
        }

        if (c == '"')
          return out;

        out.push_back(c);
      }

      return "";
    }

    static Artifact materialize_artifact_paths(const Artifact &a)
    {
      Artifact out = a;
      out.root = ArtifactCache::artifact_path(a);
      out.include = out.root / "include";
      out.lib = out.root / "lib";
      return out;
    }
  } // namespace

  fs::path ArtifactCache::cache_root()
  {
    if (const auto home = user_home_dir(); home)
      return fs::path(*home) / ".vix" / "cache" / "build";

    return fs::temp_directory_path() / "vix" / "cache" / "build";
  }

  fs::path ArtifactCache::artifact_path(const Artifact &a)
  {
    return cache_root() /
           sanitize_path_component(a.target) /
           sanitize_path_component(a.compiler) /
           sanitize_path_component(a.buildType) /
           sanitize_path_component(a.package + "@" + a.version) /
           sanitize_path_component(a.fingerprint);
  }

  bool ArtifactCache::exists(const Artifact &a)
  {
    const Artifact resolved = materialize_artifact_paths(a);
    const fs::path manifest = resolved.root / "manifest.json";

    return util::dir_exists(resolved.include) &&
           util::dir_exists(resolved.lib) &&
           util::file_exists(manifest);
  }

  std::optional<Artifact> ArtifactCache::resolve(const Artifact &a)
  {
    if (!exists(a))
      return std::nullopt;

    return materialize_artifact_paths(a);
  }

  bool ArtifactCache::ensure_layout(const Artifact &a)
  {
    std::string err;
    const Artifact resolved = materialize_artifact_paths(a);

    if (!util::ensure_dir(resolved.root, err))
      return false;

    if (!util::ensure_dir(resolved.include, err))
      return false;

    if (!util::ensure_dir(resolved.lib, err))
      return false;

    if (!util::ensure_dir(resolved.root / "share", err))
      return false;

    return true;
  }

  bool ArtifactCache::write_manifest(const Artifact &a)
  {
    if (!ensure_layout(a))
      return false;

    const Artifact resolved = materialize_artifact_paths(a);
    const fs::path path = resolved.root / "manifest.json";

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"package\": \"" << json_escape(a.package) << "\",\n";
    oss << "  \"version\": \"" << json_escape(a.version) << "\",\n";
    oss << "  \"target\": \"" << json_escape(a.target) << "\",\n";
    oss << "  \"compiler\": \"" << json_escape(a.compiler) << "\",\n";
    oss << "  \"build_type\": \"" << json_escape(a.buildType) << "\",\n";
    oss << "  \"fingerprint\": \"" << json_escape(a.fingerprint) << "\"\n";
    oss << "}\n";

    return util::write_text_file_atomic(path, oss.str());
  }

  std::optional<Artifact> ArtifactCache::read_manifest(const fs::path &artifactRoot)
  {
    const fs::path path = artifactRoot / "manifest.json";

    if (!util::file_exists(path))
      return std::nullopt;

    const std::string content = util::read_text_file_or_empty(path);
    if (content.empty())
      return std::nullopt;

    Artifact a;
    a.package = extract_json_string(content, "package");
    a.version = extract_json_string(content, "version");
    a.target = extract_json_string(content, "target");
    a.compiler = extract_json_string(content, "compiler");
    a.buildType = extract_json_string(content, "build_type");
    a.fingerprint = extract_json_string(content, "fingerprint");

    if (a.package.empty() ||
        a.version.empty() ||
        a.target.empty() ||
        a.compiler.empty() ||
        a.buildType.empty() ||
        a.fingerprint.empty())
    {
      return std::nullopt;
    }

    a.root = artifactRoot;
    a.include = artifactRoot / "include";
    a.lib = artifactRoot / "lib";

    return a;
  }

} // namespace vix::cli::cache
