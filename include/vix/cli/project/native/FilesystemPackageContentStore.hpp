/** @file FilesystemPackageContentStore.hpp
 *  Durable storage for immutable P10O canonical package-content objects.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_FILESYSTEM_PACKAGE_CONTENT_STORE_HPP
#define VIX_CLI_PROJECT_NATIVE_FILESYSTEM_PACKAGE_CONTENT_STORE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <vix/cli/project/native/PackageContentDigest.hpp>

namespace vix::cli::project::native
{
  struct PersistentPackageContentReadResult
  {
    std::optional<std::string> canonicalBytes;
    std::vector<PackageContentIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return canonicalBytes.has_value() && issues.empty(); }
  };

  // The root is explicit; user-home or project-local location policy belongs
  // to a higher-level integration layer. Integrity assumes the root remains
  // inside the local OS/user trust boundary and does not defend against a
  // malicious process with equivalent filesystem permissions.
  class FilesystemPackageContentStore final : public PackageContentStore
  {
  public:
    explicit FilesystemPackageContentStore(std::filesystem::path root) : root_(std::move(root)) {}

    [[nodiscard]] PackageContentStoreResult inspect(const PackageContentId &id) const override;
    [[nodiscard]] PackageContentAdmissionResult admit(const VerifiedPackageContent &content) override;
    [[nodiscard]] PersistentPackageContentReadResult read_canonical_object(const PackageContentId &id) const;

  private:
    std::filesystem::path root_;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_FILESYSTEM_PACKAGE_CONTENT_STORE_HPP
