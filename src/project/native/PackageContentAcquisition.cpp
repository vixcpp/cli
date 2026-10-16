#include <vix/cli/project/native/PackageContentAcquisition.hpp>

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>

namespace vix::cli::project::native
{
  namespace
  {
    PackageContentAcquisitionIssue acquisition_issue(PackageContentAcquisitionIssueCode code,
      const PackageContentRequirement &required, std::string message)
    {
      return {code, required.id.digest(), required.nodeIds, std::move(message), {}, {}, {}};
    }

    const LockNode *find_node(const Lockfile &lockfile, const std::string &id)
    {
      const auto found = std::find_if(lockfile.nodes.begin(), lockfile.nodes.end(), [&](const auto &node)
      { return std::visit([&](const auto &value) { return value.id == id; }, node); });
      return found == lockfile.nodes.end() ? nullptr : &*found;
    }

    PackageContentAcquisitionIssue store_failure(const PackageContentRequirement &required,
      std::vector<PackageContentIssue> issues, std::string message)
    {
      if (!issues.empty() && !issues.front().message.empty())
        message += ": " + issues.front().message;
      auto result = acquisition_issue(PackageContentAcquisitionIssueCode::package_content_store_failure,
        required, std::move(message));
      result.contentIssues = std::move(issues);
      return result;
    }

    PackageContentAcquisitionResult failure(PackageContentAcquisitionIssue issue)
    { return {{}, {std::move(issue)}}; }

    PackageContentAcquisitionIssue integrity_failure(const PackageContentRequirement &required,
      std::vector<CanonicalPackageContentIssue> issues, std::string message)
    {
      auto result = acquisition_issue(PackageContentAcquisitionIssueCode::package_content_integrity_failure,
        required, std::move(message));
      result.canonicalIssues = std::move(issues);
      return result;
    }

    PackageContentAcquisitionResult verify_and_admit(
      const PackageContentRequirement &required,
      std::shared_ptr<const CanonicalPackageContentCandidate> candidate,
      PackageContentStore &store,
      std::string source)
    {
      if (!candidate)
        return failure(integrity_failure(required, {}, source + " produced no canonical package candidate"));

      const auto canonical = canonicalize_package_content_tree(candidate->tree());
      if (!canonical.valid())
        return failure(integrity_failure(required, canonical.issues,
          source + " produced an invalid canonical package tree"));
      if (*canonical.contentId != required.id)
        return failure(acquisition_issue(PackageContentAcquisitionIssueCode::package_content_digest_mismatch,
          required, source + " content digest differs from the exact lock digest"));

      CanonicalPackageContentVerifier verifier;
      const auto verified = verifier.verify(std::move(candidate), required.id);
      if (!verified.valid())
      {
        auto issue = integrity_failure(required, {}, source + " content failed P10O verification");
        issue.contentIssues = verified.issues;
        return failure(std::move(issue));
      }
      const auto admitted = store.admit(*verified.value);
      if (!admitted.valid())
        return failure(store_failure(required, admitted.issues,
          "verified package content could not be admitted to P10P"));
      return {{{required.id, PackageContentAcquisitionState::acquired, required.nodeIds}}, {}};
    }

    PackageContentAcquisitionResult acquire_registry(
      const PackageContentRequirement &required,
      const RegistryLockNode &node,
      const RegistryDirectory &registries,
      RegistryPackageContentSource &source,
      PackageContentStore &store)
    {
      const auto *definition = registries.find(node.identity.registry);
      if (!definition)
        return failure(acquisition_issue(PackageContentAcquisitionIssueCode::unknown_registry,
          required, "locked Registry ID has no routing definition"));

      if (definition->contentLocations.empty())
        return failure(acquisition_issue(PackageContentAcquisitionIssueCode::package_content_source_unavailable,
          required, "registry has no package content locations configured"));

      const RegistryPackageContentRequest request{node.identity, node.version, required.id};
      std::vector<RegistryLocationAcquisitionDiagnostic> diagnostics;
      bool sawTransportFailure = false;
      for (const auto &location : definition->contentLocations)
      {
        const auto response = source.acquire(location, request);
        if (response.status == RegistryPackageContentSourceStatus::configuration_error ||
            response.status == RegistryPackageContentSourceStatus::payload_too_large)
        {
          diagnostics.push_back({location.descriptor, response.status, response.diagnostic});
          const auto code = response.status == RegistryPackageContentSourceStatus::configuration_error
            ? PackageContentAcquisitionIssueCode::package_content_source_configuration_error
            : PackageContentAcquisitionIssueCode::package_content_payload_too_large;
          auto issue = acquisition_issue(code, required,
            response.diagnostic.empty() ? "Registry content transport failed terminally" : response.diagnostic);
          issue.registryLocations = std::move(diagnostics);
          return failure(std::move(issue));
        }
        if (response.status != RegistryPackageContentSourceStatus::available)
        {
          sawTransportFailure = sawTransportFailure ||
            response.status == RegistryPackageContentSourceStatus::transport_failure ||
            response.status == RegistryPackageContentSourceStatus::permission_denied;
          diagnostics.push_back({location.descriptor, response.status, response.diagnostic});
          continue;
        }

        if (!response.canonicalBytes)
        {
          auto issue = integrity_failure(required, {},
            "Registry source reported success without canonical object bytes");
          issue.registryLocations = std::move(diagnostics);
          issue.registryLocations.push_back({location.descriptor, response.status,
            "successful response omitted its object payload"});
          return failure(std::move(issue));
        }
        const auto parsed = parse_canonical_package_content(*response.canonicalBytes);
        if (!parsed.valid())
        {
          auto issue = integrity_failure(required, parsed.issues,
            "Registry source returned malformed or noncanonical P10O bytes");
          issue.registryLocations = std::move(diagnostics);
          issue.registryLocations.push_back({location.descriptor, response.status,
            "object failed strict canonical parsing"});
          return failure(std::move(issue));
        }
        if (*parsed.contentId != required.id)
        {
          auto issue = acquisition_issue(PackageContentAcquisitionIssueCode::package_content_digest_mismatch,
            required, "Registry object digest differs from the exact lock digest");
          issue.registryLocations = std::move(diagnostics);
          issue.registryLocations.push_back({location.descriptor, response.status,
            "canonical object digest did not match the lock"});
          return failure(std::move(issue));
        }
        auto candidate = std::make_shared<CanonicalPackageContentCandidate>(*parsed.tree);
        auto result = verify_and_admit(required, std::move(candidate), store, "Registry");
        if (!result.valid())
        {
          result.issues.front().registryLocations = std::move(diagnostics);
          result.issues.front().registryLocations.push_back({location.descriptor, response.status,
            "content integrity/admission failed; mirror fallback stopped"});
          return result;
        }
        return result;
      }

      auto issue = acquisition_issue(sawTransportFailure ?
        PackageContentAcquisitionIssueCode::package_content_transport_failure :
        PackageContentAcquisitionIssueCode::package_content_source_unavailable,
        required, "no configured Registry location supplied the locked content object");
      issue.registryLocations = std::move(diagnostics);
      return failure(std::move(issue));
    }

