#include <vix/cli/project/native/MetadataStorage.hpp>

#include <toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <utility>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    using Table = toml::table;
    using Node = toml::node;
    using Array = toml::array;

    struct StorageFailure
    {
      std::string message;
      bool corrupt{false};
    };
    struct TrustedStatesReadResult
    {
      std::optional<std::vector<TrustedRegistrySnapshotState>> states;
      std::vector<TrustedStateStoreIssue> issues;
      bool valid() const noexcept { return issues.empty(); }
    };

    std::string version_text(const Version &value)
    {
      auto result = std::to_string(value.major) + "." + std::to_string(value.minor) + "." + std::to_string(value.patch);
      if (value.prerelease)
        result += "-" + *value.prerelease;
      return result;
    }

    bool valid_storage_digest(std::string_view digest)
    {
      const auto colon = digest.find(':');
      if (colon == std::string_view::npos)
        return false;
      const auto algorithm = digest.substr(0, colon);
      const auto hex = digest.substr(colon + 1);
      const bool lengthMatches = (algorithm == "sha256" && hex.size() == 64) ||
                                 (algorithm == "sha512" && hex.size() == 128);
      return lengthMatches && std::all_of(hex.begin(), hex.end(), [](char c)
                                          { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    bool valid_storage_repository(std::string_view repository)
    {
      if (repository.empty() || std::any_of(repository.begin(), repository.end(), [](unsigned char c)
                                            { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }))
        return false;
      const auto scheme = repository.find("://");
      if (scheme != std::string_view::npos)
      {
        const auto start = scheme + 3;
        const auto end = repository.find_first_of("/?#", start);
        const auto authority = repository.substr(start, end == std::string_view::npos ? repository.size() - start : end - start);
        if (authority.empty() || authority.find('@') != std::string_view::npos)
          return false;
      }
      return true;
    }

    std::optional<Version> parse_version(std::string_view text)
    {
      const auto dash = text.find('-');
      const auto numeric = text.substr(0, dash);
      std::array<std::uint64_t, 3> parts{};
      std::size_t begin = 0;
      for (std::size_t i = 0; i != parts.size(); ++i)
      {
        const auto end = i == 2 ? numeric.size() : numeric.find('.', begin);
        if (end == std::string_view::npos || end == begin || (end - begin > 1 && numeric[begin] == '0'))
          return std::nullopt;
        const auto parsed = std::from_chars(numeric.data() + begin, numeric.data() + end, parts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != numeric.data() + end)
          return std::nullopt;
        begin = end + 1;
      }
      if (begin != numeric.size() + 1)
        return std::nullopt;
      std::optional<std::string> prerelease;
      if (dash != std::string_view::npos)
      {
        if (dash + 1 == text.size())
          return std::nullopt;
        prerelease = std::string{text.substr(dash + 1)};
      }
      Version result{parts[0], parts[1], parts[2], std::move(prerelease)};
      if (!valid_semver(result))
        return std::nullopt;
      return result;
    }

    std::string revision_kind(const GitRevisionRequirement &revision)
    {
      return std::visit([](const auto &item) -> std::string
                        {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, CommitRevision>) return "commit";
        if constexpr (std::is_same_v<T, TagRevision>) return "tag";
        return "branch"; }, revision);
    }

    std::string revision_value(const GitRevisionRequirement &revision)
    {
      return std::visit([](const auto &item)
                        { return item.value; }, revision);
    }

    toml::table requirement_table(const PackageRequirement &requirement)
    {
      toml::table result;
      result.insert("alias", requirement.alias);
      std::visit([&](const auto &source)
                 {
        using T = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<T, RegistryRequirement>)
        {
          result.insert("source", "registry");
          result.insert("registry", source.identity.registry);
          result.insert("publisher", source.identity.publisher);
          result.insert("name", source.identity.name);
          toml::table requirement_value;
          if (const auto *exact = std::get_if<ExactVersion>(&source.version))
            requirement_value.insert("exact", version_text(exact->version));
          else requirement_value.insert("compatible", version_text(std::get<CompatibleVersion>(source.version).version));
          result.insert("requirement", std::move(requirement_value));
        }
        else
        {
          result.insert("source", "git");
          result.insert("repository", source.normalizedRepository);
          toml::table revision;
          revision.insert(revision_kind(source.revision), revision_value(source.revision));
          result.insert("revision", std::move(revision));
        } }, requirement.source);
      return result;
    }

    std::string contract_text(const PackageContract &contract)
    {
      toml::table root;
      root.insert("format", contract.format);
      toml::table package;
      if (const auto *registry = std::get_if<RegistryPackageContractIdentity>(&contract.identity))
      {
        package.insert("source", "registry");
        package.insert("registry", registry->identity.registry);
        package.insert("publisher", registry->identity.publisher);
        package.insert("name", registry->identity.name);
        package.insert("version", version_text(registry->version));
      }
      else
        package.insert("source", "git");
      root.insert("package", std::move(package));
      toml::array packages;
      for (const auto &requirement : contract.packages)
        packages.push_back(requirement_table(requirement));
      root.insert("packages", std::move(packages));
      toml::table integration;
      if (const auto *native = std::get_if<NativeVixIntegration>(&contract.integration))
      {
        integration.insert("type", "native-vix");
        toml::array roots;
        for (const auto &path : native->includeRoots)
          roots.push_back(path);
        integration.insert("include-roots", std::move(roots));
      }
      else
      {
        integration.insert("type", "cmake-config");
        integration.insert("config-name", std::get<CMakeConfigIntegration>(contract.integration).configName);
      }
      root.insert("integration", std::move(integration));
      toml::array exports;
      for (const auto &entry : contract.exports)
      {
        toml::table item;
        item.insert("name", entry.name);
        if (const auto *native = std::get_if<NativeExportMapping>(&entry.mapping))
          item.insert("project-target", native->projectTarget);
        else
          item.insert("cmake-target", std::get<CMakeExportMapping>(entry.mapping).cmakeTarget);
        exports.push_back(std::move(item));
      }
      root.insert("exports", std::move(exports));
      std::ostringstream output;
      output << root;
      return output.str();
    }

    PackageContract parse_contract(const std::string &text)
    {
      auto parsed = parse_package_contract_text(text);
      if (!parsed.valid() || !parsed.value)
        throw StorageFailure{"cached package contract is malformed or semantically invalid", true};
      if (!validate_package_contract(*parsed.value).valid())
        throw StorageFailure{"cached package contract fails semantic validation", true};
      return std::move(*parsed.value);
    }

    toml::table versioned(std::string_view kind)
    {
      toml::table root;
      root.insert("format", std::int64_t{1});
      root.insert("kind", std::string{kind});
      return root;
    }

    std::string registry_key(const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query)
    {
      return "VIX-REGISTRY-METADATA-CACHE-KEY-v1\n" +
             std::to_string(snapshot.registryId.size()) + ":" + snapshot.registryId + "\n" +
             std::to_string(snapshot.sequence) + "\n" + std::to_string(snapshot.digest.size()) + ":" + snapshot.digest + "\n" +
             std::to_string(query.publisher.size()) + ":" + query.publisher + "\n" +
             std::to_string(query.name.size()) + ":" + query.name + "\n";
    }

    std::string git_key(const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
    {
      return "VIX-GIT-METADATA-CACHE-KEY-v1\n" +
             std::to_string(identity.normalizedRepository.size()) + ":" + identity.normalizedRepository + "\n" +
             (identity.repositoryRoot ? "1\n" : "0\n") + revision_kind(revision) + "\n" +
             std::to_string(revision_value(revision).size()) + ":" + revision_value(revision) + "\n";
    }

    fs::path registry_path(const fs::path &root, const RegistrySnapshotIdentity &snapshot,
                           const RegistryPackageIdentity &query)
    {
      return root / "metadata" / "registry" / (sha256_hex(registry_key(snapshot, query)) + ".toml");
    }

    fs::path git_path(const fs::path &root, const GitPackageIdentity &identity,
                      const GitRevisionRequirement &revision)
    {
      return root / "metadata" / "git" / (sha256_hex(git_key(identity, revision)) + ".toml");
    }

    void strict_keys(const Table &table, std::initializer_list<std::string_view> allowed)
    {
      for (const auto &entry : table)
      {
        bool found = false;
        for (const auto key : allowed)
          if (entry.first.str() == key)
          {
            found = true;
            break;
          }
        if (!found)
          throw StorageFailure{"unknown field in private storage record", true};
      }
    }

    const Table &as_table(const Node *node)
    {
      if (!node || !node->is_table())
        throw StorageFailure{"expected TOML table", true};
      return *node->as_table();
    }

    const Array &as_array(const Node *node)
    {
      if (!node || !node->is_array())
        throw StorageFailure{"expected TOML array", true};
      return *node->as_array();
    }

    std::string string_field(const Table &table, std::string_view key)
    {
      const auto *node = table.get(key);
      if (!node || !node->is_string())
        throw StorageFailure{"missing or invalid string field", true};
      return *node->value<std::string>();
    }

    std::int64_t integer_field(const Table &table, std::string_view key)
    {
      const auto *node = table.get(key);
      if (!node || !node->is_integer())
        throw StorageFailure{"missing or invalid integer field", true};
      return *node->value<std::int64_t>();
    }

    bool bool_field(const Table &table, std::string_view key)
    {
      const auto *node = table.get(key);
      if (!node || !node->is_boolean())
        throw StorageFailure{"missing or invalid boolean field", true};
      return *node->value<bool>();
    }

    Table parse_table(const std::string &bytes)
    {
      try
      {
        return toml::parse(std::string_view{bytes});
      }
      catch (const StorageFailure &)
      {
        throw;
      }
      catch (const std::exception &)
      {
        throw StorageFailure{"malformed private TOML record", true};
      }
    }

    std::string serialize_table(const Table &table)
    {
      std::ostringstream output;
      output << table;
      return output.str();
    }

    void validate_header(const Table &table, std::string_view kind, std::initializer_list<std::string_view> allowed)
    {
      strict_keys(table, allowed);
      if (integer_field(table, "format") != 1 || string_field(table, "kind") != kind)
        throw StorageFailure{"unsupported private cache record format or kind", true};
    }

    std::string storage_issue_path(const fs::path &path, const std::string &reason)
    {
      return path.string() + ": " + reason;
    }

    bool check_no_symlink(const fs::path &path, bool allowMissing, std::string &error)
    {
      std::error_code ec;
      const auto status = fs::symlink_status(path, ec);
      if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
      {
        if (allowMissing)
          return true;
        error = "required storage object is missing";
        return false;
      }
      if (ec)
      {
        error = ec.message();
        return false;
      }
      if (fs::is_symlink(status))
      {
        error = "unexpected symlink in metadata storage";
        return false;
      }
      return true;
    }

    bool ensure_directory(const fs::path &path, std::string &error)
    {
      if (!check_no_symlink(path, true, error))
        return false;
      std::error_code ec;
      if (!fs::exists(path, ec))
      {
        fs::create_directories(path, ec);
        if (ec)
        {
          error = ec.message();
          return false;
        }
#ifndef _WIN32
        (void)::chmod(path.c_str(), 0700);
#endif
      }
      if (ec || !fs::is_directory(path, ec) || ec)
      {
        error = ec ? ec.message() : "storage path component is not a directory";
        return false;
      }
      return check_no_symlink(path, false, error);
    }

    bool prepare_root(const fs::path &root, std::string &error)
    {
      return ensure_directory(root, error);
    }

    class LocalFileLock
    {
    public:
      explicit LocalFileLock(const fs::path &path)
      {
        if (!check_no_symlink(path, true, error_))
          return;
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
        {
          error_ = "cannot open storage lock";
          return;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle_, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
          error_ = "unexpected reparse point for storage lock";
          CloseHandle(handle_);
          handle_ = INVALID_HANDLE_VALUE;
          return;
        }
        if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlap_))
        {
          error_ = "cannot acquire storage lock";
          CloseHandle(handle_);
          handle_ = INVALID_HANDLE_VALUE;
        }
#else
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR
#ifdef O_NOFOLLOW
                                       | O_NOFOLLOW
#endif
                     ,
                     0600);
        if (fd_ < 0 || ::flock(fd_, LOCK_EX) != 0)
        {
          error_ = "cannot acquire storage lock";
          if (fd_ >= 0)
            ::close(fd_);
          fd_ = -1;
        }
        else if (::fchmod(fd_, 0600) != 0)
        {
          error_ = "cannot restrict storage lock permissions";
          (void)::flock(fd_, LOCK_UN);
          (void)::close(fd_);
          fd_ = -1;
        }
#endif
      }
      ~LocalFileLock()
      {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
        {
          UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlap_);
          CloseHandle(handle_);
        }
