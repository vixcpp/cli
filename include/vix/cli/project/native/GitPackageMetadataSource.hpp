/** @file GitPackageMetadataSource.hpp
 *  Exact revision Git metadata observation without checkout or P10P admission.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_METADATA_SOURCE_HPP
#define VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_METADATA_SOURCE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <vix/cli/project/native/GitPackageContentSource.hpp>
#include <vix/cli/project/native/MetadataAcquisition.hpp>

namespace vix::cli::project::native
{
  struct GitDefaultBranchResult
  {
    std::optional<std::string> branch;
    std::string diagnostic;
    [[nodiscard]] bool valid() const noexcept { return branch.has_value() && diagnostic.empty(); }
  };

  class GitDefaultBranchSource
  {
  public:
    virtual ~GitDefaultBranchSource() = default;
    [[nodiscard]] virtual GitDefaultBranchResult discover_default_branch(
      const GitPackageIdentity &identity) = 0;
  };

  class GitPackageMetadataSource final : public GitDefaultBranchSource
  {
  public:
    GitPackageMetadataSource(std::filesystem::path workRoot, GitCommandRunner &runner)
      : workRoot_(std::move(workRoot)), runner_(runner) {}

    [[nodiscard]] GitQueryResult resolve_git(
      const GitPackageIdentity &identity, const GitRevisionRequirement &revision);
    [[nodiscard]] GitDefaultBranchResult discover_default_branch(
      const GitPackageIdentity &identity) override;

  private:
    std::filesystem::path workRoot_;
    GitCommandRunner &runner_;
  };
}

#endif
