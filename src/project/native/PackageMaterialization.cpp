#include <vix/cli/project/native/PackageMaterialization.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    PackageMaterializationIssue issue(PackageMaterializationErrorCode code, std::string message,
      std::vector<PackageContentIssue> contentIssues = {})
    { return {code, std::move(message), std::move(contentIssues)}; }

    PackageMaterializationResult failure(PackageMaterializationIssue value)
    { return {std::nullopt, std::nullopt, {std::move(value)}}; }

    std::optional<fs::path> path_from_utf8(std::string_view value)
    {
#ifdef _WIN32
      if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
      const int length = static_cast<int>(value.size());
      const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), length, nullptr, 0);
      if (wideLength <= 0) return std::nullopt;
      std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
      if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length,
          wide.data(), wideLength) != wideLength) return std::nullopt;
      return fs::path{std::move(wide)};
#else
      return fs::path{std::string{value}};
#endif
    }

    std::optional<std::string> path_to_utf8(const fs::path &value)
    {
#ifdef _WIN32
      const auto wide = value.generic_wstring();
      if (wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
      const int length = static_cast<int>(wide.size());
      const int byteLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), length, nullptr, 0, nullptr, nullptr);
      if (byteLength <= 0 && length != 0) return std::nullopt;
      std::string bytes(static_cast<std::size_t>(byteLength), '\0');
      if (byteLength != 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
          wide.data(), length, bytes.data(), byteLength, nullptr, nullptr) != byteLength)
        return std::nullopt;
      return bytes;
#else
      return value.generic_string();
