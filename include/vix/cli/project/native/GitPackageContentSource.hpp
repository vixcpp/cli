/** @file GitPackageContentSource.hpp
 *  Exact-commit Git object acquisition (P10Q-B).
 */
#ifndef VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_CONTENT_SOURCE_HPP
#define VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_CONTENT_SOURCE_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/project/native/PackageContentAcquisition.hpp>

namespace vix::cli::project::native
{
  struct GitCommand
  {
    std::vector<std::string> arguments;
    std::string standardInput;
  };

  struct GitCommandResult
  {
    bool started{false};
    int exitCode{127};
    std::string standardOutput;
    std::string standardError;
  };

  class GitCommandRunner
  {
  public:
    virtual ~GitCommandRunner() = default;
    [[nodiscard]] virtual GitCommandResult run(const GitCommand &command) = 0;
  };

  enum class GitObjectTreeReadStatus { available, transport_failure, invalid_object_data };
  struct GitObjectTreeReadResult
  {
    GitObjectTreeReadStatus status{GitObjectTreeReadStatus::transport_failure};
    std::optional<CanonicalPackageTree> tree;
    std::string diagnostic;
  };

  // Shared object-database reader used by content and metadata acquisition.
  // It reads one exact commit tree without creating a worktree or admitting
  // bytes to the persistent package-content store.
  [[nodiscard]] GitObjectTreeReadResult read_git_object_tree(
    GitCommandRunner &runner, std::string_view gitDirectory, std::string_view fullCommit);

  class NativeGitCommandRunner final : public GitCommandRunner
  {
  public:
    [[nodiscard]] GitCommandResult run(const GitCommand &command) override;
  };

  // Receives an injected work root; each call uses a private disposable bare
  // object database. The returned candidate remains untrusted until P10O.
  class GitCliPackageContentSource final : public GitPackageContentSource
  {
  public:
    GitCliPackageContentSource(std::filesystem::path workRoot, GitCommandRunner &runner);
    [[nodiscard]] GitPackageContentSourceResult acquire(
      const GitPackageContentRequest &request) override;
  private:
    std::filesystem::path workRoot_;
    GitCommandRunner &runner_;
  };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_CONTENT_SOURCE_HPP
