#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <vix/cli/project/native/CanonicalHash.hpp>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    PackageContentIssue store_issue(PackageContentIssueCode code, const PackageContentId &id,
      std::string message)
    { return {code, id.digest(), {}, std::move(message)}; }

    bool is_sha256_id(const PackageContentId &id) noexcept
    {
      const auto &value = id.digest();
      return value.size() == 71 && value.compare(0, 7, "sha256:") == 0 &&
        std::all_of(value.begin() + 7, value.end(), [](char character)
        { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
    }

    fs::path object_path(const fs::path &root, const PackageContentId &id)
    {
      const auto &hex = id.digest();
      return root / "objects" / "sha256" / hex.substr(7, 2) / hex.substr(9);
    }

    bool prepare_root(const fs::path &path, fs::path &absolute, std::string &error)
    {
      if (path.empty())
      {
        error = "storage root is empty";
        return false;
      }
      std::error_code ec;
      absolute = fs::absolute(path, ec).lexically_normal();
      if (ec)
      {
        error = ec.message();
        return false;
      }
      const auto before = fs::symlink_status(absolute, ec);
      const bool existed = !ec && before.type() != fs::file_type::not_found;
      if (ec == std::errc::no_such_file_or_directory) ec.clear();
      if (ec)
      {
        error = ec.message();
        return false;
      }
      fs::create_directories(absolute, ec);
      if (ec)
      {
        error = ec.message();
        return false;
      }
      const auto status = fs::symlink_status(absolute, ec);
      if (ec || fs::is_symlink(status) || !fs::is_directory(status))
      {
        error = ec ? ec.message() : "storage root is a symlink or non-directory";
        return false;
      }
#ifndef _WIN32
      if (!existed) (void)::chmod(absolute.c_str(), 0700);
#endif
      return true;
    }

    bool prepare_managed_directory(const fs::path &root, const fs::path &path, std::string &error)
    {
      const auto relative = path.lexically_relative(root);
      if (relative.empty() || relative.is_absolute())
      {
        error = "managed package content path escapes the injected root";
        return false;
      }
      fs::path current = root;
      for (const auto &component : relative)
      {
        if (component == "..")
        {
          error = "managed package content path escapes the injected root";
          return false;
        }
        current /= component;
        std::error_code ec;
        auto status = fs::symlink_status(current, ec);
        if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        {
          ec.clear();
          const bool created = fs::create_directory(current, ec);
          if (ec && ec != std::errc::file_exists)
          {
            error = ec.message();
            return false;
          }
          if (!created) ec.clear();
#ifndef _WIN32
          if (created) (void)::chmod(current.c_str(), 0700);
#endif
          status = fs::symlink_status(current, ec);
        }
        if (ec || fs::is_symlink(status) || !fs::is_directory(status))
        {
          error = ec ? ec.message() : "storage directory contains a symlink or non-directory component";
          return false;
        }
      }
      return true;
    }

    class ContentFileLock
    {
    public:
      explicit ContentFileLock(const fs::path &path)
      {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
        {
          error_ = "cannot open package content lock";
          return;
        }
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(handle_, &information) ||
            (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
            !LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &overlap_))
        {
          error_ = "cannot safely acquire package content lock";
          CloseHandle(handle_);
          handle_ = INVALID_HANDLE_VALUE;
        }
#else
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR
#ifdef O_NOFOLLOW
                     | O_NOFOLLOW
#endif
                     , 0600);
        struct stat information{};
        if (fd_ < 0 || ::fstat(fd_, &information) != 0 || !S_ISREG(information.st_mode) ||
            ::flock(fd_, LOCK_EX) != 0 || ::fchmod(fd_, 0600) != 0)
        {
          error_ = "cannot safely acquire package content lock";
          if (fd_ >= 0)
          {
            (void)::flock(fd_, LOCK_UN);
            (void)::close(fd_);
          }
          fd_ = -1;
        }
#endif
      }

      ~ContentFileLock()
      {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
        {
          (void)UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlap_);
          (void)CloseHandle(handle_);
        }
#else
        if (fd_ >= 0)
        {
          (void)::flock(fd_, LOCK_UN);
          (void)::close(fd_);
        }
#endif
      }

      [[nodiscard]] bool valid() const noexcept
      {
#ifdef _WIN32
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
      }

      [[nodiscard]] const std::string &error() const noexcept { return error_; }

    private:
      std::string error_;
#ifdef _WIN32
      HANDLE handle_{INVALID_HANDLE_VALUE};
      OVERLAPPED overlap_{};
#else
      int fd_{-1};
