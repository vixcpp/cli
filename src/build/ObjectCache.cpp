/**
 *
 *  @file ObjectCache.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Object file cache
 *
 */

#include <vix/cli/build/ObjectCache.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <system_error>
#include <cstdlib>

#include <vix/cli/build/DependencyFile.hpp>

namespace vix::cli::build
{
  namespace
  {
    static constexpr std::uint64_t FNV_OFFSET = 1469598103934665603ull;
    static constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

    static fs::path global_object_cache_root()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
#endif

      if (home && *home)
        return fs::path(home) / ".vix" / "cache" / "objects";

      return fs::current_path() / ".vix" / "cache" / "objects";
    }

    static std::uint64_t fnv_mix(
        std::uint64_t h,
        const void *data,
        std::size_t len)
    {
      const auto *p = static_cast<const unsigned char *>(data);

      for (std::size_t i = 0; i < len; ++i)
      {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= FNV_PRIME;
      }

      return h;
    }

    static std::uint64_t fnv_mix_string(
        std::uint64_t h,
        const std::string &value)
    {
      return fnv_mix(h, value.data(), value.size());
    }

    static std::string hex64(std::uint64_t value)
    {
      static constexpr char digits[] = "0123456789abcdef";

      std::string out(16, '0');

      for (int i = 15; i >= 0; --i)
      {
        out[static_cast<std::size_t>(i)] = digits[value & 0x0f];
        value >>= 4;
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

    static std::uint64_t file_size_or_zero(const fs::path &path)
    {
      std::error_code ec;
      const auto size = fs::file_size(path, ec);

      if (ec)
        return 0;

      return static_cast<std::uint64_t>(size);
    }

    static bool file_exists(const fs::path &path)
    {
      std::error_code ec;
      return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

    static bool dir_exists(const fs::path &path)
    {
      std::error_code ec;
      return fs::exists(path, ec) && fs::is_directory(path, ec);
    }

    static bool ensure_dir(const fs::path &path)
    {
      std::error_code ec;

      if (path.empty())
        return false;

      if (dir_exists(path))
        return true;

      return fs::create_directories(path, ec) || dir_exists(path);
    }

    static std::string normalize_path_string(const fs::path &path)
    {
      return path.lexically_normal().generic_string();
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

    static std::uint64_t extract_json_u64(
        const std::string &content,
        const std::string &key)
    {
      const std::string value = extract_json_string(content, key);

      if (value.empty())
        return 0;

      try
      {
        std::size_t pos = 0;
        const auto parsed = std::stoull(value, &pos, 10);

        if (pos != value.size())
          return 0;

        return static_cast<std::uint64_t>(parsed);
      }
      catch (...)
      {
        return 0;
      }
    }

    static std::string read_text_file_or_empty(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};

      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }

    static bool write_text_file_atomic(
        const fs::path &path,
        const std::string &content)
    {
      const fs::path parent = path.parent_path();

      if (!parent.empty() && !ensure_dir(parent))
        return false;

      const fs::path tmp = path.string() + ".tmp";

      {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
          return false;

        out << content;

        if (!out)
          return false;
      }

      std::error_code ec;
      fs::rename(tmp, path, ec);

      if (!ec)
        return true;

      fs::remove(path, ec);
      ec.clear();
      fs::rename(tmp, path, ec);

      return !ec;
    }

    static bool copy_file_replace(
        const fs::path &from,
        const fs::path &to)
    {
      if (!file_exists(from))
        return false;

      const fs::path parent = to.parent_path();

      if (!parent.empty() && !ensure_dir(parent))
        return false;

      std::error_code ec;
      fs::copy_file(
          from,
          to,
          fs::copy_options::overwrite_existing,
          ec);

      return !ec;
    }

    static std::uint64_t hash_file_content(const fs::path &path)
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
  } // namespace

  bool ObjectCacheEntry::valid() const
  {
    return !key.empty() && !object.empty();
  }

  ObjectCache::ObjectCache(fs::path buildDir)
  {
    (void)buildDir;

    /*
     * The object cache must survive build directory deletion.
     *
     * Old layout:
     *   <build-dir>/.vix/objects
     *
     * New layout:
     *   ~/.vix/cache/objects
     *
     * This makes warm object cache builds possible even after:
     *   rm -rf build-ninja
     */
    root_ = global_object_cache_root();
  }

  const fs::path &ObjectCache::root() const
  {
    return root_;
  }

  fs::path ObjectCache::index_path() const
  {
    return root_ / "index.vix";
  }

  fs::path ObjectCache::object_path_for_key(const std::string &key) const
  {
    return root_ / key / "object.o";
  }

  fs::path ObjectCache::dependency_path_for_key(const std::string &key) const
  {
    return root_ / key / "object.d";
  }

  bool ObjectCache::ensure_layout() const
  {
    return ensure_dir(root_);
  }

  std::string ObjectCache::compute_input_hash(
      const fs::path &source,
      const std::vector<fs::path> &dependencies)
  {
    std::uint64_t h = FNV_OFFSET;

    const auto mix_file = [&](const fs::path &path)
    {
      const std::string normalized = normalize_path_string(path);
      h = fnv_mix_string(h, normalized);

      if (!file_exists(path))
      {
        h = fnv_mix_string(h, "<missing>");
        return;
      }

      h = fnv_mix_string(h, "<present>");

      const std::uint64_t contentHash = hash_file_content(path);
      h = fnv_mix(h, &contentHash, sizeof(contentHash));

      h = fnv_mix_string(h, std::to_string(file_size_or_zero(path)));
    };

    mix_file(source);

    std::vector<fs::path> sortedDeps = dependencies;
    std::sort(
        sortedDeps.begin(),
        sortedDeps.end(),
        [](const fs::path &a, const fs::path &b)
        {
          return normalize_path_string(a) < normalize_path_string(b);
        });

    sortedDeps.erase(
        std::unique(
            sortedDeps.begin(),
            sortedDeps.end(),
            [](const fs::path &a, const fs::path &b)
            {
              return normalize_path_string(a) == normalize_path_string(b);
            }),
        sortedDeps.end());

    for (const auto &dep : sortedDeps)
      mix_file(dep);

    return hex64(h);
  }

  std::string ObjectCache::compute_object_key(
      const fs::path &source,
      const std::string &inputHash,
      const std::string &commandHash,
      const std::string &buildFingerprint)
  {
    std::uint64_t h = FNV_OFFSET;

    h = fnv_mix_string(h, normalize_path_string(source));
    h = fnv_mix_string(h, inputHash);
    h = fnv_mix_string(h, commandHash);
    h = fnv_mix_string(h, buildFingerprint);

    return hex64(h);
  }

  bool ObjectCache::exists(const std::string &key) const
  {
    if (key.empty())
      return false;

    return file_exists(object_path_for_key(key));
  }

  std::optional<ObjectCacheEntry> ObjectCache::read_entry(
      const std::string &key) const
  {
    if (key.empty())
      return std::nullopt;

    const fs::path manifest = root_ / key / "manifest.json";

    if (!file_exists(manifest))
      return std::nullopt;

    const std::string content = read_text_file_or_empty(manifest);

    if (content.empty())
      return std::nullopt;

    ObjectCacheEntry entry;
    entry.key = extract_json_string(content, "key");
    entry.source = extract_json_string(content, "source");
    entry.object = extract_json_string(content, "object");
    entry.dependencyFile = extract_json_string(content, "dependency_file");
    entry.commandHash = extract_json_string(content, "command_hash");
    entry.inputHash = extract_json_string(content, "input_hash");
    entry.finalHash = extract_json_string(content, "final_hash");
    entry.objectSize = extract_json_u64(content, "object_size");
    entry.updatedUnixMs = extract_json_u64(content, "updated_unix_ms");

    if (!entry.valid())
      return std::nullopt;

    if (!file_exists(entry.object))
      return std::nullopt;

    return entry;
  }

  bool ObjectCache::store(
      const std::string &key,
      const fs::path &source,
      const fs::path &objectPath,
      const fs::path &dependencyFilePath,
      const std::string &inputHash,
      const std::string &commandHash) const
  {
    if (key.empty())
      return false;

    if (!file_exists(objectPath))
      return false;

    if (!ensure_layout())
      return false;

    const fs::path entryDir = root_ / key;

    if (!ensure_dir(entryDir))
      return false;

    const fs::path cachedObject = object_path_for_key(key);
    const fs::path cachedDep = dependency_path_for_key(key);

    if (!copy_file_replace(objectPath, cachedObject))
      return false;

    if (file_exists(dependencyFilePath))
      (void)copy_file_replace(dependencyFilePath, cachedDep);

    ObjectCacheEntry entry;
    entry.key = key;
    entry.source = source.lexically_normal();
    entry.object = cachedObject.lexically_normal();
    entry.dependencyFile = cachedDep.lexically_normal();
    entry.commandHash = commandHash;
    entry.inputHash = inputHash;
    entry.finalHash = key;
    entry.objectSize = file_size_or_zero(cachedObject);
    entry.updatedUnixMs = now_unix_ms();

    std::ostringstream manifest;
    manifest << "{\n";
    manifest << "  \"key\": \"" << json_escape(entry.key) << "\",\n";
    manifest << "  \"source\": \"" << json_escape(entry.source.string()) << "\",\n";
    manifest << "  \"object\": \"" << json_escape(entry.object.string()) << "\",\n";
    manifest << "  \"dependency_file\": \"" << json_escape(entry.dependencyFile.string()) << "\",\n";
    manifest << "  \"command_hash\": \"" << json_escape(entry.commandHash) << "\",\n";
    manifest << "  \"input_hash\": \"" << json_escape(entry.inputHash) << "\",\n";
    manifest << "  \"final_hash\": \"" << json_escape(entry.finalHash) << "\",\n";
    manifest << "  \"object_size\": \"" << entry.objectSize << "\",\n";
    manifest << "  \"updated_unix_ms\": \"" << entry.updatedUnixMs << "\"\n";
    manifest << "}\n";

    if (!write_text_file_atomic(entryDir / "manifest.json", manifest.str()))
      return false;

    std::ofstream index(index_path(), std::ios::app);
    if (index)
    {
      index << key << "\t"
            << entry.source.generic_string() << "\t"
            << entry.object.generic_string() << "\t"
            << entry.dependencyFile.generic_string() << "\t"
            << entry.inputHash << "\t"
            << entry.commandHash << "\t"
            << entry.updatedUnixMs << "\n";
    }

    return true;
  }

  ObjectCacheResult ObjectCache::restore(
      const std::string &key,
      const fs::path &destinationObject,
      const fs::path &destinationDependencyFile) const
  {
    ObjectCacheResult result;

    const auto entry = read_entry(key);
    if (!entry)
      return result;

    if (!copy_file_replace(entry->object, destinationObject))
      return result;

    if (!entry->dependencyFile.empty() && file_exists(entry->dependencyFile))
      (void)copy_file_replace(entry->dependencyFile, destinationDependencyFile);

    result.hit = true;
    result.entry = *entry;
    result.materializedObject = destinationObject.lexically_normal();
    result.materializedDependencyFile = destinationDependencyFile.lexically_normal();

    return result;
  }

  ObjectCacheResult ObjectCache::resolve_compile_task(
      const BuildTask &task,
      const fs::path &source,
      const std::vector<fs::path> &dependencies,
      const fs::path &objectPath,
      const fs::path &dependencyFilePath,
      const std::string &buildFingerprint) const
  {
    ObjectCacheResult result;

    if (task.kind != BuildTaskKind::Compile)
      return result;

    if (task.commandHash.empty())
      return result;

    const std::string inputHash =
        compute_input_hash(source, dependencies);

    const std::string key =
        compute_object_key(
            source,
            inputHash,
            task.commandHash,
            buildFingerprint);

    return restore(key, objectPath, dependencyFilePath);
  }

} // namespace vix::cli::build
