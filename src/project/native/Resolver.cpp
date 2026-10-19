#include <vix/cli/project/native/Resolver.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/Validation.hpp>

namespace vix::cli::project::native
{
  namespace
  {
    bool bytewise_less(std::string_view left, std::string_view right)
    {
      const auto common = std::min(left.size(), right.size());
      for (std::size_t index = 0; index < common; ++index)
      {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (lhs != rhs) return lhs < rhs;
      }
      return left.size() < right.size();
    }

    struct IdentityLess
    {
      bool operator()(const PackageIdentity &left, const PackageIdentity &right) const
      {
        if (left.index() != right.index()) return left.index() < right.index();
        if (const auto *registry = std::get_if<RegistryPackageIdentity>(&left))
        {
          const auto &other = std::get<RegistryPackageIdentity>(right);
          if (registry->registry != other.registry) return bytewise_less(registry->registry, other.registry);
          if (registry->publisher != other.publisher) return bytewise_less(registry->publisher, other.publisher);
          return bytewise_less(registry->name, other.name);
        }
        const auto &git = std::get<GitPackageIdentity>(left);
        const auto &other = std::get<GitPackageIdentity>(right);
        if (git.normalizedRepository != other.normalizedRepository)
          return bytewise_less(git.normalizedRepository, other.normalizedRepository);
        return git.repositoryRoot < other.repositoryRoot;
      }
    };

    bool semver_identifier_numeric(std::string_view value)
    {
      return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    int compare_numeric_identifier(std::string_view left, std::string_view right)
    {
      while (left.size() > 1 && left.front() == '0') left.remove_prefix(1);
      while (right.size() > 1 && right.front() == '0') right.remove_prefix(1);
      if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
      if (left == right) return 0;
      return left < right ? -1 : 1;
    }

    int compare_prerelease(std::string_view left, std::string_view right)
    {
      std::size_t leftBegin = 0;
      std::size_t rightBegin = 0;
      while (leftBegin <= left.size() && rightBegin <= right.size())
      {
        const auto leftEnd = left.find('.', leftBegin);
        const auto rightEnd = right.find('.', rightBegin);
        const auto leftPart = left.substr(leftBegin, leftEnd == std::string_view::npos ? left.size() - leftBegin : leftEnd - leftBegin);
        const auto rightPart = right.substr(rightBegin, rightEnd == std::string_view::npos ? right.size() - rightBegin : rightEnd - rightBegin);
        const bool leftNumeric = semver_identifier_numeric(leftPart);
        const bool rightNumeric = semver_identifier_numeric(rightPart);
        if (leftNumeric && rightNumeric)
        {
          const int comparison = compare_numeric_identifier(leftPart, rightPart);
          if (comparison != 0) return comparison;
        }
        else if (leftNumeric != rightNumeric) return leftNumeric ? -1 : 1;
        else if (leftPart != rightPart) return leftPart < rightPart ? -1 : 1;

        if (leftEnd == std::string_view::npos || rightEnd == std::string_view::npos)
        {
          if (leftEnd == rightEnd) return 0;
          return leftEnd == std::string_view::npos ? -1 : 1;
        }
        leftBegin = leftEnd + 1;
        rightBegin = rightEnd + 1;
      }
      return 0;
    }

    int compare_versions(const Version &left, const Version &right)
    {
      if (left.major != right.major) return left.major < right.major ? -1 : 1;
      if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
      if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
      if (!left.prerelease && !right.prerelease) return 0;
      if (!left.prerelease) return 1;
      if (!right.prerelease) return -1;
      return compare_prerelease(*left.prerelease, *right.prerelease);
    }

    bool satisfies(const Version &candidate, const VersionRequirement &requirement)
    {
      return std::visit([&](const auto &mode)
      {
        using Mode = std::decay_t<decltype(mode)>;
        if constexpr (std::is_same_v<Mode, ExactVersion>)
        {
          return candidate == mode.version;
        }
        else
        {
          if (candidate.prerelease) return false;
          const auto &minimum = mode.version;
          if (compare_versions(candidate, minimum) < 0) return false;
          if (minimum.major > 0) return candidate.major == minimum.major;
          if (minimum.minor > 0) return candidate.major == 0 && candidate.minor == minimum.minor;
          return candidate.major == 0 && candidate.minor == 0 && candidate.patch == minimum.patch;
        }
      }, requirement);
    }

    bool same_revision(const GitRevisionRequirement &left, const GitRevisionRequirement &right)
    {
      if (left.index() != right.index()) return false;
      return std::visit([&](const auto &value)
      {
        using Revision = std::decay_t<decltype(value)>;
        const auto *other = std::get_if<Revision>(&right);
        return other && value.value == other->value;
      }, left);
    }

    PackageIdentity package_identity(const PackageRequirement &requirement)
    {
      return std::visit([](const auto &source) -> PackageIdentity
      {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, RegistryRequirement>) return source.identity;
        else return GitPackageIdentity{source.normalizedRepository, source.repositoryRoot};
      }, requirement.source);
    }

