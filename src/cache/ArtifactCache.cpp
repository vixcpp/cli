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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Strings.hpp>
#include <vix/utils/Env.hpp>

namespace vix::cli::cache
{
  namespace util = vix::cli::util;

  namespace
  {
    static constexpr std::uint64_t FNV_OFFSET = 1469598103934665603ull;
    static constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

    static std::uint64_t fnv_mix(std::uint64_t h, const void *data, std::size_t len)
    {
      const auto *p = static_cast<const unsigned char *>(data);

      for (std::size_t i = 0; i < len; ++i)
      {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= FNV_PRIME;
      }

      return h;
    }

    static std::uint64_t fnv_mix_string(std::uint64_t h, const std::string &s)
    {
      return fnv_mix(h, s.data(), s.size());
    }

    static std::uint64_t fnv_mix_u64(std::uint64_t h, std::uint64_t v)
    {
      return fnv_mix(h, &v, sizeof(v));
    }

    static std::string hex64(std::uint64_t v)
    {
      static constexpr char digits[] = "0123456789abcdef";

      std::string out(16, '0');
      for (int i = 15; i >= 0; --i)
      {
        out[static_cast<std::size_t>(i)] = digits[v & 0x0f];
        v >>= 4;
      }

      return out;
    }

    static std::uint64_t now_unix_ms()
    {
      using namespace std::chrono;

      return static_cast<std::uint64_t>(
          duration_cast<milliseconds>(
              system_clock::now().time_since_epoch())
              .count());
    }

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

    static std::string normalize_rel_path(fs::path p)
    {
      p = p.lexically_normal();

      std::string s = p.generic_string();

      while (!s.empty() && s.rfind("./", 0) == 0)
        s.erase(0, 2);

      return s;
    }

    static bool path_has_component(const fs::path &p, const std::string &name)
    {
      for (const auto &part : p)
      {
        if (part == name)
          return true;
      }

      return false;
    }

    static bool should_skip_dir(const fs::path &p)
    {
      const std::string name = p.filename().string();

      if (name.empty())
        return false;

      if (name == ".git" ||
          name == ".hg" ||
          name == ".svn" ||
          name == ".vix" ||
          name == "node_modules" ||
          name == ".cache" ||
          name == ".idea" ||
          name == ".vscode")
      {
        return true;
      }

      if (name.rfind("build", 0) == 0)
        return true;

      if (name == "cmake-build-debug" ||
          name == "cmake-build-release")
      {
        return true;
      }

      return false;
    }

    static bool is_tracked_project_input(const fs::path &relative)
    {
      const std::string filename = relative.filename().string();
      const std::string ext = relative.extension().string();

      if (filename == "CMakeLists.txt" ||
          filename == "CMakePresets.json" ||
          filename == "vix.json" ||
          filename == "vix.toml" ||
          filename == "vix.lock" ||
          filename == "package.vix" ||
          filename == ".vixrc")
      {
        return true;
      }

      if (path_has_component(relative, "cmake"))
        return true;

      if (ext == ".cmake" ||
          ext == ".cpp" ||
          ext == ".cc" ||
          ext == ".cxx" ||
          ext == ".c" ||
          ext == ".hpp" ||
          ext == ".hh" ||
          ext == ".hxx" ||
          ext == ".h" ||
          ext == ".ipp" ||
          ext == ".ixx" ||
          ext == ".cppm" ||
          ext == ".c++m" ||
          ext == ".mpp")
      {
        return true;
      }

      return false;
    }

    static std::uint64_t file_mtime_count(const fs::path &path)
    {
      std::error_code ec;
      const auto t = fs::last_write_time(path, ec);

      if (ec)
        return 0;

      return static_cast<std::uint64_t>(t.time_since_epoch().count());
    }

