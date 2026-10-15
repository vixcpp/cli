#include <vix/cli/project/native/GitPackageMetadataSource.hpp>

#include <vix/cli/project/native/PackageContentDigest.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    bool valid_full_oid(std::string_view oid)
    {
      return (oid.size() == 40 || oid.size() == 64) &&
        std::all_of(oid.begin(), oid.end(), [](char ch)
        { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
    }

    std::string trim_line(std::string value)
    {
      while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
      return value;
    }

    GitCommandResult run(GitCommandRunner &runner, std::vector<std::string> arguments)
    { return runner.run({std::move(arguments), {}}); }

    bool valid_revision_name(GitCommandRunner &runner, std::string_view namespaceName,
      std::string_view value)
    {
      if (value.empty() || value.find_first_of("\r\n") != std::string_view::npos ||
          value.find('\0') != std::string_view::npos) return false;
      const auto checked = run(runner, {"git", "check-ref-format", std::string{namespaceName} + std::string{value}});
      return checked.started && checked.exitCode == 0;
    }

    GitQueryResult failed(std::string message)
    { return {GitQueryStatus::failed, std::nullopt, std::move(message)}; }

    GitQueryResult missing(std::string message)
    { return {GitQueryStatus::not_found, std::nullopt, std::move(message)}; }

    std::optional<std::string> package_contract_bytes(const CanonicalPackageTree &tree)
    {
      const auto entry = std::find_if(tree.entries.begin(), tree.entries.end(), [](const auto &value)
        { return value.path == "vix.package.toml"; });
      if (entry == tree.entries.end() || entry->kind != CanonicalPackageEntryKind::file) return std::nullopt;
      std::string bytes;
      bytes.reserve(entry->content.size());
      for (const auto byte : entry->content) bytes.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
      return bytes;
    }
  }

  GitQueryResult GitPackageMetadataSource::resolve_git(
    const GitPackageIdentity &identity, const GitRevisionRequirement &revision)
  {
    if (!identity.repositoryRoot || identity.normalizedRepository.empty() ||
        identity.normalizedRepository.find_first_of("\r\n") != std::string::npos ||
        identity.normalizedRepository.find('\0') != std::string::npos)
      return failed("Git metadata request has an invalid repository identity or unsupported non-root package identity");

    std::string fullCommit;
    std::string sourceRef;
    std::string destinationRef;
    bool commitRequest = false;
    const bool revisionValid = std::visit([&](const auto &value)
    {
      using Revision = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<Revision, CommitRevision>)
      {
        commitRequest = true;
        fullCommit = value.value;
        return valid_full_oid(value.value);
      }
      else if constexpr (std::is_same_v<Revision, BranchRevision>)
      {
        sourceRef = "refs/heads/" + value.value;
        destinationRef = "refs/vix-metadata/heads/" + value.value;
        return valid_revision_name(runner_, "refs/heads/", value.value);
      }
      else
      {
        sourceRef = "refs/tags/" + value.value;
        destinationRef = "refs/vix-metadata/tags/" + value.value;
        return valid_revision_name(runner_, "refs/tags/", value.value);
      }
    }, revision);
    if (!revisionValid)
      return failed("Git revision must be a full commit ID or a valid exact branch/tag name");

    std::error_code ec;
    fs::create_directories(workRoot_, ec);
    if (ec) return failed("cannot create injected Git metadata work root");
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path workspace = workRoot_ / ("vix-git-metadata-" + std::to_string(stamp) + "-" +
      std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
    if (!fs::create_directory(workspace, ec) || ec)
      return failed("cannot create isolated Git metadata workspace");
    struct Cleanup
    {
      fs::path path;
      ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{workspace};
    const auto gitDir = (workspace / "objects.git").string();
    const auto initialized = run(runner_, {"git", "init", "--bare", "--quiet", gitDir});
    if (!initialized.started || initialized.exitCode != 0)
      return failed("could not initialize isolated Git metadata object database");

    if (commitRequest)
    {
      auto fetched = runner_.run({{"git", "--git-dir", gitDir, "fetch", "--no-tags", "--depth=1", "--",
        identity.normalizedRepository, fullCommit}, {}});
      if (!fetched.started) return failed("Git executable unavailable during exact-commit metadata fetch");
      auto type = run(runner_, {"git", "--git-dir", gitDir, "cat-file", "-t", fullCommit});
      if (!type.started || type.exitCode != 0)
      {
        fetched = runner_.run({{"git", "--git-dir", gitDir, "fetch", "--no-tags", "--depth=1", "--",
          identity.normalizedRepository, "+refs/*:refs/vix-metadata/all/*"}, {}});
        if (!fetched.started || fetched.exitCode != 0)
          return missing("the requested full Git commit is unavailable from the configured repository");
        type = run(runner_, {"git", "--git-dir", gitDir, "cat-file", "-t", fullCommit});
      }
      if (!type.started || type.exitCode != 0)
        return missing("the requested full Git commit is absent from the configured repository");
      if (trim_line(std::move(type.standardOutput)) != "commit")
        return failed("the requested full Git object is not a commit");
    }
    else
    {
      const auto refspec = "+" + sourceRef + ":" + destinationRef;
      const auto fetched = runner_.run({{"git", "--git-dir", gitDir, "fetch", "--no-tags", "--depth=1", "--",
        identity.normalizedRepository, refspec}, {}});
      if (!fetched.started) return failed("Git executable unavailable while resolving the requested ref");
      if (fetched.exitCode != 0)
      {
        const auto &diagnostic = fetched.standardError;
        if (diagnostic.find("couldn't find remote ref") != std::string::npos ||
            diagnostic.find("no such ref") != std::string::npos)
          return missing("the requested exact Git ref is not present in the configured repository");
        return failed("Git transport failed while fetching the requested exact ref");
      }
      const auto peel = run(runner_, {"git", "--git-dir", gitDir, "rev-parse", "--verify",
        destinationRef + "^{commit}"});
      if (!peel.started || peel.exitCode != 0)
        return failed("the requested Git ref does not resolve to a commit");
      fullCommit = trim_line(peel.standardOutput);
      if (!valid_full_oid(fullCommit)) return failed("Git returned a malformed full commit ID");
    }

    const auto objectTree = read_git_object_tree(runner_, gitDir, fullCommit);
    if (objectTree.status != GitObjectTreeReadStatus::available || !objectTree.tree)
      return failed(objectTree.diagnostic.empty() ? "could not inspect exact Git commit objects" : objectTree.diagnostic);
    const auto contractBytes = package_contract_bytes(*objectTree.tree);
    if (!contractBytes) return failed("exact Git commit is missing a regular vix.package.toml at repository root");
    const auto parsedContract = parse_package_contract_text(*contractBytes, "Git vix.package.toml");
    if (!parsedContract.valid() || !parsedContract.value || !validate_package_contract(*parsedContract.value).valid())
      return failed("exact Git vix.package.toml is malformed or semantically invalid");
    if (!std::holds_alternative<GitPackageContractIdentity>(parsedContract.value->identity))
      return failed("exact Git vix.package.toml declares a non-Git package identity");
    const auto canonical = canonicalize_package_content_tree(*objectTree.tree);
    if (!canonical.valid() || !canonical.contentId)
      return failed("exact Git package tree cannot be represented by the P10O canonical contract");

    return {GitQueryStatus::found,
      GitResolution{identity.normalizedRepository, revision, std::move(fullCommit),
        canonical.contentId->digest(), *parsedContract.value}, {}};
  }

  GitDefaultBranchResult GitPackageMetadataSource::discover_default_branch(
    const GitPackageIdentity &identity)
  {
    if (!identity.repositoryRoot || identity.normalizedRepository.empty() ||
        identity.normalizedRepository.find_first_of("\r\n") != std::string::npos ||
        identity.normalizedRepository.find('\0') != std::string::npos)
      return {std::nullopt, "Git default-branch request has an invalid repository identity"};

    const auto result = run(runner_, {"git", "ls-remote", "--symref",
      identity.normalizedRepository, "HEAD"});
    if (!result.started || result.exitCode != 0)
      return {std::nullopt, "could not discover the Git repository default branch"};

    std::optional<std::string> branch;
    std::size_t offset = 0;
    while (offset < result.standardOutput.size())
    {
      const auto end = result.standardOutput.find('\n', offset);
      auto line = std::string_view{result.standardOutput}.substr(offset,
        end == std::string::npos ? result.standardOutput.size() - offset : end - offset);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      offset = end == std::string::npos ? result.standardOutput.size() : end + 1;
      if (!line.starts_with("ref: ")) continue;
      const auto tab = line.find('\t');
      if (tab == std::string_view::npos || line.substr(tab + 1) != "HEAD" || branch)
        return {std::nullopt, "Git repository returned malformed symbolic HEAD data"};
      const auto ref = line.substr(5, tab - 5);
      constexpr std::string_view prefix{"refs/heads/"};
      if (!ref.starts_with(prefix) || ref.size() == prefix.size())
        return {std::nullopt, "Git symbolic HEAD does not identify a branch"};
      const auto name = ref.substr(prefix.size());
      if (!valid_revision_name(runner_, prefix, name))
        return {std::nullopt, "Git symbolic HEAD contains an invalid branch name"};
      branch = std::string{name};
    }
    if (!branch)
      return {std::nullopt, "Git repository did not advertise a symbolic default branch"};
    return {std::move(branch), {}};
  }
}