#endif
    };

    enum class ObjectReadState { missing, valid, corrupt, io_error };
    struct ObjectRead
    {
      ObjectReadState state{ObjectReadState::missing};
      std::string bytes;
      std::string message;
    };

    ObjectRead read_object(const fs::path &path, const PackageContentId &id)
    {
      std::error_code ec;
      const auto status = fs::symlink_status(path, ec);
      if (ec == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found)
        return {ObjectReadState::missing, {}, {}};
      if (ec) return {ObjectReadState::io_error, {}, ec.message()};
      if (fs::is_symlink(status) || !fs::is_regular_file(status))
        return {ObjectReadState::corrupt, {}, "content object is not a regular non-symlink file"};

      std::ifstream input(path, std::ios::binary);
      if (!input) return {ObjectReadState::io_error, {}, "cannot open content object"};
      std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
      if (input.bad()) return {ObjectReadState::io_error, {}, "cannot read complete content object"};
      if (sha256_digest(bytes) != id.digest())
        return {ObjectReadState::corrupt, {}, "content object SHA-256 does not match its content ID"};

      const auto parsed = parse_canonical_package_content(bytes);
      if (!parsed.valid()) return {ObjectReadState::corrupt, {}, "content object is not valid canonical P10O encoding"};
      const auto canonical = canonicalize_package_content_tree(*parsed.tree);
      if (!canonical.valid() || canonical.contentId->digest() != id.digest() || *canonical.canonicalBytes != bytes)
        return {ObjectReadState::corrupt, {}, "content object failed canonical round-trip validation"};
      return {ObjectReadState::valid, std::move(bytes), {}};
    }

    enum class PublishState { published, already_exists, failed };

    PublishState publish_immutable(const fs::path &path, std::string_view bytes, std::string &error)
    {
      // The same-directory temporary and no-replace publication make the
      // final digest path visible only as one complete immutable object.
      fs::path temporary = path;
      temporary += ".tmp";
      std::error_code ec;
      const auto tempStatus = fs::symlink_status(temporary, ec);
      if (ec && ec != std::errc::no_such_file_or_directory)
      {
        error = ec.message();
        return PublishState::failed;
      }
      if (!ec && tempStatus.type() != fs::file_type::not_found)
      {
        if (fs::is_symlink(tempStatus) || !fs::is_regular_file(tempStatus))
        {
          error = "unexpected filesystem object at content temporary path";
          return PublishState::failed;
        }
        if (!fs::remove(temporary, ec) || ec)
        {
          error = ec ? ec.message() : "cannot remove abandoned content temporary";
          return PublishState::failed;
        }
      }
#ifdef _WIN32
      HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
      if (file == INVALID_HANDLE_VALUE)
      {
        error = "cannot create package content temporary";
        return PublishState::failed;
      }
      std::size_t offset = 0;
      bool wrote = true;
      while (offset < bytes.size())
      {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) || written != count)
        {
          wrote = false;
          break;
        }
        offset += written;
      }
      const bool flushed = wrote && FlushFileBuffers(file);
      (void)CloseHandle(file);
      if (!flushed)
      {
        error = "cannot flush package content temporary";
        (void)DeleteFileW(temporary.c_str());
        return PublishState::failed;
      }
      if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
      {
        const auto code = GetLastError();
        (void)DeleteFileW(temporary.c_str());
        if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS) return PublishState::already_exists;
        error = "cannot atomically publish package content object";
        return PublishState::failed;
      }
#else
      const int fd = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY
#ifdef O_NOFOLLOW
                            | O_NOFOLLOW