    static std::uint64_t file_size_or_zero(const fs::path &path)
    {
      std::error_code ec;
      const auto s = fs::file_size(path, ec);

      if (ec)
        return 0;

      return static_cast<std::uint64_t>(s);
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

    static std::string state_escape(const std::string &value)
    {
      std::string out;
      out.reserve(value.size() + 8);

      for (char c : value)
      {
        switch (c)
        {
        case '\\':
          out += "\\\\";
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
        case '=':
          out += "\\e";
          break;
        default:
          out.push_back(c);
          break;
        }
      }

      return out;
    }

    static std::string state_unescape(const std::string &value)
    {
      std::string out;
      out.reserve(value.size());

      bool escaped = false;

      for (char c : value)
      {
        if (!escaped)
        {
          if (c == '\\')
          {
            escaped = true;
            continue;
          }

          out.push_back(c);
          continue;
        }

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
        case 'e':
          out.push_back('=');
          break;
        case '\\':
          out.push_back('\\');
          break;
        default:
          out.push_back(c);
          break;
        }

        escaped = false;
      }

      if (escaped)
        out.push_back('\\');

      return out;
    }

    static std::optional<std::uint64_t> parse_u64(const std::string &s)
    {
      if (s.empty())
        return std::nullopt;

      try
      {
        std::size_t pos = 0;
        const auto v = std::stoull(s, &pos, 10);

        if (pos != s.size())
          return std::nullopt;

        return static_cast<std::uint64_t>(v);
      }
      catch (...)
      {
        return std::nullopt;
      }
    }

    static std::optional<std::uint32_t> parse_u32(const std::string &s)
    {
      const auto v = parse_u64(s);
      if (!v)
        return std::nullopt;

      if (*v > 0xffffffffull)
        return std::nullopt;

      return static_cast<std::uint32_t>(*v);
    }

    static std::optional<std::string> user_home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
      if (home && *home)
        return std::string(home);

      const char *drive = vix::utils::vix_getenv("HOMEDRIVE");
      const char *path = vix::utils::vix_getenv("HOMEPATH");
      if (drive && *drive && path && *path)
        return std::string(drive) + std::string(path);

      return std::nullopt;
#else
      const char *home = vix::utils::vix_getenv("HOME");
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

    static std::unordered_map<std::string, ProjectInput>
    make_previous_input_map(const std::vector<ProjectInput> *previousInputs)
    {
      std::unordered_map<std::string, ProjectInput> out;

      if (!previousInputs)
        return out;

      out.reserve(previousInputs->size());

      for (const auto &input : *previousInputs)
        out.emplace(input.path, input);

      return out;
    }

    static std::optional<ProjectInput> reuse_previous_input(
        const std::unordered_map<std::string, ProjectInput> &previous,
        const std::string &path,
        std::uint64_t size,
        std::uint64_t mtime)
    {
      const auto it = previous.find(path);
      if (it == previous.end())
        return std::nullopt;

      const ProjectInput &old = it->second;

      if (old.size != size)
        return std::nullopt;

      if (old.mtime != mtime)
        return std::nullopt;

      return old;
    }

    static bool parse_key_value_line(
        const std::string &line,
        std::string &key,
        std::string &value)
    {
      const auto pos = line.find('=');
      if (pos == std::string::npos)
        return false;

      key = line.substr(0, pos);
      value = line.substr(pos + 1);
      return true;
    }

    static bool parse_input_line(const std::string &value, ProjectInput &out)
    {
      std::vector<std::string> parts;
      parts.reserve(4);

      std::size_t start = 0;

      while (true)
      {
        const auto pos = value.find('\t', start);
        if (pos == std::string::npos)
        {
          parts.push_back(value.substr(start));
          break;
        }

        parts.push_back(value.substr(start, pos - start));
        start = pos + 1;
      }

      if (parts.size() != 4)
        return false;

      const auto size = parse_u64(parts[1]);
      const auto mtime = parse_u64(parts[2]);
      const auto hash = parse_u64(parts[3]);

      if (!size || !mtime || !hash)
        return false;

      out.path = state_unescape(parts[0]);
      out.size = *size;
      out.mtime = *mtime;
      out.hash = *hash;

      return !out.path.empty();
    }

    static std::string index_record_for_artifact(const Artifact &a)
    {
      const Artifact resolved = materialize_artifact_paths(a);

      std::ostringstream oss;
      oss << "key=" << state_escape(ArtifactCache::artifact_key(a));
      oss << "\tpackage=" << state_escape(a.package);
      oss << "\tversion=" << state_escape(a.version);
      oss << "\ttarget=" << state_escape(a.target);
      oss << "\tcompiler=" << state_escape(a.compiler);
      oss << "\tbuildType=" << state_escape(a.buildType);
      oss << "\tfingerprint=" << state_escape(a.fingerprint);
      oss << "\troot=" << state_escape(resolved.root.string());
      oss << "\tupdatedUnixMs=" << now_unix_ms();

      return oss.str();
    }

