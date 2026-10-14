/** @file PackageContentStore.hpp
 *  Verified immutable package-content availability semantics.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_STORE_HPP
#define VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_STORE_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vix/cli/project/native/Lockfile.hpp>

namespace vix::cli::project::native
{
  class PackageContentId final
  {
  public:
    [[nodiscard]] static std::optional<PackageContentId> parse(std::string digest);
    [[nodiscard]] const std::string &digest() const noexcept { return digest_; }
    friend bool operator==(const PackageContentId &, const PackageContentId &) = default;
    friend bool operator<(const PackageContentId &left, const PackageContentId &right) noexcept
    { return left.digest_ < right.digest_; }
  private:
    explicit PackageContentId(std::string digest) : digest_(std::move(digest)) {}
    std::string digest_;
  };

  // Candidate representation is deliberately opaque: archive/tree/Git bytes
  // and their canonical digest representation belong to a later contract.
  // A verifier must bind a stable candidate handle; callers must not mutate
  // candidate state after verification.
  class PackageContentCandidate
  {
  public:
    virtual ~PackageContentCandidate() = default;
  };

  class VerifiedPackageContent final
  {
  public:
    [[nodiscard]] const PackageContentId &id() const noexcept { return id_; }
  private:
    friend class PackageContentVerifier;
    friend class InMemoryPackageContentStore;
    VerifiedPackageContent(PackageContentId id,
      std::shared_ptr<const PackageContentCandidate> candidate, std::string equivalenceKey)
      : id_(std::move(id)), candidate_(std::move(candidate)), equivalenceKey_(std::move(equivalenceKey)) {}
    PackageContentId id_;
    std::shared_ptr<const PackageContentCandidate> candidate_;
    // Produced by the verifier; used only to detect contradictory test or
    // backend attestations for the same immutable content identity.
    std::string equivalenceKey_;
  };

  enum class PackageContentIssueCode
  {
    package_content_verification_failed,
    package_content_unavailable,
    package_content_conflict,
    invalid_content_id,
    invalid_lockfile
  };

  struct PackageContentIssue
  {
    PackageContentIssueCode code{PackageContentIssueCode::package_content_verification_failed};
    std::string digest;
    std::vector<std::string> nodeIds;
    std::string message;
  };

  struct PackageContentVerificationResult
  {
    std::optional<VerifiedPackageContent> value;
    std::vector<PackageContentIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  class PackageContentVerifier
  {
  public:
    virtual ~PackageContentVerifier() = default;
    [[nodiscard]] virtual PackageContentVerificationResult verify(
      std::shared_ptr<const PackageContentCandidate> candidate,
      const PackageContentId &expected) const = 0;
  protected:
    // Concrete verification policy is supplied by a later acquisition layer.
    // This factory lets that verifier mint the internal capability without
    // making VerifiedPackageContent publicly constructible.
    [[nodiscard]] PackageContentVerificationResult verified_content(
      std::shared_ptr<const PackageContentCandidate> candidate,
      const PackageContentId &id, std::string equivalenceKey) const;
    [[nodiscard]] PackageContentVerificationResult verification_failure(
      const PackageContentId &id, std::string message) const;
  };

  enum class PackageContentAvailability { missing, available };

  struct PackageContentStoreResult
  {
    std::optional<PackageContentAvailability> value;
    std::vector<PackageContentIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  struct PackageContentAdmissionResult
  {
    bool admitted{false};
    std::vector<PackageContentIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return admitted && issues.empty(); }
  };

  class PackageContentStore
  {
  public:
    virtual ~PackageContentStore() = default;
    [[nodiscard]] virtual PackageContentStoreResult inspect(const PackageContentId &id) const = 0;
    [[nodiscard]] virtual PackageContentAdmissionResult admit(const VerifiedPackageContent &content) = 0;
  };

  class InMemoryPackageContentStore final : public PackageContentStore
  {
  public:
    [[nodiscard]] PackageContentStoreResult inspect(const PackageContentId &id) const override;
    [[nodiscard]] PackageContentAdmissionResult admit(const VerifiedPackageContent &content) override;
    void clear() noexcept { entries_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  private:
    std::vector<VerifiedPackageContent> entries_;
  };

  struct PackageContentRequirement
  {
    PackageContentId id;
    std::vector<std::string> nodeIds;
  };

  struct PackageContentPlanResult
  {
    std::vector<PackageContentRequirement> required;
    std::vector<PackageContentIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
  };

  struct PackageContentAvailabilityReport
  {
    bool complete{false};
    std::vector<PackageContentRequirement> missing;
    std::vector<PackageContentIssue> issues;
    // Missing content is a valid availability result (incomplete, not a
    // malformed check); only planning/store inspection failures invalidate it.
    [[nodiscard]] bool valid() const noexcept
    {
      for (const auto &problem : issues)
        if (problem.code != PackageContentIssueCode::package_content_unavailable) return false;
      return true;
    }
  };

  [[nodiscard]] PackageContentPlanResult required_package_content(const Lockfile &lockfile);
  [[nodiscard]] PackageContentAvailabilityReport check_package_content_availability(
    const Lockfile &lockfile, const PackageContentStore &store);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_PACKAGE_CONTENT_STORE_HPP
