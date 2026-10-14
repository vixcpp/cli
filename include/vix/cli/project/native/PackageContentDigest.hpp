/** @file PackageContentDigest.hpp
 *  Canonical logical package-tree content identity, version 1.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_DIGEST_HPP
#define VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_DIGEST_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vix/cli/project/native/PackageContentStore.hpp>

namespace vix::cli::project::native
{
  enum class CanonicalPackageEntryKind
  {
    file,
    symlink,
  };

  // This is a logical tree model, not an archive or filesystem transport
  // format. Directories and transport metadata are intentionally absent.
  struct CanonicalPackageEntry
  {
    std::string path;
    CanonicalPackageEntryKind kind{CanonicalPackageEntryKind::file};
    bool executable{false};
    std::vector<std::byte> content;
    std::string target;
  };

  struct CanonicalPackageTree
  {
    std::vector<CanonicalPackageEntry> entries;
  };

  enum class CanonicalPackageContentIssueCode
  {
    invalid_path,
    invalid_utf8,
    duplicate_package_content_path,
    package_content_tree_conflict,
    invalid_symlink_target,
    unsupported_package_content_entry,
    invalid_canonical_encoding,
  };

  struct CanonicalPackageContentIssue
  {
    CanonicalPackageContentIssueCode code{CanonicalPackageContentIssueCode::invalid_path};
    std::string path;
    std::string message;
  };

  struct CanonicalPackageContentResult
  {
    std::optional<std::string> canonicalBytes;
    std::optional<PackageContentId> contentId;
    std::vector<CanonicalPackageContentIssue> issues;
    [[nodiscard]] bool valid() const noexcept
    { return canonicalBytes.has_value() && contentId.has_value() && issues.empty(); }
  };

  // Encodes and hashes only the supplied logical package tree. File payloads
  // are held in memory here as a semantic/testing representation; later
  // acquisition may stream the same canonical byte domain.
  [[nodiscard]] CanonicalPackageContentResult canonicalize_package_content_tree(
    const CanonicalPackageTree &tree);

  struct CanonicalPackageContentParseResult
  {
    std::optional<CanonicalPackageTree> tree;
    std::optional<PackageContentId> contentId;
    std::vector<CanonicalPackageContentIssue> issues;
    [[nodiscard]] bool valid() const noexcept
    { return tree.has_value() && contentId.has_value() && issues.empty(); }
  };

  // Strict inverse of the P10O canonical encoding. Successful parses are
  // required to serialize back to the exact input bytes.
  [[nodiscard]] CanonicalPackageContentParseResult parse_canonical_package_content(
    std::string_view bytes);

  // Candidate adapter for the P10N verified-token boundary. Its immutable
  // logical tree is hashed before the verifier can mint a token.
  class CanonicalPackageContentCandidate final : public PackageContentCandidate
  {
  public:
    explicit CanonicalPackageContentCandidate(CanonicalPackageTree tree) : tree_(std::move(tree)) {}
    [[nodiscard]] const CanonicalPackageTree &tree() const noexcept { return tree_; }
  private:
    CanonicalPackageTree tree_;
  };

  class CanonicalPackageContentVerifier final : public PackageContentVerifier
  {
  public:
    [[nodiscard]] PackageContentVerificationResult verify(
      std::shared_ptr<const PackageContentCandidate> candidate,
      const PackageContentId &expected) const override;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_DIGEST_HPP