    static bool parse_index_record(
        const std::string &line,
        ArtifactIndexEntry &entry)
    {
      std::size_t start = 0;

      while (true)
      {
        const auto end = line.find('\t', start);
        const std::string token =
            end == std::string::npos
                ? line.substr(start)
                : line.substr(start, end - start);

        std::string key;
        std::string value;

        if (parse_key_value_line(token, key, value))
        {
          value = state_unescape(value);

          if (key == "key")
            entry.key = value;
          else if (key == "package")
            entry.package = value;
          else if (key == "version")
            entry.version = value;
          else if (key == "target")
            entry.target = value;
          else if (key == "compiler")
            entry.compiler = value;
          else if (key == "buildType")
            entry.buildType = value;
          else if (key == "fingerprint")
            entry.fingerprint = value;
          else if (key == "root")
            entry.root = value;
          else if (key == "updatedUnixMs")
          {
            const auto parsed = parse_u64(value);
            if (parsed)
              entry.updatedUnixMs = *parsed;
          }
        }

        if (end == std::string::npos)
          break;

        start = end + 1;
      }

      return !entry.key.empty() &&
             !entry.package.empty() &&
             !entry.version.empty() &&
             !entry.target.empty() &&
             !entry.compiler.empty() &&
             !entry.buildType.empty() &&
             !entry.fingerprint.empty() &&
             !entry.root.empty();
    }
  } // namespace

  fs::path ArtifactCache::cache_root()
  {
    if (const auto home = user_home_dir(); home)
      return fs::path(*home) / ".vix" / "cache" / "build";

    return fs::temp_directory_path() / "vix" / "cache" / "build";
  }

