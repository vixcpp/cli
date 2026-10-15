#include <vix/cli/project/native/RegistryAuthority.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    bool valid_snapshot_digest(std::string_view value)
    {
      const auto colon = value.find(':');
      if (colon == std::string_view::npos) return false;
      const auto algorithm = value.substr(0, colon);
      const auto hex = value.substr(colon + 1);
      const bool length = (algorithm == "sha256" && hex.size() == 64) ||
        (algorithm == "sha512" && hex.size() == 128);
      return length && std::all_of(hex.begin(), hex.end(), [](char c)
        { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    RegistryVerificationIssue verification_issue(RegistryVerificationErrorCode code,
      std::string registry, std::string message)
    {
      return {code, std::move(registry), std::move(message)};
    }
  }

  RegistryAuthorityDirectory::RegistryAuthorityDirectory(std::vector<RegistryAuthorityDefinition> definitions)
    : definitions_(std::move(definitions)) {}

  const RegistryAuthorityDefinition *RegistryAuthorityDirectory::find(std::string_view registryId) const noexcept
  {
    const auto found = std::lower_bound(definitions_.begin(), definitions_.end(), registryId,
      [](const auto &definition, std::string_view id) { return definition.registryId < id; });
    return found != definitions_.end() && found->registryId == registryId ? &*found : nullptr;
  }

  RegistryAuthorityDirectoryBuildResult build_registry_authority_directory(
    std::vector<RegistryAuthorityDefinition> definitions)
  {
    RegistryAuthorityDirectoryBuildResult result;
    std::sort(definitions.begin(), definitions.end(), [](const auto &a, const auto &b)
      { return a.registryId < b.registryId; });
    for (std::size_t i = 0; i < definitions.size(); ++i)
    {
      const auto &definition = definitions[i];
      if (!valid_registry_id(definition.registryId))
        result.issues.push_back({RegistryAuthorityDirectoryErrorCode::invalid_registry_id,
          definition.registryId, "authority definition requires a valid logical Registry ID"});
      if (definition.authorityId.empty())
        result.issues.push_back({RegistryAuthorityDirectoryErrorCode::empty_authority_id,
          definition.registryId, "authority handle must be nonempty"});
      if (i > 0 && definitions[i - 1].registryId == definition.registryId)
        result.issues.push_back({RegistryAuthorityDirectoryErrorCode::duplicate_registry_authority,
          definition.registryId, "Registry ID has more than one authority definition"});
    }
    if (!result.issues.empty()) return result;
    result.value = RegistryAuthorityDirectory{std::move(definitions)};
    return result;
  }

  RegistryObservationVerificationResult verify_registry_observation(
    const RegistryAuthorityDirectory &directory, const RegistryAuthorityVerifier &verifier,
    const RegistryPackageIdentity &requested, const UnverifiedRegistryObservation &observation)
  {
    RegistryObservationVerificationResult result;
    const auto *authority = directory.find(requested.registry);
    if (!authority)
    {
      result.issues.push_back(verification_issue(RegistryVerificationErrorCode::unknown_registry_authority,
        requested.registry, "no authority is configured for this logical Registry"));
      return result;
    }
    if (observation.snapshot.sequence == 0 || !valid_snapshot_digest(observation.snapshot.digest))
    {
      result.issues.push_back(verification_issue(RegistryVerificationErrorCode::invalid_snapshot_identity,
        requested.registry, "snapshot sequence or algorithm-qualified digest is invalid"));
      return result;
    }
    if (observation.snapshot.registryId != authority->registryId)
    {
      result.issues.push_back(verification_issue(RegistryVerificationErrorCode::registry_binding_mismatch,
        requested.registry, "snapshot is bound to a different Registry ID"));
      return result;
    }
    if (observation.query != requested)
    {
      result.issues.push_back(verification_issue(RegistryVerificationErrorCode::package_query_binding_mismatch,
        requested.registry, "observation is bound to a different package query"));
      return result;
    }
    const auto mismatchedRelease = std::find_if(observation.releases.begin(), observation.releases.end(),
      [&](const auto &release) { return release.identity != requested; });
    if (mismatchedRelease != observation.releases.end())
    {
      result.issues.push_back(verification_issue(RegistryVerificationErrorCode::package_query_binding_mismatch,
        requested.registry, "observation contains a release for a different package query"));
      return result;
    }
    std::string diagnostic;
    if (!verifier.verify_evidence(*authority, observation, diagnostic))
    {
      result.issues.push_back(verification_issue(RegistryVerificationErrorCode::authority_verification_failed,
        requested.registry, diagnostic.empty() ? "authority evidence did not authenticate this observation" : std::move(diagnostic)));
      return result;
    }
    result.observation = VerifiedRegistryObservation{observation.snapshot, observation.query, observation.releases};
    return result;
  }

  VerifiedRegistryRoutingResult route_verified_registry_observation(
    const RegistryDirectory &locations, const RegistryAuthorityDirectory &authorities,
    RegistryObservationLocationSource &source, const RegistryAuthorityVerifier &verifier,
    const RegistryPackageIdentity &identity)
  {
    VerifiedRegistryRoutingResult result;
    const auto *registry = locations.find(identity.registry);
    if (!registry)
    {
      result.issues.push_back({VerifiedRegistryRoutingErrorCode::unknown_registry, identity.registry, {},
        "no Registry directory definition exists for this logical Registry ID"});
      return result;
    }
    if (!authorities.find(identity.registry))
    {
      result.issues.push_back({VerifiedRegistryRoutingErrorCode::unknown_registry_authority, identity.registry, {},
        "no authority is configured for this logical Registry ID"});
      return result;
    }
    for (const auto &location : registry->locations)
    {
      auto response = source.registry_observation_at(location, identity);
      if (response.status != RegistryObservationLocationStatus::available)
      {
        result.issues.push_back({VerifiedRegistryRoutingErrorCode::location_or_verification_failure,
          identity.registry, location.descriptor,
          response.diagnostic.empty() ? "location failed to return an observation" : std::move(response.diagnostic)});
        if (response.status == RegistryObservationLocationStatus::not_found ||
            response.status == RegistryObservationLocationStatus::transport_failure ||
            response.status == RegistryObservationLocationStatus::permission_denied)
          continue;
        result.issues.push_back({VerifiedRegistryRoutingErrorCode::terminal_location_or_authority_failure,
          identity.registry, location.descriptor, "terminal Registry metadata location failure"});
        return result;
      }
      if (!response.observation)
      {
        result.issues.push_back({VerifiedRegistryRoutingErrorCode::terminal_location_or_authority_failure,
          identity.registry, location.descriptor, "available Registry metadata response omitted its observation"});
        return result;
      }
      auto verified = verify_registry_observation(authorities, verifier, identity, *response.observation);
      if (verified.valid())
      {
        result.observation = std::move(verified.observation);
        result.unverifiedObservation = *response.observation;
        result.issues.clear();
        return result;
      }
      for (const auto &issue : verified.issues)
        result.issues.push_back({VerifiedRegistryRoutingErrorCode::location_or_verification_failure,
          identity.registry, location.descriptor, issue.message});
      result.issues.push_back({VerifiedRegistryRoutingErrorCode::terminal_location_or_authority_failure,
        identity.registry, location.descriptor, "Registry authority verification failed; mirror fallback is forbidden"});
      return result;
    }
    if (result.issues.empty())
      result.issues.push_back({VerifiedRegistryRoutingErrorCode::location_or_verification_failure,
        identity.registry, {}, "no configured location produced an authenticated observation"});
    result.issues.push_back({VerifiedRegistryRoutingErrorCode::all_registry_locations_untrusted_or_failed,
      identity.registry, {}, "no configured Registry location produced an authority-verified observation"});
    return result;
  }

  VerifiedRoutedPackageMetadataSource::VerifiedRoutedPackageMetadataSource(
    const RegistryDirectory &locations, const RegistryAuthorityDirectory &authorities,
    RegistryObservationLocationSource &registrySource, const RegistryAuthorityVerifier &verifier,
    PackageMetadataSource &gitSource)
    : locations_(locations), authorities_(authorities), registrySource_(registrySource),
      verifier_(verifier), gitSource_(gitSource) {}

  RegistryQueryResult VerifiedRoutedPackageMetadataSource::registry_releases(const RegistryPackageIdentity &identity)
  {
    auto routed = route_verified_registry_observation(locations_, authorities_, registrySource_, verifier_, identity);
    if (routed.valid()) return {true, routed.observation->releases(), {}};
    std::string diagnostic;
    for (const auto &issue : routed.issues)
    {
      if (!diagnostic.empty()) diagnostic += "; ";
      diagnostic += issue.code == VerifiedRegistryRoutingErrorCode::unknown_registry ? "unknown_registry" :
        issue.code == VerifiedRegistryRoutingErrorCode::unknown_registry_authority ? "unknown_registry_authority" :
        issue.code == VerifiedRegistryRoutingErrorCode::terminal_location_or_authority_failure ? "terminal_registry_location_or_authority_failure" :
        issue.code == VerifiedRegistryRoutingErrorCode::all_registry_locations_untrusted_or_failed ?
          "all_registry_locations_untrusted_or_failed" : "registry_authentication_or_location_failure";
      diagnostic += " [" + issue.registryId + "]";
      if (!issue.location.empty()) diagnostic += " at " + issue.location;
      diagnostic += ": " + issue.message;
    }
    return {false, {}, std::move(diagnostic)};
  }

  GitQueryResult VerifiedRoutedPackageMetadataSource::resolve_git(
    const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
  {
    return gitSource_.resolve_git(identity, revision);
  }
} // namespace vix::cli::project::native
