#include <vix/cli/project/native/PackageContentStore.hpp>

#include <algorithm>
#include <map>
#include <type_traits>
#include <utility>

#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    PackageContentIssue issue(PackageContentIssueCode code, std::string digest,
      std::vector<std::string> nodeIds, std::string message)
    { return {code, std::move(digest), std::move(nodeIds), std::move(message)}; }

    const std::string &node_id(const LockNode &node)
    { return std::visit([](const auto &value) -> const std::string & { return value.id; }, node); }
  }

  std::optional<PackageContentId> PackageContentId::parse(std::string digest)
  {
    if (!valid_package_content_digest(digest)) return std::nullopt;
    return PackageContentId{std::move(digest)};
  }

  PackageContentVerificationResult PackageContentVerifier::verified_content(
    std::shared_ptr<const PackageContentCandidate> candidate,
    const PackageContentId &id, std::string equivalenceKey) const
  {
    if (!candidate || equivalenceKey.empty())
      return verification_failure(id, "verifier cannot issue a token without candidate and equivalence identity");
    return {VerifiedPackageContent{id, std::move(candidate), std::move(equivalenceKey)}, {}};
  }

  PackageContentVerificationResult PackageContentVerifier::verification_failure(
    const PackageContentId &id, std::string message) const
  {
    return {std::nullopt, {issue(PackageContentIssueCode::package_content_verification_failed,
      id.digest(), {}, std::move(message))}};
  }

  PackageContentStoreResult InMemoryPackageContentStore::inspect(const PackageContentId &id) const
  {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto &entry) { return entry.id_ == id; });
    return {found == entries_.end() ? PackageContentAvailability::missing : PackageContentAvailability::available, {}};
  }

  PackageContentAdmissionResult InMemoryPackageContentStore::admit(const VerifiedPackageContent &content)
  {
    if (!content.candidate_ || content.equivalenceKey_.empty() ||
        !valid_package_content_digest(content.id_.digest()))
      return {false, {issue(PackageContentIssueCode::invalid_content_id, content.id_.digest(), {},
        "verified content token is structurally invalid")}};
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto &entry) { return entry.id_ == content.id_; });
    if (found != entries_.end())
    {
      if (found->equivalenceKey_ == content.equivalenceKey_) return {true, {}};
      return {false, {issue(PackageContentIssueCode::package_content_conflict, content.id_.digest(), {},
        "different verified content claims an already available content identity")}};
    }
    entries_.push_back(content);
    std::sort(entries_.begin(), entries_.end(), [](const auto &left, const auto &right) { return left.id_ < right.id_; });
    return {true, {}};
  }

  PackageContentPlanResult required_package_content(const Lockfile &lockfile)
  {
    PackageContentPlanResult result;
    const auto validation = validate_lockfile(lockfile);
    if (!validation.valid())
      for (const auto &problem : validation.issues)
        result.issues.push_back(issue(PackageContentIssueCode::invalid_lockfile, problem.subject, {}, problem.message));
    const auto integrity = validate_lockfile_integrity(lockfile);
    if (!integrity.valid())
      for (const auto &problem : integrity.issues)
        result.issues.push_back(issue(PackageContentIssueCode::invalid_lockfile, problem.subject, {}, problem.message));
    if (!result.issues.empty()) return result;

    std::map<std::string, std::vector<std::string>> byDigest;
    for (const auto &node : lockfile.nodes)
    {
      const auto &id = node_id(node);
      const auto digest = std::visit([](const auto &value) { return value.contentDigest; }, node);
      if (!PackageContentId::parse(digest))
      {
        result.issues.push_back(issue(PackageContentIssueCode::invalid_content_id, digest, {id},
          "lock node contains an invalid package content digest"));
        continue;
      }
      byDigest[digest].push_back(id);
    }
    if (!result.issues.empty()) return result;
    for (auto &[digest, nodeIds] : byDigest)
    {
      std::sort(nodeIds.begin(), nodeIds.end());
      result.required.push_back({*PackageContentId::parse(digest), std::move(nodeIds)});
    }
    return result;
  }

  PackageContentAvailabilityReport check_package_content_availability(
    const Lockfile &lockfile, const PackageContentStore &store)
  {
    PackageContentAvailabilityReport report;
    const auto plan = required_package_content(lockfile);
    if (!plan.valid())
    {
      report.issues = plan.issues;
      return report;
    }
    report.complete = true;
    for (const auto &required : plan.required)
    {
      const auto status = store.inspect(required.id);
      if (!status.valid())
      {
        report.complete = false;
        report.issues.insert(report.issues.end(), status.issues.begin(), status.issues.end());
      }
      else if (*status.value == PackageContentAvailability::missing)
      {
        report.complete = false;
        report.missing.push_back(required);
        report.issues.push_back(issue(PackageContentIssueCode::package_content_unavailable,
          required.id.digest(), required.nodeIds, "required immutable package content is not available"));
      }
    }
    return report;
  }
} // namespace vix::cli::project::native