  fs::path ArtifactCache::index_path()
  {
    return cache_root() / "index.vix";
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

  std::string ArtifactCache::artifact_key(const Artifact &a)
  {
    std::ostringstream oss;
    oss << sanitize_path_component(a.target) << "|";
    oss << sanitize_path_component(a.compiler) << "|";
    oss << sanitize_path_component(a.buildType) << "|";
    oss << sanitize_path_component(a.package) << "|";
    oss << sanitize_path_component(a.version) << "|";
    oss << sanitize_path_component(a.fingerprint);

    return oss.str();
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
    oss << "  \"fingerprint\": \"" << json_escape(a.fingerprint) << "\",\n";
    oss << "  \"root\": \"" << json_escape(resolved.root.string()) << "\",\n";
    oss << "  \"updated_unix_ms\": \"" << now_unix_ms() << "\"\n";
    oss << "}\n";

    if (!util::write_text_file_atomic(path, oss.str()))
      return false;

    return write_index_entry(a);
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

  bool ArtifactCache::write_index_entry(const Artifact &a)
  {
    std::string err;
    if (!util::ensure_dir(cache_root(), err))
      return false;

    std::ofstream out(index_path(), std::ios::app);
    if (!out)
      return false;

    out << index_record_for_artifact(a) << "\n";
    return static_cast<bool>(out);
  }

  std::optional<ArtifactIndexEntry> ArtifactCache::find_index_entry(const Artifact &a)
  {
    const fs::path path = index_path();

    if (!util::file_exists(path))
      return std::nullopt;

    std::ifstream in(path);
    if (!in)
      return std::nullopt;

    const std::string expectedKey = artifact_key(a);

    std::optional<ArtifactIndexEntry> best;
    std::string line;

    while (std::getline(in, line))
    {
      if (line.empty())
        continue;

      ArtifactIndexEntry entry;
      if (!parse_index_record(line, entry))
        continue;

      if (entry.key != expectedKey)
        continue;

      if (!util::dir_exists(entry.root))
        continue;

      best = entry;
    }

    return best;
  }

  fs::path ArtifactCache::build_state_path(const fs::path &buildDir)
  {
    return buildDir / ".vix-build-state";
  }

  std::optional<BuildState> ArtifactCache::read_build_state(const fs::path &buildDir)
  {
    const fs::path path = build_state_path(buildDir);

    if (!util::file_exists(path))
      return std::nullopt;

    std::ifstream in(path);
    if (!in)
      return std::nullopt;

    std::string magic;
    if (!std::getline(in, magic))
      return std::nullopt;

    if (magic != "vix-build-state-v2")
      return std::nullopt;

    BuildState state;
    std::string line;

    while (std::getline(in, line))
    {
      if (line.empty())
        continue;

      std::string key;
      std::string value;

      if (!parse_key_value_line(line, key, value))
        continue;

      if (key == "input")
      {
        ProjectInput input;
        if (parse_input_line(value, input))
          state.inputs.push_back(std::move(input));
        continue;
      }

      value = state_unescape(value);

      if (key == "schemaVersion")
      {
        const auto parsed = parse_u32(value);
        if (parsed)
          state.schemaVersion = *parsed;
      }
      else if (key == "signature")
      {
        state.signature = value;
      }
      else if (key == "projectFingerprint")
      {
        state.projectFingerprint = value;
      }
      else if (key == "inputsFingerprint")
      {
        state.inputsFingerprint = value;
      }
      else if (key == "artifactRoot")
      {
        state.artifactRoot = value;
      }
      else if (key == "lastBinary")
      {
        state.lastBinary = value;
      }
      else if (key == "buildTarget")
      {
        state.buildTarget = value;
      }
      else if (key == "preset")
      {
        state.preset = value;
      }
      else if (key == "buildType")
      {
        state.buildType = value;
      }
      else if (key == "target")
      {
        state.target = value;
      }
      else if (key == "compiler")
      {
        state.compiler = value;
      }
      else if (key == "createdUnixMs")
      {
        const auto parsed = parse_u64(value);
        if (parsed)
          state.createdUnixMs = *parsed;
      }
      else if (key == "updatedUnixMs")
      {
        const auto parsed = parse_u64(value);
        if (parsed)
          state.updatedUnixMs = *parsed;
      }
    }

    if (state.schemaVersion != 2)
      return std::nullopt;

    if (state.signature.empty() ||
        state.projectFingerprint.empty() ||
        state.inputsFingerprint.empty() ||
        state.preset.empty() ||
        state.buildType.empty())
    {
      return std::nullopt;
    }

    return state;
  }

  bool ArtifactCache::write_build_state(
      const fs::path &buildDir,
      const BuildState &state)
  {
    std::string err;
    if (!util::ensure_dir(buildDir, err))
      return false;

    BuildState outState = state;

    const std::uint64_t now = now_unix_ms();
    if (outState.createdUnixMs == 0)
      outState.createdUnixMs = now;
    outState.updatedUnixMs = now;

    const fs::path path = build_state_path(buildDir);

    std::ostringstream out;
    out << "vix-build-state-v2\n";
    out << "schemaVersion=" << outState.schemaVersion << "\n";
    out << "signature=" << state_escape(outState.signature) << "\n";
    out << "projectFingerprint=" << state_escape(outState.projectFingerprint) << "\n";
    out << "inputsFingerprint=" << state_escape(outState.inputsFingerprint) << "\n";
    out << "artifactRoot=" << state_escape(outState.artifactRoot) << "\n";
    out << "lastBinary=" << state_escape(outState.lastBinary) << "\n";
    out << "buildTarget=" << state_escape(outState.buildTarget) << "\n";
    out << "preset=" << state_escape(outState.preset) << "\n";
    out << "buildType=" << state_escape(outState.buildType) << "\n";
    out << "target=" << state_escape(outState.target) << "\n";
    out << "compiler=" << state_escape(outState.compiler) << "\n";
    out << "createdUnixMs=" << outState.createdUnixMs << "\n";
    out << "updatedUnixMs=" << outState.updatedUnixMs << "\n";
    out << "inputsCount=" << outState.inputs.size() << "\n";

    for (const auto &input : outState.inputs)
    {
      out << "input="
          << state_escape(input.path) << "\t"
          << input.size << "\t"
          << input.mtime << "\t"
          << input.hash << "\n";
    }

    return util::write_text_file_atomic(path, out.str());
  }

  std::vector<ProjectInput> ArtifactCache::snapshot_project_inputs(
      const fs::path &projectDir,
      const std::vector<ProjectInput> *previousInputs)
  {
    std::vector<ProjectInput> inputs;
    const fs::path root = fs::absolute(projectDir).lexically_normal();

    const auto previous = make_previous_input_map(previousInputs);

    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
      return inputs;

    fs::recursive_directory_iterator it(
        root,
        fs::directory_options::skip_permission_denied,
        ec);

    const fs::recursive_directory_iterator end;

    while (!ec && it != end)
    {
      const fs::path current = it->path();

      if (it->is_directory(ec))
      {
        if (should_skip_dir(current))
        {
          it.disable_recursion_pending();
        }

        ++it;
        continue;
      }

      if (!it->is_regular_file(ec))
      {
        ++it;
        continue;
      }

      fs::path relative;
      std::error_code relEc;
      relative = fs::relative(current, root, relEc);

      if (relEc)
      {
        ++it;
        continue;
      }

      relative = relative.lexically_normal();

      if (!is_tracked_project_input(relative))
      {
        ++it;
        continue;
      }

      const std::string rel = normalize_rel_path(relative);
      const std::uint64_t size = file_size_or_zero(current);
      const std::uint64_t mtime = file_mtime_count(current);

      if (auto reused = reuse_previous_input(previous, rel, size, mtime); reused)
      {
        inputs.push_back(*reused);
        ++it;
        continue;
      }

      ProjectInput input;
      input.path = rel;
      input.size = size;
      input.mtime = mtime;
      input.hash = hash_file_content(current);

      inputs.push_back(std::move(input));
      ++it;
    }

    std::sort(
        inputs.begin(),
        inputs.end(),
        [](const ProjectInput &a, const ProjectInput &b)
        {
          return a.path < b.path;
        });

    return inputs;
  }

  std::string ArtifactCache::compute_inputs_fingerprint(
      const std::vector<ProjectInput> &inputs)
  {
    std::uint64_t h = FNV_OFFSET;

    for (const auto &input : inputs)
    {
      h = fnv_mix_string(h, input.path);
      h = fnv_mix_u64(h, input.size);
      h = fnv_mix_u64(h, input.mtime);
      h = fnv_mix_u64(h, input.hash);
    }

    return hex64(h);
  }

  bool ArtifactCache::build_state_matches(
      const BuildState &state,
      const std::string &signature,
      const std::string &projectFingerprint,
      const std::string &buildTarget,
      const std::vector<ProjectInput> &currentInputs)
  {
    if (state.signature != signature)
      return false;

    if (state.projectFingerprint != projectFingerprint)
      return false;

    if (state.buildTarget != buildTarget)
      return false;

    const std::string currentInputFingerprint =
        compute_inputs_fingerprint(currentInputs);

    if (state.inputsFingerprint != currentInputFingerprint)
      return false;

    if (!state.lastBinary.empty() && !util::file_exists(state.lastBinary))
      return false;

    if (!state.artifactRoot.empty() && !util::dir_exists(state.artifactRoot))
      return false;

    return true;
  }

  BuildState ArtifactCache::make_build_state(
      const std::string &signature,
      const std::string &projectFingerprint,
      const std::string &artifactRoot,
      const std::string &lastBinary,
      const std::string &buildTarget,
      const std::string &preset,
      const std::string &buildType,
      const std::string &target,
      const std::string &compiler,
      const std::vector<ProjectInput> &inputs)
  {
    BuildState state;
    state.schemaVersion = 2;
    state.signature = signature;
    state.projectFingerprint = projectFingerprint;
    state.inputsFingerprint = compute_inputs_fingerprint(inputs);
    state.artifactRoot = artifactRoot;
    state.lastBinary = lastBinary;
    state.buildTarget = buildTarget;
    state.preset = preset;
    state.buildType = buildType;
    state.target = target;
    state.compiler = compiler;
    state.createdUnixMs = now_unix_ms();
    state.updatedUnixMs = state.createdUnixMs;
    state.inputs = inputs;

    return state;
  }

  std::uint64_t ArtifactCache::hash_string(const std::string &value)
  {
    return fnv_mix_string(FNV_OFFSET, value);
  }

  std::uint64_t ArtifactCache::hash_file_content(const fs::path &path)
  {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      return 0;

    std::uint64_t h = FNV_OFFSET;

    char buffer[64 * 1024];

    while (in)
    {
      in.read(buffer, sizeof(buffer));
      const std::streamsize n = in.gcount();

      if (n > 0)
      {
        h = fnv_mix(
            h,
            buffer,
            static_cast<std::size_t>(n));
      }
    }

    return h;
  }

} // namespace vix::cli::cache