    std::string identity_text(const PackageIdentity &identity)
    {
      return std::visit([](const auto &value)
      {
        using Identity = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Identity, RegistryPackageIdentity>)
          return std::string{"registry:"} + value.registry + "/" + value.publisher + "/" + value.name;
        else return std::string{"git:"} + value.normalizedRepository + (value.repositoryRoot ? "#." : "#<invalid-root>");
      }, identity);
    }

    std::string version_text(const Version &version)
    {
      std::string output = std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
      if (version.prerelease) output += "-" + *version.prerelease;
      return output;
    }

    std::string requirement_text(const PackageRequirement &requirement)
    {
      return std::visit([](const auto &source)
      {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, RegistryRequirement>)
          return std::visit([](const auto &mode)
          {
            using Mode = std::decay_t<decltype(mode)>;
            return std::string{std::is_same_v<Mode, ExactVersion> ? "exact " : "compatible "} + version_text(mode.version);
          }, source.version);
        else
          return std::visit([](const auto &revision)
          {
            using Revision = std::decay_t<decltype(revision)>;
            if constexpr (std::is_same_v<Revision, CommitRevision>)
              return std::string{"commit "} + revision.value;
            else if constexpr (std::is_same_v<Revision, TagRevision>)
              return std::string{"tag "} + revision.value;
            else
              return std::string{"branch "} + revision.value;
          }, source.revision);
      }, requirement.source);
    }

    std::string revision_text(const GitRevisionRequirement &revision)
    {
      return std::visit([](const auto &value)
      {
        using Revision = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Revision, CommitRevision>)
          return std::string{"commit "} + value.value;
        else if constexpr (std::is_same_v<Revision, TagRevision>)
          return std::string{"tag "} + value.value;
        else
          return std::string{"branch "} + value.value;
      }, revision);
    }

    bool valid_digest(std::string_view value)
    {
      const auto separator = value.find(':');
      if (separator == std::string_view::npos) return false;
      const auto algorithm = value.substr(0, separator);
      const auto hex = value.substr(separator + 1);
      const bool lengthMatches = (algorithm == "sha256" && hex.size() == 64) ||
        (algorithm == "sha512" && hex.size() == 128);
      return lengthMatches && std::all_of(hex.begin(), hex.end(), [](char c)
      { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    PackageRequirement from_contract(const PackageRequirement &requirement)
    {
      return requirement;
    }

    struct PendingRequirement
    {
      PackageRequirement requirement;
      std::string path;
    };

    struct Constraint
    {
      PackageRequirement requirement;
      std::string path;
    };

    struct Assigned { ResolvedNode node; };

    struct SearchState
    {
      std::vector<PendingRequirement> pending;
      std::map<PackageIdentity, std::vector<Constraint>, IdentityLess> constraints;
      std::map<PackageIdentity, Assigned, IdentityLess> assigned;
    };

    ResolverIssue issue(ResolverErrorCode code, const PackageIdentity &identity, std::string message,
                        const std::vector<Constraint> *constraints = nullptr)
    {
      ResolverIssue result;
      result.code = code;
      result.identity = identity_text(identity);
      result.message = std::move(message);
      if (constraints)
      {
        for (const auto &constraint : *constraints)
          result.constraints.push_back({constraint.requirement.alias, requirement_text(constraint.requirement), constraint.path});
      }
      return result;
    }

    ResolverIssue provider_issue(const PackageIdentity &identity, std::string message)
    {
      return issue(ResolverErrorCode::invalid_provider_metadata, identity, std::move(message));
    }

    bool contract_is_git(const PackageContract &contract)
    {
      return std::holds_alternative<GitPackageContractIdentity>(contract.identity);
    }

    bool validate_registry_candidate(const RegistryCandidate &candidate, const RegistryPackageIdentity &expected,
                                     ResolverIssue &failure)
    {
      const PackageIdentity identity{expected};
      if (!(candidate.identity == expected))
      {
        failure = provider_issue(identity, "metadata provider returned a candidate for a different registry identity");
        return false;
      }
      if (!valid_semver(candidate.version) || !valid_digest(candidate.contentDigest))
      {
        failure = provider_issue(identity, "registry candidate has an invalid version or content digest");
        return false;
      }
      if (!validate_package_contract(candidate.contract).valid())
      {
        failure = provider_issue(identity, "registry candidate has an invalid package contract");
        return false;
      }
      const auto *contractIdentity = std::get_if<RegistryPackageContractIdentity>(&candidate.contract.identity);
      if (!contractIdentity || !(contractIdentity->identity == candidate.identity) || !(contractIdentity->version == candidate.version))
      {
        failure = {ResolverErrorCode::package_contract_mismatch, identity_text(identity),
          "registry package contract identity/version does not match candidate metadata", {}};
        return false;
      }
      return true;
    }

    bool validate_git_resolution(const GitResolution &resolution, const std::string &repository,
                                 const GitRevisionRequirement &requested, ResolverIssue &failure)
    {
      const PackageIdentity identity{GitPackageIdentity{repository, true}};
      if (resolution.normalizedRepository != repository || !same_revision(resolution.requestedRevision, requested))
      {
        failure = provider_issue(identity, "Git metadata provider returned a result for a different repository or revision request");
        return false;
      }
      if (resolution.commit.size() != 40 && resolution.commit.size() != 64)
      {
        failure = provider_issue(identity, "Git resolution must contain a full 40- or 64-character commit");
        return false;
      }
      if (!std::all_of(resolution.commit.begin(), resolution.commit.end(), [](char c)
          { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) || !valid_digest(resolution.contentDigest))
      {
        failure = provider_issue(identity, "Git resolution has an invalid commit or content digest");
        return false;
      }
      if (!validate_package_contract(resolution.contract).valid())
      {
        failure = provider_issue(identity, "Git resolution has an invalid package contract");
        return false;
      }
      if (!contract_is_git(resolution.contract))
      {
        failure = {ResolverErrorCode::package_contract_mismatch, identity_text(identity),
          "Git package resolution requires a Git package contract", {}};
        return false;
      }
      return true;
    }

    bool registry_constraints_satisfied(const PackageIdentity &identity, const std::vector<Constraint> &constraints,
                                        const Assigned &assigned)
    {
      if (const auto *registry = std::get_if<ResolvedRegistryNode>(&assigned.node))
      {
        for (const auto &constraint : constraints)
        {
          const auto *requirement = std::get_if<RegistryRequirement>(&constraint.requirement.source);
          if (!requirement || !(requirement->identity == registry->identity) || !satisfies(registry->version, requirement->version)) return false;
        }
        return true;
      }
      (void)identity;
      return false;
    }

    struct Search
    {
      const ResolutionMetadataProvider &provider;
      std::optional<ResolverIssue> failure;
      std::size_t recursionDepth{0};

      explicit Search(const ResolutionMetadataProvider &metadataProvider) : provider(metadataProvider) {}

      std::vector<RegistryCandidate> registry_options(const RegistryPackageIdentity &identity,
                                                       const std::vector<Constraint> &constraints,
                                                       bool &providerHasNoCandidates)
      {
        auto candidates = provider.registry_candidates(identity);
        providerHasNoCandidates = candidates.empty();
        std::set<Version, decltype([](const Version &a, const Version &b) { return compare_versions(a, b) < 0; })> seen;
        for (const auto &candidate : candidates)
        {
          ResolverIssue invalid;
          if (!validate_registry_candidate(candidate, identity, invalid))
          {
            failure = std::move(invalid);
            return {};
          }
          if (!seen.insert(candidate.version).second)
          {
            failure = provider_issue(PackageIdentity{identity}, "metadata provider returned duplicate immutable Registry version metadata");
            return {};
          }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right)
        {
          const int comparison = compare_versions(left.version, right.version);
          if (comparison != 0) return comparison > 0;
          return left.contentDigest < right.contentDigest;
        });
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const auto &candidate)
        {
          for (const auto &constraint : constraints)
          {
            const auto *registry = std::get_if<RegistryRequirement>(&constraint.requirement.source);
            if (!registry || !(registry->identity == identity) || !satisfies(candidate.version, registry->version)) return true;
          }
          return false;
        }), candidates.end());
        return candidates;
      }

      std::optional<GitResolution> git_option(const std::string &repository, const std::vector<Constraint> &constraints)
      {
        std::optional<GitResolution> selected;
        for (const auto &constraint : constraints)
        {
          const auto *git = std::get_if<GitRequirement>(&constraint.requirement.source);
          if (!git) continue;
          auto resolutions = provider.git_resolutions(repository, git->revision);
          if (resolutions.empty())
          {
            failure = issue(ResolverErrorCode::git_revision_unresolved,
              PackageIdentity{GitPackageIdentity{repository, true}},
              "metadata provider has no immutable resolution for " + revision_text(git->revision), &constraints);
            return std::nullopt;
          }
          std::optional<GitResolution> forRequest;
          for (const auto &resolution : resolutions)
          {
            ResolverIssue invalid;
            if (!validate_git_resolution(resolution, repository, git->revision, invalid))
            {
              failure = std::move(invalid);
              return std::nullopt;
            }
            if (forRequest)
            {
              failure = provider_issue(PackageIdentity{GitPackageIdentity{repository, true}},
                forRequest->commit != resolution.commit || forRequest->contentDigest != resolution.contentDigest
                  ? "metadata provider returned conflicting immutable results for one Git revision"
                  : "metadata provider returned duplicate immutable metadata for one Git revision");
              return std::nullopt;
            }
            forRequest = resolution;
          }
          if (selected && selected->commit != forRequest->commit)
          {
            failure = issue(ResolverErrorCode::git_commit_conflict,
              PackageIdentity{GitPackageIdentity{repository, true}},
              "Git requirements for one repository resolve to different commits", &constraints);
            return std::nullopt;
          }
          if (selected && selected->contentDigest != forRequest->contentDigest)
          {
            failure = provider_issue(PackageIdentity{GitPackageIdentity{repository, true}},
              "metadata provider returned inconsistent content digests for the same Git commit");
            return std::nullopt;
          }
          selected = std::move(forRequest);
        }
        return selected;
      }

      bool search(SearchState state)
      {
        failure.reset();
        if (++recursionDepth > 4096)
        {
          failure = {ResolverErrorCode::resolution_depth_exceeded, "package graph",
            "resolution graph exceeds the supported recursive search depth", {}};
          --recursionDepth;
          return false;
        }
        while (!state.pending.empty())
        {
          auto pending = std::move(state.pending.front());
          state.pending.erase(state.pending.begin());
          const auto identity = package_identity(pending.requirement);
          auto &constraints = state.constraints[identity];
          constraints.push_back({pending.requirement, std::move(pending.path)});
          const auto assigned = state.assigned.find(identity);
          if (assigned != state.assigned.end())
          {
            bool satisfied = registry_constraints_satisfied(identity, constraints, assigned->second);
            if (const auto *gitNode = std::get_if<ResolvedGitNode>(&assigned->second.node))
            {
              const auto mapping = git_option(gitNode->normalizedRepository, constraints);
              if (failure || !mapping)
              {
                --recursionDepth;
                return false;
              }
              satisfied = mapping->commit == gitNode->commit && mapping->contentDigest == gitNode->contentDigest;
              if (!satisfied)
                failure = issue(ResolverErrorCode::git_commit_conflict, identity,
                  "Git requirement resolves to a different immutable commit", &constraints);
            }
            if (!satisfied)
            {
              if (!failure)
                failure = issue(ResolverErrorCode::constraint_conflict, identity,
                  "selected immutable candidate does not satisfy all accumulated requirements", &constraints);
              --recursionDepth;
              return false;
            }
          }
        }

        auto unresolved = state.constraints.end();
        for (auto it = state.constraints.begin(); it != state.constraints.end(); ++it)
          if (!state.assigned.contains(it->first)) { unresolved = it; break; }

        if (unresolved == state.constraints.end())
        {
          if (has_cycle(state.assigned))
          {
            --recursionDepth;
            return false;
          }
          finalState = std::move(state);
          --recursionDepth;
          return true;
        }

        const auto identity = unresolved->first;
        const auto constraints = unresolved->second;
        if (const auto *registryIdentity = std::get_if<RegistryPackageIdentity>(&identity))
        {
          bool providerHasNoCandidates = false;
          auto candidates = registry_options(*registryIdentity, constraints, providerHasNoCandidates);
          if (failure)
          {
            --recursionDepth;
            return false;
          }
          if (candidates.empty())
          {
            const auto code = providerHasNoCandidates
              ? ResolverErrorCode::no_candidate : ResolverErrorCode::constraint_conflict;
            failure = issue(code, identity, code == ResolverErrorCode::no_candidate
              ? "metadata provider has no candidates for this Registry identity"
              : "no available Registry version satisfies every accumulated constraint", &constraints);
            --recursionDepth;
            return false;
          }
          for (const auto &candidate : candidates)
          {
            SearchState branch = state;
            ResolvedRegistryNode node{candidate.identity, candidate.version, candidate.contentDigest, {}};
            for (const auto &dependency : candidate.contract.packages)
              node.dependencies.push_back({dependency.alias, package_identity(dependency)});
            std::sort(node.dependencies.begin(), node.dependencies.end(), [](const auto &left, const auto &right)
              { return left.alias < right.alias; });
            branch.assigned.emplace(identity, Assigned{ResolvedNode{std::move(node)}});
            for (const auto &dependency : candidate.contract.packages)
              branch.pending.push_back({from_contract(dependency), constraints.front().path + " -> " + identity_text(identity) + " (" + dependency.alias + ")"});
            if (search(std::move(branch))) { --recursionDepth; return true; }
            if (failure && failure->code == ResolverErrorCode::invalid_provider_metadata) { --recursionDepth; return false; }
          }
          --recursionDepth;
          return false;
        }

        const auto &gitIdentity = std::get<GitPackageIdentity>(identity);
        if (!gitIdentity.repositoryRoot)
        {
          failure = provider_issue(identity, "format 1 supports only repository-root Git packages");
          --recursionDepth;
          return false;
        }
        const auto resolution = git_option(gitIdentity.normalizedRepository, constraints);
        if (failure || !resolution)
        {
          --recursionDepth;
          return false;
        }
        ResolvedGitNode node{resolution->normalizedRepository, true, resolution->commit, resolution->contentDigest, {}};
        for (const auto &dependency : resolution->contract.packages)
          node.dependencies.push_back({dependency.alias, package_identity(dependency)});
        std::sort(node.dependencies.begin(), node.dependencies.end(), [](const auto &left, const auto &right)
          { return left.alias < right.alias; });
        SearchState branch = std::move(state);
        branch.assigned.emplace(identity, Assigned{ResolvedNode{std::move(node)}});
        for (const auto &dependency : resolution->contract.packages)
          branch.pending.push_back({from_contract(dependency), constraints.front().path + " -> " + identity_text(identity) + " (" + dependency.alias + ")"});
        const bool resolved = search(std::move(branch));
        --recursionDepth;
        return resolved;
      }

      bool has_cycle(const std::map<PackageIdentity, Assigned, IdentityLess> &assigned)
      {
        std::map<PackageIdentity, int, IdentityLess> marks;
        std::vector<PackageIdentity> stack;
        std::function<bool(const PackageIdentity &)> visit = [&](const PackageIdentity &identity)
        {
          const auto mark = marks[identity];
          if (mark == 1)
          {
            std::ostringstream message;
            message << "package dependency cycle:";
            const auto begin = std::find(stack.begin(), stack.end(), identity);
            for (auto it = begin; it != stack.end(); ++it) message << " " << identity_text(*it) << " ->";
            message << " " << identity_text(identity);
            failure = ResolverIssue{ResolverErrorCode::dependency_cycle, identity_text(identity), message.str(), {}};
            return true;
          }
          if (mark == 2) return false;
          marks[identity] = 1;
          stack.push_back(identity);
          const auto found = assigned.find(identity);
          if (found != assigned.end())
          {
            const bool foundCycle = std::visit([&](const auto &node)
            {
              for (const auto &dependency : node.dependencies)
                if (visit(dependency.identity)) return true;
              return false;
            }, found->second.node);
            if (foundCycle) return true;
          }
          stack.pop_back();
          marks[identity] = 2;
          return false;
        };
        for (const auto &[identity, _] : assigned)
          if (marks[identity] == 0 && visit(identity)) return true;
        return false;
      }

      SearchState finalState;
    };

    std::variant<NormalizedDeclaredRegistry, NormalizedDeclaredGit> normalized_declared(const PackageRequirement &requirement)
    {
      return std::visit([](const auto &source) -> std::variant<NormalizedDeclaredRegistry, NormalizedDeclaredGit>
      {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, RegistryRequirement>) return NormalizedDeclaredRegistry{source.identity, source.version};
        else return NormalizedDeclaredGit{source.normalizedRepository, source.repositoryRoot, source.revision};
      }, requirement.source);
    }

  } // namespace

  ResolverResult<ResolvedGraph> resolve_packages(const std::vector<PackageRequirement> &rootRequirements,
                                                  const ResolutionMetadataProvider &provider)
  {
    ResolverResult<ResolvedGraph> result;
    if (rootRequirements.empty())
    {
      result.value = ResolvedGraph{};
      return result;
    }
    NativeProject declarationCheck;
    declarationCheck.name = "resolver-input";
    declarationCheck.packages = rootRequirements;
    const auto validation = validate_native_project(declarationCheck);
    if (!validation.valid())
    {
      result.issues.push_back({ResolverErrorCode::invalid_requirement, "requirements",
        "resolver input contains invalid package declarations", {}});
      return result;
    }

    std::vector<PackageRequirement> orderedRoots = rootRequirements;
    std::sort(orderedRoots.begin(), orderedRoots.end(), [](const auto &left, const auto &right)
      { return left.alias < right.alias; });
    Search searcher{provider};
    SearchState initial;
    for (const auto &root : orderedRoots)
      initial.pending.push_back({root, "root:" + root.alias});

    if (!searcher.search(std::move(initial)))
    {
      if (searcher.failure) result.issues.push_back(std::move(*searcher.failure));
      else result.issues.push_back({ResolverErrorCode::constraint_conflict, "package graph",
        "no consistent single-version package graph satisfies all requirements", {}});
      return result;
    }

    ResolvedGraph graph;
    for (const auto &root : orderedRoots)
      graph.roots.push_back({root.alias, normalized_declared(root), package_identity(root)});
    for (const auto &[identity, assigned] : searcher.finalState.assigned)
    {
      (void)identity;
      graph.nodes.push_back(assigned.node);
    }
    std::sort(graph.nodes.begin(), graph.nodes.end(), [](const auto &left, const auto &right)
    {
      const auto leftIdentity = std::visit([](const auto &node) -> PackageIdentity
      {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, ResolvedRegistryNode>) return node.identity;
        else return GitPackageIdentity{node.normalizedRepository, node.repositoryRoot};
      }, left);
      const auto rightIdentity = std::visit([](const auto &node) -> PackageIdentity
      {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, ResolvedRegistryNode>) return node.identity;
        else return GitPackageIdentity{node.normalizedRepository, node.repositoryRoot};
      }, right);
      return IdentityLess{}(leftIdentity, rightIdentity);
    });
    result.value = std::move(graph);
    return result;
  }

  ResolverResult<Lockfile> make_resolved_lockfile(const ResolvedGraph &graph)
  {
    ResolverResult<Lockfile> result;
    if (graph.roots.empty())
    {
      result.issues.push_back({ResolverErrorCode::no_root_requirements, "roots",
        "format-1 does not produce a persisted lockfile for an empty root requirement set", {}});
      return result;
    }
    Lockfile lock;
    std::map<PackageIdentity, std::string, IdentityLess> nodeIds;
    for (const auto &node : graph.nodes)
    {
      std::visit([&](const auto &resolved)
      {
        using Node = std::decay_t<decltype(resolved)>;
        if constexpr (std::is_same_v<Node, ResolvedRegistryNode>)
        {
          const auto id = registry_node_id(RegistryNodeIdentity{resolved.identity, resolved.version});
          nodeIds.emplace(PackageIdentity{resolved.identity}, id);
          lock.nodes.emplace_back(RegistryLockNode{id, resolved.identity, resolved.version, resolved.contentDigest, {}});
        }
        else
        {
          const auto id = git_node_id(GitNodeIdentity{resolved.normalizedRepository, resolved.commit});
          nodeIds.emplace(PackageIdentity{GitPackageIdentity{resolved.normalizedRepository, resolved.repositoryRoot}}, id);
          lock.nodes.emplace_back(GitLockNode{id, resolved.normalizedRepository, resolved.repositoryRoot, resolved.commit, resolved.contentDigest, {}});
        }
      }, node);
    }
    for (std::size_t index = 0; index < graph.nodes.size(); ++index)
    {
      const auto &resolved = graph.nodes[index];
      auto &lockNode = lock.nodes[index];
      const auto dependencies = std::visit([](const auto &node) { return node.dependencies; }, resolved);
      for (const auto &dependency : dependencies)
      {
        const auto found = nodeIds.find(dependency.identity);
        if (found == nodeIds.end())
        {
          result.issues.push_back({ResolverErrorCode::invalid_provider_metadata, identity_text(dependency.identity),
            "resolved graph dependency refers to a node that is absent from the graph", {}});
          return result;
        }
        std::visit([&](auto &node) { node.dependencies.push_back({dependency.alias, found->second}); }, lockNode);
      }
    }
    for (const auto &root : graph.roots)
    {
      const auto found = nodeIds.find(root.identity);
      if (found == nodeIds.end())
      {
        result.issues.push_back({ResolverErrorCode::invalid_provider_metadata, identity_text(root.identity),
          "resolved graph root refers to a node that is absent from the graph", {}});
        return result;
      }
      lock.roots.push_back({root.alias, root.declared, found->second});
    }
    std::vector<RootRequirementDigestEntry> digestEntries;
    digestEntries.reserve(graph.roots.size());
    for (const auto &root : graph.roots)
      digestEntries.push_back({root.alias, std::visit([](const auto &declared)
        -> std::variant<NormalizedDeclaredRegistry, NormalizedRootGitRequirement>
      {
        using Declared = std::decay_t<decltype(declared)>;
        if constexpr (std::is_same_v<Declared, NormalizedDeclaredRegistry>) return declared;
        else return NormalizedRootGitRequirement{declared.normalizedRepository, declared.revision};
      }, root.declared)});
    lock.rootRequirementsDigest = root_requirements_digest(digestEntries);

    const auto semantic = validate_lockfile(lock);
    if (!semantic.valid())
    {
      result.issues.push_back({ResolverErrorCode::invalid_provider_metadata, "resolved graph",
        "resolved graph cannot be represented as a valid format-1 lockfile", {}});
      return result;
    }
    const auto integrity = validate_lockfile_integrity(lock);
    if (!integrity.valid())
    {
      result.issues.push_back({ResolverErrorCode::invalid_provider_metadata, "resolved graph",
        "resolved graph produced a lockfile that failed structural integrity validation", {}});
      return result;
    }
    result.value = std::move(lock);
    return result;
  }
} // namespace vix::cli::project::native
