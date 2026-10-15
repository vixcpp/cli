/** @file GitPackageMetadataSource.hpp
 *  Exact revision Git metadata observation without checkout or P10P admission.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_METADATA_SOURCE_HPP
#define VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_METADATA_SOURCE_HPP

#include <filesystem>
#include <utility>

#include <vix/cli/project/native/GitPackageContentSource.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>

namespace vix::cli::project::native
{
  class GitPackageMetadataSource final
  {
  public:
    GitPackageMetadataSource(std::filesystem::path workRoot, GitCommandRunner &runner)
      : workRoot_(std::move(workRoot)), runner_(runner) {}

    [[nodiscard]] GitQueryResult resolve_git(
      const GitPackageIdentity &identity, const GitRevisionRequirement &revision);

  private:
    std::filesystem::path workRoot_;
    GitCommandRunner &runner_;
  };
}

#endif
