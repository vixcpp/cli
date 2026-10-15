#include <vix/cli/project/native/GitPackageMetadataSource.hpp>
#include <vix/cli/project/native/OnlinePackageMetadataSource.hpp>
#include <vix/cli/project/native/PackageContentDigest.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  constexpr auto commitOid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr auto contractOid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  constexpr auto headerOid = "cccccccccccccccccccccccccccccccccccccccc";
  const std::string contractText = R"toml(format = 1
[package]
source = "git"
[integration]
type = "native-vix"
include-roots = ["include"]
[[exports]]
name = "api"
project-target = "api"
[[packages]]
alias = "dep"
source = "registry"
registry = "vix"
publisher = "example"
name = "dep"
requirement = { compatible = "1.0.0" }
)toml";

  bool has_argument(const GitCommand &command, const std::string &argument);

  class ScriptedGitRunner final : public GitCommandRunner
  {
  public:
    std::vector<GitCommand> commands;
    bool commitObject{true};
    bool includeContract{true};
    bool includeHeader{true};
    bool missingCommit{false};
    bool missingRef{false};
    bool nonCommitRef{false};
    bool includeSymlink{false};
    bool unsafeSymlink{false};
    bool includeGitlink{false};
    std::string headerMode{"100644"};
    std::string headerContent{"int package_api();\n"};
    std::string contract{contractText};
    std::string symbolicHead{"ref: refs/heads/main\tHEAD\n"};
    bool advertiseSymbolicHead{true};

    GitCommandResult run(const GitCommand &command) override
    {
      commands.push_back(command);
      if (has_argument(command, "ls-remote"))
        return advertiseSymbolicHead ? GitCommandResult{true, 0, symbolicHead, {}} :
          GitCommandResult{true, 0, std::string{commitOid} + "\tHEAD\n", {}};
      if (has_argument(command, "check-ref-format")) return {true, 0, {}, {}};
      if (has_argument(command, "init")) return {true, 0, {}, {}};
      if (has_argument(command, "fetch"))
        return missingRef ? GitCommandResult{true, 1, {}, "fatal: couldn't find remote ref"}
          : GitCommandResult{true, 0, {}, {}};
      if (has_argument(command, "cat-file") && has_argument(command, "-t"))
      {
        if (missingCommit) return {true, 1, {}, "missing"};
        return commitObject ? GitCommandResult{true, 0, "commit\n", {}}
          : GitCommandResult{true, 0, "blob\n", {}};
      }
      if (has_argument(command, "rev-parse"))
        return nonCommitRef ? GitCommandResult{true, 1, {}, "not a commit"}
          : GitCommandResult{true, 0, std::string{commitOid} + "\n", {}};
      if (has_argument(command, "ls-tree"))
      {
        std::string listing;
        if (includeContract)
        { listing += "100644 blob " + std::string{contractOid} + "\tvix.package.toml"; listing.push_back('\0'); }
        if (includeHeader)
        { listing += headerMode + " blob " + std::string{headerOid} + "\tinclude/api.hpp"; listing.push_back('\0'); }
        if (includeSymlink)
        { listing += "120000 blob " + std::string{std::string(40, 'd')} + "\tlink.hpp"; listing.push_back('\0'); }
        if (includeGitlink)
        { listing += "160000 commit " + std::string{std::string(40, 'e')} + "\tsubmodule"; listing.push_back('\0'); }
        return {true, 0, std::move(listing), {}};
      }
      if (has_argument(command, "cat-file") && has_argument(command, "--batch"))
      {
        std::string response;
        std::size_t position = 0;
        while (position < command.standardInput.size())
        {
          const auto end = command.standardInput.find('\n', position);
          if (end == std::string::npos) break;
          const auto oid = command.standardInput.substr(position, end - position);
          const auto content = oid == contractOid ? contract : oid == std::string(40, 'd')
            ? (unsafeSymlink ? std::string{"../../outside"} : std::string{"include/api.hpp"}) : headerContent;
          response += oid + " blob " + std::to_string(content.size()) + "\n";
          response += content;
          response.push_back('\n');
          position = end + 1;
        }
        return {true, 0, std::move(response), {}};
      }
      return {true, 0, {}, {}};
    }
  };

  class RegistryMetadataFixture final : public PackageMetadataSource
  {
  public:
    int registryCalls{0};
    int gitCalls{0};
    RegistryQueryResult registry_releases(const RegistryPackageIdentity &) override
    { ++registryCalls; return {true, {}, {}}; }
    GitQueryResult resolve_git(const GitPackageIdentity &, const GitRevisionRequirement &) override
    { ++gitCalls; return {GitQueryStatus::failed, std::nullopt, "unexpected direct Git route"}; }
  };

  bool has_argument(const GitCommand &command, const std::string &argument)
  {
    return std::find(command.arguments.begin(), command.arguments.end(), argument) != command.arguments.end();
  }

  void exact_commit_branch_and_tag_tests()
  {
    const auto workRoot = std::filesystem::temp_directory_path() / "vix-git-metadata-fixture";
    ScriptedGitRunner runner;
    GitPackageMetadataSource source{workRoot, runner};
    const GitPackageIdentity identity{"https://example.invalid/repository.git", true};

    const auto commit = source.resolve_git(identity, CommitRevision{commitOid});
    assert(commit.status == GitQueryStatus::found && commit.resolution);
    assert(commit.resolution->commit == commitOid);
    assert(std::get<CommitRevision>(commit.resolution->requestedRevision).value == commitOid);
    assert(commit.resolution->contract.packages.size() == 1);

    runner.commands.clear();
    const auto branch = source.resolve_git(identity, BranchRevision{"release/1.x"});
    assert(branch.status == GitQueryStatus::found && branch.resolution);
    assert(std::any_of(runner.commands.begin(), runner.commands.end(), [](const auto &command)
    { return has_argument(command, "+refs/heads/release/1.x:refs/vix-metadata/heads/release/1.x"); }));

    runner.commands.clear();
    const auto tag = source.resolve_git(identity, TagRevision{"v1.2.3"});
    assert(tag.status == GitQueryStatus::found && tag.resolution);
    assert(std::any_of(runner.commands.begin(), runner.commands.end(), [](const auto &command)
    { return has_argument(command, "+refs/tags/v1.2.3:refs/vix-metadata/tags/v1.2.3"); }));
    assert(std::any_of(runner.commands.begin(), runner.commands.end(), [](const auto &command)
    { return std::any_of(command.arguments.begin(), command.arguments.end(), [](const auto &arg)
      { return arg.ends_with("^{commit}"); }); }));
    runner.missingRef = true;
    const auto missingTag = source.resolve_git(identity, TagRevision{"absent"});
    assert(missingTag.status == GitQueryStatus::not_found && !missingTag.resolution);
    runner.missingRef = false;
    runner.nonCommitRef = true;
    const auto tagToNonCommit = source.resolve_git(identity, TagRevision{"blob-tag"});
    assert(tagToNonCommit.status == GitQueryStatus::failed && !tagToNonCommit.resolution);
    runner.nonCommitRef = false;
    std::error_code error;
    std::filesystem::remove_all(workRoot, error);
  }

  void failures_and_digest_tests()
  {
    const auto workRoot = std::filesystem::temp_directory_path() / "vix-git-metadata-failures";
    ScriptedGitRunner runner;
    GitPackageMetadataSource source{workRoot, runner};
    const GitPackageIdentity identity{"https://example.invalid/repository.git", true};

    auto result = source.resolve_git(identity, BranchRevision{"main"});
    assert(result.status == GitQueryStatus::found && result.resolution);
    CanonicalPackageTree expectedTree;
    expectedTree.entries.push_back({"vix.package.toml", CanonicalPackageEntryKind::file, false,
      std::vector<std::byte>{}, {}});
    for (const char ch : contractText) expectedTree.entries.front().content.push_back(static_cast<std::byte>(ch));
    expectedTree.entries.push_back({"include/api.hpp", CanonicalPackageEntryKind::file, false,
      {}, {}});
    const std::string header{"int package_api();\n"};
    for (const char ch : header) expectedTree.entries.back().content.push_back(static_cast<std::byte>(ch));
    const auto expected = canonicalize_package_content_tree(expectedTree);
    assert(expected.valid() && result.resolution->contentDigest == expected.contentId->digest());

    runner.commitObject = false;
    const auto notCommit = source.resolve_git(identity, CommitRevision{commitOid});
    assert(notCommit.status == GitQueryStatus::failed && !notCommit.resolution);
    runner.commitObject = true;
    runner.missingCommit = true;
    const auto missingCommit = source.resolve_git(identity, CommitRevision{commitOid});
    assert(missingCommit.status == GitQueryStatus::not_found && !missingCommit.resolution);
    runner.missingCommit = false;
    runner.includeContract = false;
    const auto missingContract = source.resolve_git(identity, CommitRevision{commitOid});
    assert(missingContract.status == GitQueryStatus::failed && !missingContract.resolution);
    runner.includeContract = true;
    runner.contract = "not a package contract";
    const auto malformedContract = source.resolve_git(identity, CommitRevision{commitOid});
    assert(malformedContract.status == GitQueryStatus::failed && !malformedContract.resolution);
    runner.contract = contractText;
    runner.headerContent += "// changed\n";
    const auto changedContent = source.resolve_git(identity, CommitRevision{commitOid});
    assert(changedContent.status == GitQueryStatus::found && changedContent.resolution &&
      changedContent.resolution->contentDigest != expected.contentId->digest());
    runner.headerContent = "int package_api();\n";
    runner.headerMode = "100755";
    const auto executable = source.resolve_git(identity, CommitRevision{commitOid});
    assert(executable.status == GitQueryStatus::found && executable.resolution &&
      executable.resolution->contentDigest != expected.contentId->digest());
    runner.headerMode = "100644";
    runner.includeSymlink = true;
    const auto safeLink = source.resolve_git(identity, CommitRevision{commitOid});
    assert(safeLink.status == GitQueryStatus::found && safeLink.resolution);
    runner.unsafeSymlink = true;
    const auto unsafeLink = source.resolve_git(identity, CommitRevision{commitOid});
    assert(unsafeLink.status == GitQueryStatus::failed && !unsafeLink.resolution);
    runner.unsafeSymlink = false;
    runner.includeSymlink = false;
    runner.includeGitlink = true;
    const auto gitlink = source.resolve_git(identity, CommitRevision{commitOid});
    assert(gitlink.status == GitQueryStatus::failed && !gitlink.resolution);
    const auto nonRoot = source.resolve_git(GitPackageIdentity{identity.normalizedRepository, false},
      CommitRevision{commitOid});
    assert(nonRoot.status == GitQueryStatus::failed && !nonRoot.resolution);
    std::error_code error;
    std::filesystem::remove_all(workRoot, error);
  }

  void default_branch_discovery_tests()
  {
    ScriptedGitRunner runner;
    GitPackageMetadataSource source{std::filesystem::path{"unused"}, runner};
    const GitPackageIdentity identity{"https://example.invalid/org/repo.git", true};
    auto discovered = source.discover_default_branch(identity);
    assert(discovered.valid() && *discovered.branch == "main");
    assert(runner.commands.size() == 2);
    assert(has_argument(runner.commands.front(), "ls-remote"));
    assert(has_argument(runner.commands.front(), identity.normalizedRepository));
    assert(has_argument(runner.commands.front(), "HEAD"));

    runner.commands.clear();
    runner.symbolicHead = "ref: refs/heads/trunk\tHEAD\n";
    discovered = source.discover_default_branch(identity);
    assert(discovered.valid() && *discovered.branch == "trunk");

    runner.commands.clear();
    runner.symbolicHead = "ref: refs/tags/v1\tHEAD\n";
    discovered = source.discover_default_branch(identity);
    assert(!discovered.valid());

    runner.commands.clear();
    runner.advertiseSymbolicHead = false;
    discovered = source.discover_default_branch(identity);
    assert(!discovered.valid());
    assert(runner.commands.size() == 1);
  }

  void online_composition_routes_typed_sources()
  {
    const auto workRoot = std::filesystem::temp_directory_path() / "vix-git-metadata-composition";
    ScriptedGitRunner runner;
    GitPackageMetadataSource git{workRoot, runner};
    RegistryMetadataFixture registry;
    OnlinePackageMetadataSource online{registry, git};
    const auto catalog = acquire_resolution_catalog({PackageRequirement{"root",
      GitRequirement{"https://example.invalid/repository.git", true, CommitRevision{commitOid}}}}, online);
    assert(catalog.valid() && catalog.catalog);
    assert(registry.registryCalls == 1 && registry.gitCalls == 0);
    assert(!runner.commands.empty());
    std::error_code error;
    std::filesystem::remove_all(workRoot, error);
  }
}

int main()
{
  exact_commit_branch_and_tag_tests();
  failures_and_digest_tests();
  default_branch_discovery_tests();
  online_composition_routes_typed_sources();
}
