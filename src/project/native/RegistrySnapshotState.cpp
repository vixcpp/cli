#include <vix/cli/project/native/RegistrySnapshotState.hpp>

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    bool valid_digest(std::string_view value)
    {
      const auto colon = value.find(':');
      if (colon == std::string_view::npos) return false;
      const auto algorithm = value.substr(0, colon);
      const auto hex = value.substr(colon + 1);
      const bool size = (algorithm == "sha256" && hex.size() == 64) ||
        (algorithm == "sha512" && hex.size() == 128);
      return size && std::all_of(hex.begin(), hex.end(), [](char c)
        { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    RegistrySnapshotStateIssue state_issue(RegistrySnapshotStateErrorCode code,
      std::string registryId, std::string message)
    { return {code, std::move(registryId), std::move(message)}; }

    bool valid_state(const TrustedRegistrySnapshotState &state)
    { return valid_registry_id(state.registryId) && state.sequence > 0 && valid_digest(state.digest); }

    bool valid_snapshot(const RegistrySnapshotIdentity &snapshot)
    { return valid_registry_id(snapshot.registryId) && snapshot.sequence > 0 && valid_digest(snapshot.digest); }

    std::string code_text(RegistrySnapshotStateErrorCode code)
    {
      switch (code)
      {
        case RegistrySnapshotStateErrorCode::invalid_trusted_snapshot_state: return "invalid_trusted_snapshot_state";
        case RegistrySnapshotStateErrorCode::registry_rollback_detected: return "registry_rollback_detected";
        case RegistrySnapshotStateErrorCode::registry_snapshot_equivocation: return "registry_snapshot_equivocation";
        case RegistrySnapshotStateErrorCode::registry_snapshot_changed_during_acquisition:
          return "registry_snapshot_changed_during_acquisition";
      }
      return "registry_snapshot_rejected";
    }

    std::string verification_code_text(RegistryVerificationErrorCode code)
    {
      switch (code)
      {
        case RegistryVerificationErrorCode::unknown_registry_authority: return "unknown_registry_authority";
        case RegistryVerificationErrorCode::invalid_snapshot_identity: return "invalid_snapshot_identity";
        case RegistryVerificationErrorCode::registry_binding_mismatch: return "registry_binding_mismatch";
        case RegistryVerificationErrorCode::package_query_binding_mismatch: return "package_query_binding_mismatch";
        case RegistryVerificationErrorCode::authority_verification_failed: return "authority_verification_failed";
      }
      return "authority_verification_failed";
    }

    std::string routing_code_text(RegistrySnapshotRoutingErrorCode code)
    {
      switch (code)
      {
        case RegistrySnapshotRoutingErrorCode::unknown_registry: return "unknown_registry";
        case RegistrySnapshotRoutingErrorCode::unknown_registry_authority: return "unknown_registry_authority";
        case RegistrySnapshotRoutingErrorCode::location_or_verification_failure: return "registry_source_or_authority_failure";
        case RegistrySnapshotRoutingErrorCode::snapshot_rejected: return "registry_snapshot_rejected";
        case RegistrySnapshotRoutingErrorCode::all_registry_locations_rejected:
          return "all_registry_locations_rejected";
        case RegistrySnapshotRoutingErrorCode::terminal_location_or_authority_failure:
          return "terminal_registry_location_or_authority_failure";
      }
      return "registry_source_or_trust_failure";
    }
  }

  RegistrySnapshotAcceptance check_registry_snapshot_state(
    const std::optional<TrustedRegistrySnapshotState> &previous,
    const VerifiedRegistryObservation &observation)
  {
    RegistrySnapshotAcceptance result;
    const auto &candidate = observation.snapshot();
    if (!valid_snapshot(candidate) || observation.query().registry != candidate.registryId)
    {
      result.issues.push_back(state_issue(RegistrySnapshotStateErrorCode::invalid_trusted_snapshot_state,
        candidate.registryId, "verified observation has an invalid Registry snapshot identity"));
      return result;
    }
    if (previous)
    {
      if (!valid_state(*previous) || previous->registryId != candidate.registryId)
      {
        result.issues.push_back(state_issue(RegistrySnapshotStateErrorCode::invalid_trusted_snapshot_state,
          candidate.registryId, "trusted snapshot state is invalid or belongs to another Registry"));
        return result;
      }
      if (candidate.sequence < previous->sequence)
      {
        result.issues.push_back(state_issue(RegistrySnapshotStateErrorCode::registry_rollback_detected,
          candidate.registryId, "verified snapshot sequence is lower than the trusted baseline"));
        return result;
      }
      if (candidate.sequence == previous->sequence && candidate.digest != previous->digest)
      {
        result.issues.push_back(state_issue(RegistrySnapshotStateErrorCode::registry_snapshot_equivocation,
          candidate.registryId, "same snapshot sequence has a different digest than the trusted baseline"));
        return result;
      }
    }
    result.observation = observation;
    result.proposedState = TrustedRegistrySnapshotState{candidate.registryId, candidate.sequence, candidate.digest};
    return result;
  }

  RegistrySnapshotGuard::RegistrySnapshotGuard(const RegistryDirectory &locations,
    const RegistryAuthorityDirectory &authorities, RegistryObservationLocationSource &registrySource,
    const RegistryAuthorityVerifier &verifier, PackageMetadataSource &gitSource,
    std::vector<TrustedRegistrySnapshotState> trustedStates)
    : locations_(locations), authorities_(authorities), registrySource_(registrySource), verifier_(verifier),
      gitSource_(gitSource), trustedStates_(std::move(trustedStates))
  {
    std::sort(trustedStates_.begin(), trustedStates_.end(), [](const auto &a, const auto &b)
      { return a.registryId < b.registryId; });
  }

  RegistryQueryResult RegistrySnapshotGuard::registry_releases(const RegistryPackageIdentity &identity)
  {
    lastIssues_.clear();
    const auto *registry = locations_.find(identity.registry);
    if (!registry)
    {
      lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::unknown_registry, identity.registry, {},
        "no Registry directory definition exists for this logical Registry ID"});
      return {false, {}, "unknown_registry [" + identity.registry + "]: no Registry directory definition exists"};
    }
    if (!authorities_.find(identity.registry))
    {
      lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::unknown_registry_authority, identity.registry, {},
        "no authority is configured for this logical Registry ID"});
      return {false, {}, "unknown_registry_authority [" + identity.registry + "]: no Registry authority is configured"};
    }
    else
    {
      const auto trusted = std::lower_bound(trustedStates_.begin(), trustedStates_.end(), identity.registry,
        [](const auto &state, std::string_view registryId) { return state.registryId < registryId; });
      const bool duplicateTrusted = trusted != trustedStates_.end() && trusted->registryId == identity.registry &&
        std::next(trusted) != trustedStates_.end() && std::next(trusted)->registryId == identity.registry;
      if (duplicateTrusted)
      {
        lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::snapshot_rejected, identity.registry, {},
          "invalid_trusted_snapshot_state: more than one trusted baseline exists for this Registry"});
      }
      else
      {
        const std::optional<TrustedRegistrySnapshotState> previous =
          trusted != trustedStates_.end() && trusted->registryId == identity.registry
            ? std::optional<TrustedRegistrySnapshotState>{*trusted} : std::nullopt;
        for (const auto &location : registry->locations)
        {
          auto response = registrySource_.registry_observation_at(location, identity);
          if (response.status != RegistryObservationLocationStatus::available)
          {
            lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::location_or_verification_failure,
              identity.registry, location.descriptor,
              response.diagnostic.empty() ? "location failed to return an observation" : std::move(response.diagnostic)});
            if (response.status == RegistryObservationLocationStatus::not_found ||
                response.status == RegistryObservationLocationStatus::transport_failure ||
                response.status == RegistryObservationLocationStatus::permission_denied)
              continue;
            lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::terminal_location_or_authority_failure,
              identity.registry, location.descriptor, "terminal Registry metadata location failure"});
            return {false, {}, "terminal Registry metadata location failure; mirror fallback is forbidden"};
          }
          if (!response.observation)
          {
            lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::terminal_location_or_authority_failure,
              identity.registry, location.descriptor, "available Registry metadata response omitted its observation"});
            return {false, {}, "available Registry metadata response omitted its observation"};
          }
          auto verified = verify_registry_observation(authorities_, verifier_, identity, *response.observation);
          if (!verified.valid())
          {
            for (const auto &issue : verified.issues)
              lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::location_or_verification_failure,
                identity.registry, location.descriptor, verification_code_text(issue.code) + ": " + issue.message});
            lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::terminal_location_or_authority_failure,
              identity.registry, location.descriptor, "Registry authority verification failed; mirror fallback is forbidden"});
            return {false, {}, "Registry authority verification failed; mirror fallback is forbidden"};
          }
          auto accepted = check_registry_snapshot_state(previous, *verified.observation);
          if (!accepted.valid())
          {
            for (const auto &issue : accepted.issues)
              lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::snapshot_rejected,
                identity.registry, location.descriptor, code_text(issue.code) + ": " + issue.message});
            return {false, {}, "Registry snapshot rejected; mirror fallback is forbidden"};
          }
          const auto pin = std::lower_bound(pins_.begin(), pins_.end(), identity.registry,
            [](const auto &value, std::string_view registryId) { return value.registryId < registryId; });
          if (pin != pins_.end() && pin->registryId == identity.registry &&
              !(pin->snapshot == accepted.observation->snapshot()))
          {
            lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::snapshot_rejected,
              identity.registry, location.descriptor,
              "registry_snapshot_changed_during_acquisition: observation differs from this session's pinned snapshot"});
            return {false, {}, "Registry snapshot changed during acquisition; mirror fallback is forbidden"};
          }
          if (pin == pins_.end() || pin->registryId != identity.registry)
            pins_.insert(pin, RegistrySnapshotPin{identity.registry, accepted.observation->snapshot()});
          const auto proposed = std::lower_bound(proposedStates_.begin(), proposedStates_.end(), identity.registry,
            [](const auto &value, std::string_view registryId) { return value.registryId < registryId; });
          const TrustedRegistrySnapshotState next = *accepted.proposedState;
          if (proposed == proposedStates_.end() || proposed->registryId != identity.registry)
            proposedStates_.insert(proposed, next);
          else
            *proposed = next;
          return {true, accepted.observation->releases(), {}};
        }
      }
    }

    if (lastIssues_.empty())
      lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::location_or_verification_failure,
        identity.registry, {}, "no configured location produced an acceptable observation"});
    lastIssues_.push_back({RegistrySnapshotRoutingErrorCode::all_registry_locations_rejected,
      identity.registry, {}, "all Registry locations failed authority, rollback, or acquisition-coherence checks"});
    std::string diagnostic;
    for (const auto &issue : lastIssues_)
    {
      if (!diagnostic.empty()) diagnostic += "; ";
      diagnostic += routing_code_text(issue.code) + " [" + issue.registryId + "]";
      if (!issue.location.empty()) diagnostic += " at " + issue.location;
      diagnostic += ": " + issue.message;
    }
    return {false, {}, std::move(diagnostic)};
  }

  GitQueryResult RegistrySnapshotGuard::resolve_git(const GitPackageIdentity &identity,
    const GitRevisionRequirement &revision)
  { return gitSource_.resolve_git(identity, revision); }
} // namespace vix::cli::project::native
