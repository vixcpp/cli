/** @file OnlinePackageMetadataSource.hpp
 *  Typed production composition for online Registry and Git metadata.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_ONLINE_PACKAGE_METADATA_SOURCE_HPP
#define VIX_CLI_PROJECT_NATIVE_ONLINE_PACKAGE_METADATA_SOURCE_HPP

#include <vix/cli/project/native/GitPackageMetadataSource.hpp>
#include <vix/cli/project/native/RegistryMetadataSource.hpp>

namespace vix::cli::project::native
{
  class OnlinePackageMetadataSource final : public PackageMetadataSource
  {
  public:
    OnlinePackageMetadataSource(PackageMetadataSource &registry,
      GitPackageMetadataSource &git) : registry_(registry), git_(git) {}

    [[nodiscard]] RegistryQueryResult registry_releases(
      const RegistryPackageIdentity &identity) override
    { return registry_.registry_releases(identity); }

    [[nodiscard]] GitQueryResult resolve_git(const GitPackageIdentity &identity,
      const GitRevisionRequirement &revision) override
    { return git_.resolve_git(identity, revision); }

  private:
    PackageMetadataSource &registry_;
    GitPackageMetadataSource &git_;
  };
}

#endif
