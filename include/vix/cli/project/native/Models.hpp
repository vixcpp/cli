/** @file Models.hpp
 *  Parser-independent semantic values for native projects and packages.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_MODELS_HPP
#define VIX_CLI_PROJECT_NATIVE_MODELS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vix::cli::project::native
{
  enum class CxxStandard { cxx20, cxx23 };
  enum class UsageVisibility { private_, public_, interface_ };
  enum class TargetKind { executable, static_library, shared_library, interface_library };
  enum class PackageSourceKind { registry, git };

  struct Version
  {
    std::uint64_t major{0};
    std::uint64_t minor{0};
    std::uint64_t patch{0};
    std::optional<std::string> prerelease;
    friend bool operator==(const Version &, const Version &) = default;
  };

  struct ExactVersion { Version version; };
  struct CompatibleVersion { Version version; };
  using VersionRequirement = std::variant<ExactVersion, CompatibleVersion>;

  struct CommitRevision { std::string value; };
  struct TagRevision { std::string value; };
  struct BranchRevision { std::string value; };
  using GitRevisionRequirement = std::variant<CommitRevision, TagRevision, BranchRevision>;

  struct RegistryPackageIdentity
  {
    std::string registry;
    std::string publisher;
    std::string name;
    friend bool operator==(const RegistryPackageIdentity &, const RegistryPackageIdentity &) = default;
  };

  struct GitPackageIdentity
  {
    std::string normalizedRepository;
    // Format 1 supports only the repository root. This marker is semantic,
    // not a user-selectable path.
    bool repositoryRoot{true};
    friend bool operator==(const GitPackageIdentity &, const GitPackageIdentity &) = default;
  };

  using PackageIdentity = std::variant<RegistryPackageIdentity, GitPackageIdentity>;

  struct RegistryRequirement
  {
    RegistryPackageIdentity identity;
    VersionRequirement version;
  };

  struct GitRequirement
  {
    std::string normalizedRepository;
    bool repositoryRoot{true};
    GitRevisionRequirement revision;
  };

  struct PackageRequirement
  {
    std::string alias;
    std::variant<RegistryRequirement, GitRequirement> source;
  };

  struct IncludeRequirement
  {
    UsageVisibility visibility{UsageVisibility::private_};
    std::vector<std::string> paths;
  };

  struct CompileDefinitions
  {
    UsageVisibility visibility{UsageVisibility::private_};
    std::vector<std::string> values;
  };

  struct InternalTargetUse
  {
    UsageVisibility visibility{UsageVisibility::private_};
    std::string target;
  };

  struct PackageTargetUse
  {
    UsageVisibility visibility{UsageVisibility::private_};
    std::string requirementAlias;
    std::string exportName;
  };

  struct NativeTarget
  {
    std::string name;
    TargetKind kind{TargetKind::executable};
    // Interface targets canonically have no sources.
    std::vector<std::string> sources;
    std::optional<CxxStandard> cxxStandard;
    std::vector<IncludeRequirement> includes;
    std::vector<CompileDefinitions> definitions;
    std::vector<InternalTargetUse> uses;
    std::vector<PackageTargetUse> packageUses;
  };

  struct TestRegistration
  {
    std::string name;
    std::string target;
  };

  struct NativeProject
  {
    std::int64_t format{1};
    std::string name;
    std::optional<CxxStandard> cxxStandard;
    std::vector<NativeTarget> targets;
    std::vector<TestRegistration> tests;
    std::vector<PackageRequirement> packages;
  };

  struct RegistryPackageContractIdentity
  {
    RegistryPackageIdentity identity;
    Version version;
  };

  struct GitPackageContractIdentity
  {
    // Git identity is supplied by verified lock/source context. The contract
    // asserts only its source kind; it does not repeat a repository locator.
  };

  using PackageContractIdentity = std::variant<RegistryPackageContractIdentity, GitPackageContractIdentity>;

  struct NativeVixIntegration { std::vector<std::string> includeRoots; };
  struct CMakeConfigIntegration { std::string configName; };
  using PackageIntegration = std::variant<NativeVixIntegration, CMakeConfigIntegration>;

  struct NativeExportMapping { std::string projectTarget; };
  struct CMakeExportMapping { std::string cmakeTarget; };
  struct PackageExport
  {
    std::string name;
    std::variant<NativeExportMapping, CMakeExportMapping> mapping;
  };

  struct PackageContract
  {
    std::int64_t format{1};
    PackageContractIdentity identity;
    std::vector<PackageRequirement> packages;
    PackageIntegration integration;
    std::vector<PackageExport> exports;
  };

  struct NormalizedDeclaredRegistry
  {
    RegistryPackageIdentity identity;
    VersionRequirement requirement;
  };
  struct NormalizedDeclaredGit
  {
    std::string normalizedRepository;
    bool repositoryRoot{true};
    GitRevisionRequirement revision;
  };
  struct LockRoot
  {
    std::string alias;
    std::variant<NormalizedDeclaredRegistry, NormalizedDeclaredGit> declared;
    std::string node;
  };

  struct LockDependency
  {
    std::string alias;
    std::string node;
  };
  struct RegistryLockNode
  {
    std::string id;
    RegistryPackageIdentity identity;
    Version version;
    std::string contentDigest;
    std::vector<LockDependency> dependencies;
  };
  struct GitLockNode
  {
    std::string id;
    std::string normalizedRepository;
    bool repositoryRoot{true};
    std::string commit;
    std::string contentDigest;
    std::vector<LockDependency> dependencies;
  };
  using LockNode = std::variant<RegistryLockNode, GitLockNode>;

  struct Lockfile
  {
    std::int64_t format{1};
    std::string rootRequirementsDigest;
    std::vector<LockRoot> roots;
    std::vector<LockNode> nodes;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_MODELS_HPP
