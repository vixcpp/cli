/** @file Resolver.hpp
 *  Pure deterministic resolution over caller-supplied immutable metadata.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_RESOLVER_HPP
#define VIX_CLI_PROJECT_NATIVE_RESOLVER_HPP

#include <optional>
#include <string>
#include <vector>

#include <vix/cli/project/native/Lockfile.hpp>
#include <vix/cli/project/native/Models.hpp>

namespace vix::cli::project::native
{
  struct RegistryCandidate
  {
    RegistryPackageIdentity identity;
    Version version;
    std::string contentDigest;
    PackageContract contract;
  };

  struct GitResolution
  {
    std::string normalizedRepository;
    GitRevisionRequirement requestedRevision;
    std::string commit;
    std::string contentDigest;
    PackageContract contract;
  };

  // Implementations are metadata-only adapters. This interface performs no
  // I/O itself; tests and later source adapters supply immutable records.
  class ResolutionMetadataProvider
  {
  public:
    virtual ~ResolutionMetadataProvider() = default;
    [[nodiscard]] virtual std::vector<RegistryCandidate> registry_candidates(
      const RegistryPackageIdentity &identity) const = 0;
    [[nodiscard]] virtual std::vector<GitResolution> git_resolutions(
      const std::string &normalizedRepository,
      const GitRevisionRequirement &revision) const = 0;
  };

  enum class ResolverErrorCode
  {
    invalid_requirement,
    no_root_requirements,
    no_candidate,
    constraint_conflict,
    git_revision_unresolved,
    git_commit_conflict,
    package_contract_mismatch,
    dependency_cycle,
    resolution_depth_exceeded,
    invalid_provider_metadata
  };

  struct ResolverConstraintContext
  {
    std::string alias;
    std::string requirement;
    std::string dependencyPath;
  };

  struct ResolverIssue
  {
    ResolverErrorCode code{ResolverErrorCode::invalid_provider_metadata};
    std::string identity;
    std::string message;
    std::vector<ResolverConstraintContext> constraints;
  };

  template <typename T>
  struct ResolverResult
  {
    std::optional<T> value;
    std::vector<ResolverIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return value.has_value() && issues.empty(); }
  };

  struct ResolvedDependency
  {
    std::string alias;
    PackageIdentity identity;
  };

  struct ResolvedRegistryNode
  {
    RegistryPackageIdentity identity;
    Version version;
    std::string contentDigest;
    std::vector<ResolvedDependency> dependencies;
  };

  struct ResolvedGitNode
  {
    std::string normalizedRepository;
    bool repositoryRoot{true};
    std::string commit;
    std::string contentDigest;
    std::vector<ResolvedDependency> dependencies;
  };

  using ResolvedNode = std::variant<ResolvedRegistryNode, ResolvedGitNode>;

  struct ResolvedRoot
  {
    std::string alias;
    std::variant<NormalizedDeclaredRegistry, NormalizedDeclaredGit> declared;
    PackageIdentity identity;
  };

  struct ResolvedGraph
  {
    std::vector<ResolvedRoot> roots;
    std::vector<ResolvedNode> nodes;
  };

  [[nodiscard]] ResolverResult<ResolvedGraph> resolve_packages(
    const std::vector<PackageRequirement> &rootRequirements,
    const ResolutionMetadataProvider &provider);

  [[nodiscard]] ResolverResult<Lockfile> make_resolved_lockfile(const ResolvedGraph &graph);
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_RESOLVER_HPP