#endif
    }

    bool safe_native_relative_path(const fs::path &path, std::string_view canonical)
    {
      if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
      const auto roundTrip = path_to_utf8(path);
      if (!roundTrip || *roundTrip != canonical) return false;
      for (const auto &component : path)
        if (component.empty() || component == "." || component == "..") return false;
      return true;
    }

    bool safe_native_symlink_target(const fs::path &path, std::string_view canonical)
    {
      if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
      const auto roundTrip = path_to_utf8(path);
      return roundTrip && *roundTrip == canonical;
    }

    bool ensure_directory(const fs::path &path, std::string &diagnostic)
    {
      std::error_code ec;
      auto status = fs::symlink_status(path, ec);
      if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
      {
        ec.clear();
        if (!fs::create_directory(path, ec) && ec != std::errc::file_exists)
        {
          diagnostic = ec ? ec.message() : "could not create directory";
          return false;
        }
        ec.clear();
        status = fs::symlink_status(path, ec);
      }
      if (ec || fs::is_symlink(status) || !fs::is_directory(status))
      {
        diagnostic = ec ? ec.message() : "destination path component is a symlink or non-directory";
        return false;
      }
      return true;
    }

    bool prepare_destination_root(const fs::path &root, fs::path &absolute, std::string &diagnostic)
    {
      if (root.empty())
      {
        diagnostic = "materialization root is empty";
        return false;
      }
      std::error_code ec;
      absolute = fs::absolute(root, ec).lexically_normal();
      if (ec)
      {
        diagnostic = ec.message();
        return false;
      }
      fs::create_directories(absolute, ec);
      if (ec)
      {
        diagnostic = ec.message();
        return false;
      }
      const auto status = fs::symlink_status(absolute, ec);
      if (ec || fs::is_symlink(status) || !fs::is_directory(status))
      {
        diagnostic = ec ? ec.message() : "materialization root is a symlink or non-directory";
        return false;
      }
      return true;
    }

    std::string materialization_relative(const PackageContentId &id)
    {
      const auto &digest = id.digest();
      const auto hex = digest.substr(7);
      return "sha256/" + hex.substr(0, 2) + "/" + hex.substr(2);
    }

    bool expected_tree(const CanonicalPackageTree &tree,
      std::map<std::string, const CanonicalPackageEntry *> &leaves,
      std::set<std::string> &directories)
    {
      for (const auto &entry : tree.entries)
      {
        if (!leaves.emplace(entry.path, &entry).second) return false;
        std::size_t slash = entry.path.find('/');
        while (slash != std::string::npos)
        {
          directories.insert(entry.path.substr(0, slash));
          slash = entry.path.find('/', slash + 1);
        }
      }
      return true;
    }

    bool validate_existing_tree(const fs::path &destination,
      const std::map<std::string, const CanonicalPackageEntry *> &leaves,
      const std::set<std::string> &directories, std::string &diagnostic)
    {
      std::set<std::string> foundLeaves;
      std::set<std::string> foundDirectories;
      std::error_code ec;
      fs::recursive_directory_iterator iterator(destination, fs::directory_options::none, ec);
      const fs::recursive_directory_iterator end;
      if (ec)
      {
        diagnostic = ec.message();
        return false;
      }
      for (; iterator != end; iterator.increment(ec))
      {
        if (ec)
        {
          diagnostic = ec.message();
          return false;
        }
        const auto relative = iterator->path().lexically_relative(destination);
        const auto name = path_to_utf8(relative);
        if (!name)
        {
          diagnostic = "existing materialization contains a path that is not representable as UTF-8";
          return false;
        }
        const auto status = iterator->symlink_status(ec);
        if (ec)
        {
          diagnostic = ec.message();
          return false;
        }
        if (fs::is_directory(status) && !fs::is_symlink(status))
        {
          if (!directories.contains(*name))
          {
            diagnostic = "existing materialization contains an unexpected directory";
            return false;
          }
          foundDirectories.insert(*name);
          continue;
        }

        const auto expected = leaves.find(*name);
        if (expected == leaves.end())
        {
          diagnostic = "existing materialization contains an unexpected entry";
          return false;
        }
        const auto &entry = *expected->second;
        if (entry.kind == CanonicalPackageEntryKind::symlink)
        {
          if (!fs::is_symlink(status))
          {
            diagnostic = "existing materialization has a contradictory symlink entry";
            return false;
          }
          const auto target = fs::read_symlink(iterator->path(), ec);
          if (ec)
          {
            diagnostic = ec.message();
            return false;
          }
          const auto targetBytes = path_to_utf8(target);
          if (!targetBytes || *targetBytes != entry.target)
          {
            diagnostic = "existing materialization symlink target differs from canonical content";
            return false;
          }
        }
        else
        {
          if (fs::is_symlink(status) || !fs::is_regular_file(status))
          {
            diagnostic = "existing materialization has a contradictory regular-file entry";
            return false;
          }
          const auto permissions = status.permissions();
          const bool executable = (permissions & (fs::perms::owner_exec | fs::perms::group_exec |
            fs::perms::others_exec)) != fs::perms::none;
          if (executable != entry.executable)
          {
            diagnostic = "existing materialization executable mode differs from canonical content";
            return false;
          }
          std::ifstream input(iterator->path(), std::ios::binary);
          if (!input)
          {
            diagnostic = "cannot read existing materialized file";
            return false;
          }
          std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
          if (input.bad() || bytes.size() != entry.content.size() ||
              !std::equal(bytes.begin(), bytes.end(), entry.content.begin(), [](char left, std::byte right)
              { return static_cast<unsigned char>(left) == std::to_integer<unsigned char>(right); }))
          {
            diagnostic = "existing materialized file bytes differ from canonical content";
            return false;
          }
        }
        foundLeaves.insert(*name);
      }
      if (foundLeaves.size() != leaves.size() || foundDirectories.size() != directories.size())
      {
        diagnostic = "existing materialization is incomplete";
        return false;
      }
      return true;
    }

    bool create_stage_tree(const fs::path &stage, const CanonicalPackageTree &tree,
      const std::set<std::string> &directories, std::string &diagnostic)
    {
      if (!ensure_directory(stage, diagnostic)) return false;
      for (const auto &directory : directories)
      {
        const auto relative = path_from_utf8(directory);
        if (!relative || !safe_native_relative_path(*relative, directory))
        {
          diagnostic = "canonical package directory is not a safe relative path";
          return false;
        }
        fs::path current = stage;
        for (const auto &component : *relative)
        {
          if (component == "." || component == ".." || component.empty())
          {
            diagnostic = "canonical package directory contains an unsafe component";
            return false;
          }
          current /= component;
          if (!ensure_directory(current, diagnostic)) return false;
        }
      }

      for (const auto &entry : tree.entries)
      {
        const auto relative = path_from_utf8(entry.path);
        if (!relative || !safe_native_relative_path(*relative, entry.path))
        {
          diagnostic = "canonical package entry is not a safe relative path";
          return false;
        }
        const auto outputPath = stage / *relative;
        fs::path parent = stage;
        for (const auto &component : relative->parent_path())
        {
          parent /= component;
          if (!ensure_directory(parent, diagnostic)) return false;
        }
        if (entry.kind == CanonicalPackageEntryKind::symlink)
        {
          const auto target = path_from_utf8(entry.target);
          if (!target || !safe_native_symlink_target(*target, entry.target))
          {
            diagnostic = "canonical package symlink target is not a safe relative path";
            return false;
          }
          std::error_code ec;
          fs::create_symlink(*target, outputPath, ec);
          if (ec)
          {
            diagnostic = ec.message();
            return false;
          }
          continue;
        }
        std::ofstream output(outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
          diagnostic = "cannot create materialized package file";
          return false;
        }
        if (!entry.content.empty())
        {
          if (entry.content.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
          {
            diagnostic = "canonical package file exceeds the host stream size limit";
            return false;
          }
          const auto *data = reinterpret_cast<const char *>(entry.content.data());
          output.write(data, static_cast<std::streamsize>(entry.content.size()));
        }
        output.close();
        if (!output)
        {
          diagnostic = "cannot write complete materialized package file";
          return false;
        }
        auto permissions = fs::perms::owner_read | fs::perms::owner_write;
        if (entry.executable) permissions |= fs::perms::owner_exec;
        std::error_code ec;
        fs::permissions(outputPath, permissions, fs::perm_options::replace, ec);
        if (ec)
        {
          diagnostic = ec.message();
          return false;
        }
      }
      return true;
    }

    struct StagingDirectory
    {
      fs::path path;
      bool owned{false};
      bool published{false};
      ~StagingDirectory()
      {
        if (owned && !published)
        {
          std::error_code ec;
          fs::remove_all(path, ec);
        }
      }
    };
  }

  PackageMaterializationResult materialize_package_content(const PackageContentId &id,
    const FilesystemPackageContentStore &store, const fs::path &destinationRoot)
  {
    const auto stored = store.read_canonical_object(id);
    if (!stored.valid())
      return failure(issue(PackageMaterializationErrorCode::content_store_failure,
        "could not read verified P10P package content", stored.issues));

    const auto parsed = parse_canonical_package_content(*stored.canonicalBytes);
    if (!parsed.valid() || !parsed.tree || !parsed.contentId || *parsed.contentId != id)
      return failure(issue(PackageMaterializationErrorCode::invalid_content,
        "P10P payload is not the expected canonical P10O object"));

    std::map<std::string, const CanonicalPackageEntry *> leaves;
    std::set<std::string> directories;
    if (!expected_tree(*parsed.tree, leaves, directories))
      return failure(issue(PackageMaterializationErrorCode::invalid_content,
        "canonical package content contains duplicate paths"));

    fs::path root;
    std::string diagnostic;
    if (!prepare_destination_root(destinationRoot, root, diagnostic))
      return failure(issue(PackageMaterializationErrorCode::invalid_destination, std::move(diagnostic)));
    const auto relative = materialization_relative(id);
    const auto finalPath = root / "packages" / relative;
    if (!ensure_directory(root / "packages", diagnostic) ||
        !ensure_directory(root / "packages" / "sha256", diagnostic) ||
        !ensure_directory(finalPath.parent_path(), diagnostic))
      return failure(issue(PackageMaterializationErrorCode::invalid_destination, std::move(diagnostic)));

    std::error_code ec;
    auto finalStatus = fs::symlink_status(finalPath, ec);
    if (!ec && finalStatus.type() != fs::file_type::not_found)
    {
      if (fs::is_symlink(finalStatus) || !fs::is_directory(finalStatus))
        return failure(issue(PackageMaterializationErrorCode::destination_conflict,
          "existing materialization destination is not a directory"));
      if (!validate_existing_tree(finalPath, leaves, directories, diagnostic))
        return failure(issue(PackageMaterializationErrorCode::destination_conflict, std::move(diagnostic)));
      return {finalPath, PackageMaterializationState::reused, {}};
    }
    if (ec && ec != std::errc::no_such_file_or_directory)
      return failure(issue(PackageMaterializationErrorCode::filesystem_failure, ec.message()));

    static std::atomic<unsigned long long> sequence{0};
    StagingDirectory staging;
    bool created = false;
    for (unsigned attempt = 0; attempt < 32 && !created; ++attempt)
    {
      const auto nonce = sequence.fetch_add(1, std::memory_order_relaxed);
      staging.path = finalPath.parent_path() / (".stage-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(nonce));
      ec.clear();
      created = fs::create_directory(staging.path, ec);
      if (ec && ec != std::errc::file_exists)
        return failure(issue(PackageMaterializationErrorCode::filesystem_failure, ec.message()));
    }
    if (!created)
      return failure(issue(PackageMaterializationErrorCode::filesystem_failure,
        "could not reserve a temporary sibling for package materialization"));
    staging.owned = true;

    if (!create_stage_tree(staging.path, *parsed.tree, directories, diagnostic))
      return failure(issue(PackageMaterializationErrorCode::filesystem_failure, std::move(diagnostic)));

    ec.clear();
    fs::rename(staging.path, finalPath, ec);
    if (ec)
    {
      finalStatus = fs::symlink_status(finalPath, ec);
      if (!ec && fs::is_directory(finalStatus) && !fs::is_symlink(finalStatus) &&
          validate_existing_tree(finalPath, leaves, directories, diagnostic))
        return {finalPath, PackageMaterializationState::reused, {}};
      return failure(issue(PackageMaterializationErrorCode::filesystem_failure,
        "atomic materialization publication failed"));
    }
    staging.published = true;
    return {finalPath, PackageMaterializationState::materialized, {}};
  }
} // namespace vix::cli::project::native