#else
        if (fd_ >= 0)
        {
          (void)::flock(fd_, LOCK_UN);
          (void)::close(fd_);
        }
#endif
      }
      bool valid() const noexcept
      {
#ifdef _WIN32
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
      }
      const std::string &error() const noexcept { return error_; }

    private:
      std::string error_;
#ifdef _WIN32
      HANDLE handle_{INVALID_HANDLE_VALUE};
      OVERLAPPED overlap_{};
#else
      int fd_{-1};
#endif
    };

    bool read_bytes(const fs::path &path, std::optional<std::string> &bytes, std::string &error)
    {
      if (!check_no_symlink(path, true, error))
        return false;
      std::error_code ec;
      if (!fs::exists(path, ec))
      {
        bytes.reset();
        if (ec)
          error = ec.message();
        return !ec;
      }
      if (ec || !fs::is_regular_file(path, ec) || ec)
      {
        error = ec ? ec.message() : "storage target is not a regular file";
        return false;
      }
      std::ifstream input(path, std::ios::binary);
      if (!input)
      {
        error = "cannot open storage record";
        return false;
      }
      bytes = std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
      if (input.bad())
      {
        error = "cannot read complete storage record";
        return false;
      }
      return true;
    }

    bool atomic_write(const fs::path &path, const std::string &bytes, std::string &error)
    {
      if (!check_no_symlink(path, true, error))
        return false;
      const fs::path temporary = path.string() + ".tmp";
      if (!check_no_symlink(temporary, true, error))
        return false;
      std::error_code ec;
      if (fs::exists(temporary, ec))
        fs::remove(temporary, ec);
      if (ec)
      {
        error = ec.message();
        return false;
      }
#ifdef _WIN32
      HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
      if (file == INVALID_HANDLE_VALUE)
      {
        error = "cannot create storage temporary file";
        return false;
      }
      DWORD written = 0;
      const bool wrote = bytes.size() <= MAXDWORD && WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
      const bool flushed = wrote && FlushFileBuffers(file);
      CloseHandle(file);
      if (!flushed || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      {
        error = "cannot durably publish storage record";
        DeleteFileW(temporary.c_str());
        return false;
      }
#else
      const int fd = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY
#ifdef O_NOFOLLOW
                                                   | O_NOFOLLOW
#endif
                            ,
                            0600);
      if (fd < 0)
      {
        error = "cannot create storage temporary file";
        return false;
      }
      std::size_t offset = 0;
      while (offset < bytes.size())
      {
        const auto count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
          continue;
        if (count <= 0)
        {
          error = "cannot write complete storage temporary file";
          (void)::close(fd);
          (void)::unlink(temporary.c_str());
          return false;
        }
        offset += static_cast<std::size_t>(count);
      }
      const bool fileFlushed = ::fsync(fd) == 0;
      const bool fileClosed = ::close(fd) == 0;
      if (!fileFlushed || !fileClosed)
      {
        error = "cannot flush storage temporary file";
        (void)::unlink(temporary.c_str());
        return false;
      }
      if (::rename(temporary.c_str(), path.c_str()) != 0)
      {
        error = "cannot atomically replace storage record";
        (void)::unlink(temporary.c_str());
        return false;
      }
      const int directory = ::open(path.parent_path().c_str(), O_RDONLY
#ifdef O_DIRECTORY
                                                                   | O_DIRECTORY
#endif
      );
      if (directory >= 0)
      {
        (void)::fsync(directory);
        (void)::close(directory);
      }
#endif
      return true;
    }

    TrustedStatesReadResult read_trusted_file(const fs::path &path)
    {
      std::optional<std::string> bytes;
      std::string error;
      if (!read_bytes(path, bytes, error))
        return {{}, {{TrustedStateStoreErrorCode::trusted_state_storage_corrupt, {}, storage_issue_path(path, error)}}};
      if (!bytes)
        return {};
      try
      {
        const auto table = parse_table(*bytes);
        strict_keys(table, {"format", "registries"});
        if (integer_field(table, "format") != 1)
          throw StorageFailure{"unsupported trusted state format", true};
        const auto &entries = as_array(table.get("registries"));
        std::vector<TrustedRegistrySnapshotState> states;
        for (const auto &node : entries)
        {
          if (!node.is_table())
            throw StorageFailure{"trusted Registry entry must be a table", true};
          const auto &item = *node.as_table();
          strict_keys(item, {"id", "sequence", "digest"});
          const auto id = string_field(item, "id");
          const auto sequence = integer_field(item, "sequence");
          const auto digest = string_field(item, "digest");
          if (!valid_registry_id(id) || sequence <= 0 || !valid_storage_digest(digest))
            throw StorageFailure{"invalid trusted Registry state entry", true};
          if (std::any_of(states.begin(), states.end(), [&](const auto &state)
                          { return state.registryId == id; }))
            throw StorageFailure{"duplicate trusted Registry ID", true};
          states.push_back({id, static_cast<std::uint64_t>(sequence), digest});
        }
        std::sort(states.begin(), states.end(), [](const auto &a, const auto &b)
                  { return a.registryId < b.registryId; });
        return {std::move(states), {}};
      }
      catch (const StorageFailure &failure)
      {
        return {{}, {{TrustedStateStoreErrorCode::trusted_state_storage_corrupt, {}, storage_issue_path(path, failure.message)}}};
      }
    }

    std::string serialize_trusted(const std::vector<TrustedRegistrySnapshotState> &states)
    {
      auto sorted = states;
      std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b)
                { return a.registryId < b.registryId; });
      toml::table root;
      root.insert("format", std::int64_t{1});
      toml::array entries;
      for (const auto &state : sorted)
      {
        toml::table item;
        item.insert("id", state.registryId);
        item.insert("sequence", static_cast<std::int64_t>(state.sequence));
        item.insert("digest", state.digest);
        entries.push_back(std::move(item));
      }
      root.insert("registries", std::move(entries));
      return serialize_table(root);
    }

    toml::table registry_record(const UnverifiedRegistryObservation &observation)
    {
      auto root = versioned("registry");
      root.insert("key", registry_key(observation.snapshot, observation.query));
      toml::table snapshot;
      snapshot.insert("registry", observation.snapshot.registryId);
      snapshot.insert("sequence", static_cast<std::int64_t>(observation.snapshot.sequence));
      snapshot.insert("digest", observation.snapshot.digest);
      root.insert("snapshot", std::move(snapshot));
      toml::table query;
      query.insert("registry", observation.query.registry);
      query.insert("publisher", observation.query.publisher);
      query.insert("name", observation.query.name);
      root.insert("query", std::move(query));
      root.insert("evidence", observation.authorityEvidence);
      toml::array releases;
      for (const auto &release : observation.releases)
      {
        toml::table item;
        item.insert("registry", release.identity.registry);
        item.insert("publisher", release.identity.publisher);
        item.insert("name", release.identity.name);
        item.insert("version", version_text(release.version));
        item.insert("content-digest", release.contentDigest);
        item.insert("contract-toml", contract_text(release.contract));
        releases.push_back(std::move(item));
      }
      root.insert("releases", std::move(releases));
      return root;
    }

    UnverifiedRegistryObservation parse_registry_record(const std::string &bytes,
                                                        const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query)
    {
      const auto root = parse_table(bytes);
      validate_header(root, "registry", {"format", "kind", "key", "snapshot", "query", "evidence", "releases"});
      if (string_field(root, "key") != registry_key(snapshot, query))
        throw StorageFailure{"Registry cache logical key mismatch", true};
      const auto &snapshotTable = as_table(root.get("snapshot"));
      strict_keys(snapshotTable, {"registry", "sequence", "digest"});
      RegistrySnapshotIdentity stored{string_field(snapshotTable, "registry"),
                                      static_cast<std::uint64_t>(integer_field(snapshotTable, "sequence")), string_field(snapshotTable, "digest")};
      const auto &queryTable = as_table(root.get("query"));
      strict_keys(queryTable, {"registry", "publisher", "name"});
      RegistryPackageIdentity storedQuery{string_field(queryTable, "registry"), string_field(queryTable, "publisher"), string_field(queryTable, "name")};
      if (stored != snapshot || storedQuery != query)
        throw StorageFailure{"Registry cache embedded key differs from request", true};
      std::vector<RegistryCandidate> releases;
      for (const auto &node : as_array(root.get("releases")))
      {
        if (!node.is_table())
          throw StorageFailure{"Registry release must be a table", true};
        const auto &item = *node.as_table();
        strict_keys(item, {"registry", "publisher", "name", "version", "content-digest", "contract-toml"});
        RegistryPackageIdentity identity{string_field(item, "registry"), string_field(item, "publisher"), string_field(item, "name")};
        const auto version = parse_version(string_field(item, "version"));
        auto digest = string_field(item, "content-digest");
        auto contract = parse_contract(string_field(item, "contract-toml"));
        if (!version || !valid_registry_id(identity.registry) || !valid_registry_name(identity.publisher) ||
            !valid_registry_name(identity.name) || !valid_storage_digest(digest))
          throw StorageFailure{"invalid cached Registry release metadata", true};
        releases.push_back({std::move(identity), *version, std::move(digest), std::move(contract)});
      }
      return {std::move(stored), std::move(storedQuery), std::move(releases), string_field(root, "evidence")};
    }

    toml::table git_record(const GitPackageIdentity &identity, const GitRevisionRequirement &revision,
                           const GitResolution &resolution)
    {
      auto root = versioned("git");
      root.insert("key", git_key(identity, revision));
      toml::table key;
      key.insert("repository", identity.normalizedRepository);
      key.insert("repository-root", identity.repositoryRoot);
      key.insert("revision-kind", revision_kind(revision));
      key.insert("revision", revision_value(revision));
      root.insert("identity", std::move(key));
      toml::table value;
      value.insert("repository", resolution.normalizedRepository);
      value.insert("revision-kind", revision_kind(resolution.requestedRevision));
      value.insert("revision", revision_value(resolution.requestedRevision));
      value.insert("commit", resolution.commit);
      value.insert("content-digest", resolution.contentDigest);
      value.insert("contract-toml", contract_text(resolution.contract));
      root.insert("resolution", std::move(value));
      return root;
    }

    GitRevisionRequirement parse_revision(const Table &table)
    {
      const auto kind = string_field(table, "revision-kind");
      const auto value = string_field(table, "revision");
      if (kind == "commit")
        return CommitRevision{value};
      if (kind == "tag")
        return TagRevision{value};
      if (kind == "branch")
        return BranchRevision{value};
      throw StorageFailure{"unknown Git revision kind", true};
    }

    GitResolution parse_git_record(const std::string &bytes, const GitPackageIdentity &identity,
                                   const GitRevisionRequirement &revision)
    {
      const auto root = parse_table(bytes);
      validate_header(root, "git", {"format", "kind", "key", "identity", "resolution"});
      if (string_field(root, "key") != git_key(identity, revision))
        throw StorageFailure{"Git cache logical key mismatch", true};
      const auto &key = as_table(root.get("identity"));
      strict_keys(key, {"repository", "repository-root", "revision-kind", "revision"});
      GitPackageIdentity storedIdentity{string_field(key, "repository"), bool_field(key, "repository-root")};
      const auto storedRevision = parse_revision(key);
      if (storedIdentity != identity || revision_kind(storedRevision) != revision_kind(revision) || revision_value(storedRevision) != revision_value(revision))
        throw StorageFailure{"Git cache embedded key differs from request", true};
      const auto &value = as_table(root.get("resolution"));
      strict_keys(value, {"repository", "revision-kind", "revision", "commit", "content-digest", "contract-toml"});
      auto resolvedRevision = parse_revision(value);
      auto repository = string_field(value, "repository");
      auto commit = string_field(value, "commit");
      auto digest = string_field(value, "content-digest");
      if (repository != identity.normalizedRepository || revision_kind(resolvedRevision) != revision_kind(revision) ||
          revision_value(resolvedRevision) != revision_value(revision) || !valid_storage_repository(repository) ||
          (commit.size() != 40 && commit.size() != 64) ||
          !std::all_of(commit.begin(), commit.end(), [](char c)
                       { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
          !valid_storage_digest(digest))
        throw StorageFailure{"Git cache resolution disagrees with its immutable key or has invalid digest/commit", true};
      auto contract = parse_contract(string_field(value, "contract-toml"));
      if (!std::holds_alternative<GitPackageContractIdentity>(contract.identity))
        throw StorageFailure{"Git cache record contains a non-Git package contract", true};
      return {std::move(repository), std::move(resolvedRevision), std::move(commit), std::move(digest), std::move(contract)};
    }

    template <typename T>
    MetadataStorageResult<T> cache_failure(MetadataCacheErrorCode code, std::string subject, std::string message)
    {
      return {std::nullopt, {{code, std::move(subject), std::move(message)}}};
    }
  }

  FilesystemMetadataStorage::FilesystemMetadataStorage(fs::path root) : root_(std::move(root)) {}

  TrustedStateLoadResult FilesystemMetadataStorage::load(std::string_view registryId) const
  {
    const auto directory = root_ / "trusted";
    std::string error;
    if (!prepare_root(root_, error) || !ensure_directory(directory, error))
      return {{}, {{TrustedStateStoreErrorCode::trusted_state_storage_failure, std::string{registryId}, error}}};
    LocalFileLock lock(directory / "state.lock");
    if (!lock.valid())
      return {{}, {{TrustedStateStoreErrorCode::trusted_state_storage_failure, std::string{registryId}, lock.error()}}};
    auto all = read_trusted_file(directory / "state.toml");
    if (!all.valid())
      return {{}, all.issues};
    const auto states = all.states.value_or(std::vector<TrustedRegistrySnapshotState>{});
    const auto found = std::find_if(states.begin(), states.end(), [&](const auto &state)
                                    { return state.registryId == registryId; });
    return {found == states.end() ? std::nullopt : std::optional<TrustedRegistrySnapshotState>{*found}, {}};
  }

  TrustedStateCommitResult FilesystemMetadataStorage::compare_and_advance(
      const std::optional<TrustedRegistrySnapshotState> &expected, const TrustedRegistrySnapshotState &next)
  {
    if (!valid_registry_id(next.registryId) || next.sequence == 0 ||
        next.sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || !valid_storage_digest(next.digest) ||
        (expected && (expected->registryId != next.registryId || expected->sequence == 0 ||
                      expected->sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || !valid_storage_digest(expected->digest))))
      return {false, {{TrustedStateStoreErrorCode::invalid_trusted_snapshot_state, next.registryId, "expected or next trusted snapshot state is structurally invalid"}}};
    const auto directory = root_ / "trusted";
    std::string error;
    if (!prepare_root(root_, error) || !ensure_directory(directory, error))
      return {false, {{TrustedStateStoreErrorCode::trusted_state_storage_failure, next.registryId, error}}};
    LocalFileLock lock(directory / "state.lock");
    if (!lock.valid())
      return {false, {{TrustedStateStoreErrorCode::trusted_state_storage_failure, next.registryId, lock.error()}}};
    auto loaded = read_trusted_file(directory / "state.toml");
    if (!loaded.valid())
      return {false, loaded.issues};
    auto states = loaded.states.value_or(std::vector<TrustedRegistrySnapshotState>{});
    auto found = std::find_if(states.begin(), states.end(), [&](const auto &state)
                              { return state.registryId == next.registryId; });
    const std::optional<TrustedRegistrySnapshotState> current = found == states.end() ? std::nullopt : std::optional<TrustedRegistrySnapshotState>{*found};
    if (current != expected)
      return {false, {{TrustedStateStoreErrorCode::trusted_state_concurrent_update, next.registryId, "persisted trusted state differs from the caller's expected baseline"}}};
    if (!valid_registry_id(next.registryId) || next.sequence == 0 || next.sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        !valid_storage_digest(next.digest))
      return {false, {{TrustedStateStoreErrorCode::invalid_trusted_snapshot_state, next.registryId, "next trusted snapshot state is structurally invalid"}}};
    if (found == states.end())
      states.push_back(next);
    else
      *found = next;
    if (!atomic_write(directory / "state.toml", serialize_trusted(states), error))
      return {false, {{TrustedStateStoreErrorCode::trusted_state_storage_failure, next.registryId, error}}};
    return {true, {}};
  }

  MetadataStorageResult<UnverifiedRegistryObservation> FilesystemMetadataStorage::read_registry(
      const RegistrySnapshotIdentity &snapshot, const RegistryPackageIdentity &query) const
  {
    if (!valid_registry_id(snapshot.registryId) || snapshot.registryId != query.registry || snapshot.sequence == 0 ||
        snapshot.sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        !valid_storage_digest(snapshot.digest) || !valid_registry_name(query.publisher) || !valid_registry_name(query.name))
      return cache_failure<UnverifiedRegistryObservation>(MetadataCacheErrorCode::invalid_registry_cache_key, query.registry, "Registry cache query is structurally invalid");
    const auto directory = root_ / "metadata";
    std::string error;
    if (!prepare_root(root_, error) || !ensure_directory(directory, error) ||
        !ensure_directory(directory / "registry", error) || !ensure_directory(directory / "git", error))
      return cache_failure<UnverifiedRegistryObservation>(MetadataCacheErrorCode::metadata_cache_storage_failure, snapshot.registryId, error);
    LocalFileLock lock(directory / "cache.lock");
    if (!lock.valid())
      return cache_failure<UnverifiedRegistryObservation>(MetadataCacheErrorCode::metadata_cache_storage_failure, snapshot.registryId, lock.error());
    const auto path = registry_path(root_, snapshot, query);
    std::optional<std::string> bytes;
    if (!read_bytes(path, bytes, error))
      return cache_failure<UnverifiedRegistryObservation>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), error);
    if (!bytes)
      return {};
    try
    {
      return {parse_registry_record(*bytes, snapshot, query), {}};
    }
    catch (const StorageFailure &failure)
    {
      return cache_failure<UnverifiedRegistryObservation>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), failure.message);
    }
  }

  MetadataStorageResult<bool> FilesystemMetadataStorage::write_registry(const RegistryObservationCacheAdmission &admission)
  {
    const auto &observation = admission.observation();
    if (!valid_registry_id(observation.snapshot.registryId) || !valid_registry_name(observation.query.publisher) ||
        !valid_registry_name(observation.query.name) || observation.snapshot.sequence == 0 || !valid_storage_digest(observation.snapshot.digest) ||
        observation.snapshot.registryId != observation.query.registry || observation.snapshot != RegistrySnapshotIdentity{admission.proposed_state().registryId, admission.proposed_state().sequence, admission.proposed_state().digest} ||
        observation.snapshot.sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return cache_failure<bool>(MetadataCacheErrorCode::invalid_registry_cache_key, observation.query.registry, "admitted Registry observation key is inconsistent");
    const auto directory = root_ / "metadata";
    std::string error;
    if (!prepare_root(root_, error) || !ensure_directory(directory, error) || !ensure_directory(directory / "registry", error) || !ensure_directory(directory / "git", error))
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, observation.query.registry, error);
    LocalFileLock lock(directory / "cache.lock");
    if (!lock.valid())
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, observation.query.registry, lock.error());
    const auto path = registry_path(root_, observation.snapshot, observation.query);
    const auto encoded = serialize_table(registry_record(observation));
    std::optional<std::string> previous;
    if (!read_bytes(path, previous, error))
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), error);
    if (previous)
    {
      try
      {
        auto old = parse_registry_record(*previous, observation.snapshot, observation.query);
        if (serialize_table(registry_record(old)) == encoded)
          return {true, {}};
        return cache_failure<bool>(MetadataCacheErrorCode::cache_conflict, path.string(), "contradictory Registry metadata already occupies this cache key");
      }
      catch (const StorageFailure &failure)
      {
        return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), failure.message);
      }
    }
    if (!atomic_write(path, encoded, error))
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, path.string(), error);
    return {true, {}};
  }

  MetadataStorageResult<GitResolution> FilesystemMetadataStorage::read_git(
      const GitPackageIdentity &identity, const GitRevisionRequirement &revision) const
  {
    if (!identity.repositoryRoot || !valid_storage_repository(identity.normalizedRepository) ||
        std::visit([](const auto &value)
                   { return value.value.empty(); }, revision))
      return cache_failure<GitResolution>(MetadataCacheErrorCode::invalid_git_cache_key, identity.normalizedRepository, "Git cache query is structurally invalid");
    const auto directory = root_ / "metadata";
    std::string error;
    if (!prepare_root(root_, error) || !ensure_directory(directory, error) || !ensure_directory(directory / "registry", error) || !ensure_directory(directory / "git", error))
      return cache_failure<GitResolution>(MetadataCacheErrorCode::metadata_cache_storage_failure, identity.normalizedRepository, error);
    LocalFileLock lock(directory / "cache.lock");
    if (!lock.valid())
      return cache_failure<GitResolution>(MetadataCacheErrorCode::metadata_cache_storage_failure, identity.normalizedRepository, lock.error());
    const auto path = git_path(root_, identity, revision);
    std::optional<std::string> bytes;
    if (!read_bytes(path, bytes, error))
      return cache_failure<GitResolution>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), error);
    if (!bytes)
      return {};
    try
    {
      return {parse_git_record(*bytes, identity, revision), {}};
    }
    catch (const StorageFailure &failure)
    {
      return cache_failure<GitResolution>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), failure.message);
    }
  }

  MetadataStorageResult<bool> FilesystemMetadataStorage::write_git(const GitPackageIdentity &identity,
                                                                   const GitRevisionRequirement &revision, const GitResolution &resolution)
  {
    if (!identity.repositoryRoot || !valid_storage_repository(identity.normalizedRepository) ||
        resolution.normalizedRepository != identity.normalizedRepository ||
        std::visit([](const auto &value)
                   { return value.value.empty(); }, revision) ||
        revision_kind(resolution.requestedRevision) != revision_kind(revision) || revision_value(resolution.requestedRevision) != revision_value(revision) ||
        (resolution.commit.size() != 40 && resolution.commit.size() != 64) ||
        !std::all_of(resolution.commit.begin(), resolution.commit.end(), [](char c)
                     { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !valid_storage_digest(resolution.contentDigest) || !validate_package_contract(resolution.contract).valid() ||
        !std::holds_alternative<GitPackageContractIdentity>(resolution.contract.identity))
      return cache_failure<bool>(MetadataCacheErrorCode::invalid_git_cache_key, identity.normalizedRepository, "Git cache key and resolution disagree");
    const auto directory = root_ / "metadata";
    std::string error;
    if (!prepare_root(root_, error) || !ensure_directory(directory, error) || !ensure_directory(directory / "registry", error) || !ensure_directory(directory / "git", error))
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, identity.normalizedRepository, error);
    LocalFileLock lock(directory / "cache.lock");
    if (!lock.valid())
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, identity.normalizedRepository, lock.error());
    const auto path = git_path(root_, identity, revision);
    const auto encoded = serialize_table(git_record(identity, revision, resolution));
    std::optional<std::string> previous;
    if (!read_bytes(path, previous, error))
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), error);
    if (previous)
    {
      try
      {
        auto old = parse_git_record(*previous, identity, revision);
        if (serialize_table(git_record(identity, revision, old)) == encoded)
          return {true, {}};
        return cache_failure<bool>(MetadataCacheErrorCode::cache_conflict, path.string(), "contradictory Git metadata already occupies this cache key");
      }
      catch (const StorageFailure &failure)
      {
        return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_corrupt, path.string(), failure.message);
      }
    }
    if (!atomic_write(path, encoded, error))
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, path.string(), error);
    return {true, {}};
  }

  MetadataStorageResult<bool> FilesystemMetadataStorage::clear_cache()
  {
    const auto directory = root_ / "metadata";
    std::string error;
    if (!prepare_root(root_, error) || !ensure_directory(directory, error) || !ensure_directory(directory / "registry", error) || !ensure_directory(directory / "git", error))
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, directory.string(), error);
    LocalFileLock lock(directory / "cache.lock");
    if (!lock.valid())
      return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, directory.string(), lock.error());
    for (const auto &subdirectory : {directory / "registry", directory / "git"})
    {
      std::error_code ec;
      for (const auto &entry : fs::directory_iterator(subdirectory, ec))
      {
        if (ec)
          return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, subdirectory.string(), ec.message());
        if (!check_no_symlink(entry.path(), false, error))
          return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, entry.path().string(), error);
        if (fs::is_regular_file(entry.status()) && (entry.path().extension() == ".toml" || entry.path().extension() == ".tmp"))
        {
          fs::remove(entry.path(), ec);
          if (ec)
            return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, entry.path().string(), ec.message());
        }
      }
      if (ec)
        return cache_failure<bool>(MetadataCacheErrorCode::metadata_cache_storage_failure, subdirectory.string(), ec.message());
    }
    return {true, {}};
  }
} // namespace vix::cli::project::native
