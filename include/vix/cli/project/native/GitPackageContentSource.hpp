/** @file GitPackageContentSource.hpp
 *  Exact-commit Git object acquisition (P10Q-B).
 */
#ifndef VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_CONTENT_SOURCE_HPP
#define VIX_CLI_PROJECT_NATIVE_GIT_PACKAGE_CONTENT_SOURCE_HPP

#include <filesystem>
#include <memory>
#include <string>
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