    PackageContentAcquisitionResult acquire_git(
      const PackageContentRequirement &required,
      const GitLockNode &node,
      GitPackageContentSource &source,
      PackageContentStore &store)
    {
      const GitPackageContentRequest request{node.normalizedRepository, node.repositoryRoot,
        node.commit, required.id};
      const auto response = source.acquire(request);
      if (response.status == PackageContentSourceStatus::unavailable)
        return failure(acquisition_issue(PackageContentAcquisitionIssueCode::package_content_source_unavailable,
          required, response.diagnostic.empty() ? "Git content source is unavailable" : response.diagnostic));
      if (response.status == PackageContentSourceStatus::transport_failure)
        return failure(acquisition_issue(PackageContentAcquisitionIssueCode::package_content_git_source_failure,
          required, response.diagnostic.empty() ? "Git content transport failed" : response.diagnostic));
      if (!response.candidate)
        return failure(integrity_failure(required, {},
          "Git source reported success without a canonical package tree"));
      return verify_and_admit(required, response.candidate, store, "Git");
    }
  } // namespace

  PackageContentAcquisitionResult acquire_locked_package_content(
    const Lockfile &lockfile,
    AcquisitionMode mode,
    const RegistryDirectory &registries,
    RegistryPackageContentSource &registrySource,
    GitPackageContentSource &gitSource,
    PackageContentStore &store)
  {
    const auto plan = required_package_content(lockfile);
    if (!plan.valid())
    {
      PackageContentAcquisitionIssue issue;
      issue.code = PackageContentAcquisitionIssueCode::invalid_lockfile;
      issue.message = "locked package content plan is invalid";
      issue.contentIssues = plan.issues;
      return failure(std::move(issue));
    }

    std::vector<bool> missing;
    missing.reserve(plan.required.size());
    std::optional<PackageContentAcquisitionIssue> preflightFailure;
    // Preflight the full plan so corrupt/unreadable local objects fail before
    // any source is called or any new content is admitted.
    for (const auto &required : plan.required)
    {
      const auto inspected = store.inspect(required.id);
      if (!inspected.valid() || !inspected.value || *inspected.value == PackageContentAvailability::corrupt)
      {
        if (!preflightFailure)
          preflightFailure = store_failure(required, inspected.issues,
            "local package content store inspection failed closed");
        missing.push_back(false);
      }
      else
        missing.push_back(*inspected.value == PackageContentAvailability::missing);
    }
    if (preflightFailure)
      return failure(std::move(*preflightFailure));

    // Offline policy is intentionally applied only after inspecting the whole
    // deterministic plan. This keeps corruption stronger than absence and
    // lets a complete durable P10P store be reused without source access.
    if (mode == AcquisitionMode::offline)
    {
      const auto firstMissing = std::find(missing.begin(), missing.end(), true);
      if (firstMissing != missing.end())
      {
        const auto index = static_cast<std::size_t>(std::distance(missing.begin(), firstMissing));
        return failure(acquisition_issue(
          PackageContentAcquisitionIssueCode::offline_package_content_unavailable,
          plan.required[index],
          "content required by the lock is not available locally; offline mode prohibits package source acquisition (" +
            plan.required[index].id.digest() + ")"));
      }
    }

    PackageContentAcquisitionResult result;
    for (std::size_t index = 0; index < plan.required.size(); ++index)
    {
      const auto &required = plan.required[index];
      if (!missing[index])
      {
        result.entries.push_back({required.id, PackageContentAcquisitionState::reused, required.nodeIds});
        continue;
      }
      const auto *node = required.nodeIds.empty() ? nullptr : find_node(lockfile, required.nodeIds.front());
      if (!node)
        return failure(acquisition_issue(PackageContentAcquisitionIssueCode::invalid_lockfile,
          required, "content plan refers to no locked package node"));
      auto acquired = std::visit([&](const auto &value) -> PackageContentAcquisitionResult
      {
        using Node = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Node, RegistryLockNode>)
          return acquire_registry(required, value, registries, registrySource, store);
        else
          return acquire_git(required, value, gitSource, store);
      }, *node);
      if (!acquired.valid()) return acquired;
      result.entries.push_back(std::move(acquired.entries.front()));
    }
    return result;
  }
} // namespace vix::cli::project::native