#endif
                            , 0600);
      if (fd < 0)
      {
        error = "cannot create package content temporary";
        return PublishState::failed;
      }
      std::size_t offset = 0;
      while (offset < bytes.size())
      {
        const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0)
        {
          error = "cannot write complete package content temporary";
          (void)::close(fd);
          (void)::unlink(temporary.c_str());
          return PublishState::failed;
        }
        offset += static_cast<std::size_t>(written);
      }
      const int flushResult = ::fsync(fd);
      const int closeResult = ::close(fd);
      if (flushResult != 0 || closeResult != 0)
      {
        error = "cannot flush package content temporary";
        (void)::unlink(temporary.c_str());
        return PublishState::failed;
      }
      if (::link(temporary.c_str(), path.c_str()) != 0)
      {
        const auto savedErrno = errno;
        (void)::unlink(temporary.c_str());
        if (savedErrno == EEXIST) return PublishState::already_exists;
        error = "cannot atomically publish package content object";
        return PublishState::failed;
      }
      (void)::unlink(temporary.c_str());
      // Directory fsync is best-effort where supported by the host filesystem.
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
      return PublishState::published;
    }

    PackageContentStoreResult inspect_path(const fs::path &path, const PackageContentId &id)
    {
      const auto read = read_object(path, id);
      switch (read.state)
      {
      case ObjectReadState::missing:
        return {PackageContentAvailability::missing, {}};
      case ObjectReadState::valid:
        return {PackageContentAvailability::available, {}};
      case ObjectReadState::corrupt:
        return {PackageContentAvailability::corrupt, {store_issue(PackageContentIssueCode::package_content_store_corrupt,
          id, read.message)}};
      case ObjectReadState::io_error:
        return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, read.message)}};
      }
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id,
        "unknown package content read state")}};
    }
  } // namespace

  PackageContentStoreResult FilesystemPackageContentStore::inspect(const PackageContentId &id) const
  {
    if (!is_sha256_id(id))
      return {std::nullopt, {store_issue(PackageContentIssueCode::invalid_content_id, id,
        "persistent P10O objects require a SHA-256 content ID")}};
    std::string error;
    fs::path root;
    if (!prepare_root(root_, root, error))
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, std::move(error))}};
    ContentFileLock lock{root / "content.lock"};
    if (!lock.valid())
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_lock_error, id, lock.error())}};
    const auto path = object_path(root, id);
    if (!prepare_managed_directory(root, path.parent_path(), error))
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, std::move(error))}};
    return inspect_path(path, id);
  }

  PersistentPackageContentReadResult FilesystemPackageContentStore::read_canonical_object(
    const PackageContentId &id) const
  {
    if (!is_sha256_id(id))
      return {std::nullopt, {store_issue(PackageContentIssueCode::invalid_content_id, id,
        "persistent P10O objects require a SHA-256 content ID")}};
    std::string error;
    fs::path root;
    if (!prepare_root(root_, root, error))
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, std::move(error))}};
    ContentFileLock lock{root / "content.lock"};
    if (!lock.valid())
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_lock_error, id, lock.error())}};
    const auto path = object_path(root, id);
    if (!prepare_managed_directory(root, path.parent_path(), error))
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, std::move(error))}};
    const auto read = read_object(path, id);
    switch (read.state)
    {
    case ObjectReadState::valid:
      return {read.bytes, {}};
    case ObjectReadState::missing:
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_unavailable, id,
        "verified package content is missing from P10P")}};
    case ObjectReadState::corrupt:
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_corrupt, id, read.message)}};
    case ObjectReadState::io_error:
      return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, read.message)}};
    }
    return {std::nullopt, {store_issue(PackageContentIssueCode::package_content_store_io_error, id,
      "unknown package content read state")}};
  }

  PackageContentAdmissionResult FilesystemPackageContentStore::admit(const VerifiedPackageContent &content)
  {
    const auto id = content.id_;
    if (!is_sha256_id(id))
      return {false, {store_issue(PackageContentIssueCode::invalid_content_id, id,
        "persistent P10O objects require a SHA-256 content ID")}};
    const auto candidate = std::dynamic_pointer_cast<const CanonicalPackageContentCandidate>(content.candidate_);
    if (!candidate)
      return {false, {store_issue(PackageContentIssueCode::package_content_encoding_invalid, id,
        "verified token does not contain a canonical logical package tree")}};
    const auto canonical = canonicalize_package_content_tree(candidate->tree());
    if (!canonical.valid() || canonical.contentId->digest() != id.digest() ||
        !parse_canonical_package_content(*canonical.canonicalBytes).valid())
      return {false, {store_issue(PackageContentIssueCode::package_content_encoding_invalid, id,
        "verified token failed canonical encoding or digest revalidation")}};

    std::string error;
    fs::path root;
    if (!prepare_root(root_, root, error))
      return {false, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, std::move(error))}};
    const auto path = object_path(root, id);
    if (!prepare_managed_directory(root, path.parent_path(), error))
      return {false, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, std::move(error))}};
    ContentFileLock lock{root / "content.lock"};
    if (!lock.valid())
      return {false, {store_issue(PackageContentIssueCode::package_content_store_lock_error, id, lock.error())}};

    const auto existing = inspect_path(path, id);
    if (existing.value == PackageContentAvailability::corrupt)
      return {false, existing.issues};
    if (!existing.valid()) return {false, existing.issues};
    if (*existing.value == PackageContentAvailability::available)
    {
      const auto read = read_object(path, id);
      if (read.state != ObjectReadState::valid)
        return {false, {store_issue(PackageContentIssueCode::package_content_store_corrupt, id,
          "existing object changed during admission")}};
      if (read.bytes != *canonical.canonicalBytes)
        return {false, {store_issue(PackageContentIssueCode::package_content_store_conflict, id,
          "different canonical bytes claim an existing content ID")}};
      return {true, {}};
    }

    // Recheck immediately before publication; the stored payload is exactly
    // the P10O byte stream and remains streamable by format, even though this
    // implementation currently buffers it in memory.
    if (sha256_digest(*canonical.canonicalBytes) != id.digest() ||
        !parse_canonical_package_content(*canonical.canonicalBytes).valid())
      return {false, {store_issue(PackageContentIssueCode::package_content_encoding_invalid, id,
        "canonical bytes failed pre-publication validation")}};
    const auto published = publish_immutable(path, *canonical.canonicalBytes, error);
    if (published == PublishState::failed)
      return {false, {store_issue(PackageContentIssueCode::package_content_store_io_error, id, std::move(error))}};
    if (published == PublishState::already_exists)
    {
      const auto raced = inspect_path(path, id);
      if (raced.value == PackageContentAvailability::corrupt) return {false, raced.issues};
      if (!raced.valid()) return {false, raced.issues};
      const auto read = read_object(path, id);
      if (read.state != ObjectReadState::valid || read.bytes != *canonical.canonicalBytes)
        return {false, {store_issue(PackageContentIssueCode::package_content_store_conflict, id,
          "concurrently published object conflicts with verified content")}};
    }
    return {true, {}};
  }
} // namespace vix::cli::project::native
