#include <vix/cli/project/native/GitPackageContentSource.hpp>
#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>
#include <vix/cli/project/native/PackageContentAcquisition.hpp>
#include <vix/cli/project/native/PackageContentDigest.hpp>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  class TempRoot
  {
  public:
    TempRoot()
    {
      path_ = fs::temp_directory_path() / ("vix-p10qb-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      fs::create_directories(path_);
    }
    ~TempRoot() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path &path() const { return path_; }
  private:
    fs::path path_;
  };

  GitCommandResult run(GitCommandRunner &runner, std::vector<std::string> args,
    std::string input = {})
  {
    GitCommand command{std::move(args), std::move(input)};
    auto result = runner.run(command);
    if (!result.started || result.exitCode != 0)
    {
      std::cerr << "Real Git fixture command failed\nargv:";
      for (const auto &argument : command.arguments)
        std::cerr << " [" << argument << ']';
      std::cerr << "\nstarted: " << (result.started ? "true" : "false")
        << "\nexitCode: " << result.exitCode
        << "\nstderr: " << result.standardError << '\n';
      if (std::find(command.arguments.begin(), command.arguments.end(), "mktree") != command.arguments.end())
      {
        constexpr char hex[] = "0123456789ABCDEF";
        std::cerr << "stdin byte count: " << command.standardInput.size() << "\nstdin: \"";
        for (const unsigned char byte : command.standardInput)
        {
          switch (byte)
          {
            case '\n': std::cerr << "\\n"; break;
            case '\r': std::cerr << "\\r"; break;
            case '\t': std::cerr << "\\t"; break;
            case '\0': std::cerr << "\\0"; break;
            case '\\': std::cerr << "\\\\"; break;
            case '"': std::cerr << "\\\""; break;
            default:
              if (byte >= 0x20 && byte <= 0x7e) std::cerr << static_cast<char>(byte);
              else std::cerr << "\\x" << hex[byte >> 4] << hex[byte & 0x0f];
          }
        }
        std::cerr << "\"\n";
      }
      std::abort();
    }
    return result;
  }

  std::string trim_line(std::string value)
  {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
  }

  CanonicalPackageEntry package_file(std::string path, std::string_view bytes, bool executable = false)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    entry.kind = CanonicalPackageEntryKind::file;
    entry.executable = executable;
    for (const unsigned char byte : bytes) entry.content.push_back(static_cast<std::byte>(byte));
    return entry;
  }

  CanonicalPackageTree package_tree(std::string_view packageContract)
  {
    CanonicalPackageEntry link;
    link.path = "link.hpp";
    link.kind = CanonicalPackageEntryKind::symlink;
    link.target = "include/demo.hpp";
    return {{package_file("vix.package.toml", packageContract),
      package_file("include/demo.hpp", "#pragma once\n"),
      package_file("src/demo.cpp", "int demo() { return 1; }\n"),
      package_file("script.sh", "echo hi\n", true),
      std::move(link), package_file("binary.dat", std::string_view("\0\xff\n", 3)),
      package_file("line.txt", "a\r\n"), package_file("docs/my file.txt", "docs content\r\n")}};
  }

  Lockfile git_lock(std::string repository, std::string commit, const PackageContentId &content)
  {
    const auto nodeId = git_node_id(GitNodeIdentity{repository, commit});
    const GitRevisionRequirement revision{CommitRevision{commit}};
    const NormalizedDeclaredGit declared{repository, true, revision};
    Lockfile lock;
    lock.roots.push_back({"locked", declared, nodeId});
    lock.nodes.emplace_back(GitLockNode{nodeId, repository, true, commit, content.digest(), {}});
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{
      "locked", NormalizedRootGitRequirement{repository, revision}}});
    return lock;
  }

  fs::path persistent_object_path(const fs::path &root, const PackageContentId &id)
  {
    const auto hex = id.digest().substr(7);
    return root / "objects" / "sha256" / hex.substr(0, 2) / hex.substr(2);
  }

  std::string read_bytes(const fs::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  class NoRegistrySource final : public RegistryPackageContentSource
  {
  public:
    RegistryPackageContentSourceResult acquire(const RegistryContentLocation &,
      const RegistryPackageContentRequest &) override
    {
      ++calls;
      return {};
    }
    std::size_t calls{0};
  };

  class CountingFailGitRunner final : public GitCommandRunner
  {
  public:
    GitCommandResult run(const GitCommand &) override
    {
      ++calls;
      return {};
    }
    std::size_t calls{0};
  };

  constexpr const char *locked_commit = "cccccccccccccccccccccccccccccccccccccccc";
  constexpr const char *blob_oid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

  struct FakeGitScript
  {
    int targetedFetchExit{0};
    int fallbackFetchExit{0};
    bool failToStart{false};
    std::vector<std::string> commitTypes{"commit"};
    std::string lsTree;
    std::string batch;
    std::vector<GitCommand> commands;
    std::size_t typeIndex{0};
    std::size_t batchCalls{0};
    std::size_t targetedFetchCalls{0};
    std::size_t fallbackFetchCalls{0};
  };

  class FakeGitRunner final : public GitCommandRunner
  {
  public:
    explicit FakeGitRunner(FakeGitScript &script) : script_(script) {}
    GitCommandResult run(const GitCommand &command) override
    {
      script_.commands.push_back(command);
      if (script_.failToStart) return {};
      const auto has = [&](std::string_view argument)
      { return std::find(command.arguments.begin(), command.arguments.end(), argument) != command.arguments.end(); };
      if (has("init")) return {true, 0, {}, {}};
      if (has("fetch"))
      {
        if (has("+refs/*:refs/vix-acquisition/*"))
        { ++script_.fallbackFetchCalls; return {true, script_.fallbackFetchExit, {}, "fetch failed"}; }
        ++script_.targetedFetchCalls;
        return {true, script_.targetedFetchExit, {}, "targeted fetch failed"};
      }
      if (has("-t"))
      {
        const auto type = script_.typeIndex < script_.commitTypes.size()
          ? script_.commitTypes[script_.typeIndex++] : std::string{};
        return {true, type.empty() ? 1 : 0, type.empty() ? std::string{} : type + "\n", {}};
      }
      if (has("ls-tree")) return {true, 0, script_.lsTree, {}};
      if (has("--batch"))
      { ++script_.batchCalls; return {true, 0, script_.batch, {}}; }
      return {true, 91, {}, "unexpected fake Git command"};
    }
  private:
    FakeGitScript &script_;
  };

  std::string tree_record(std::string mode, std::string type, std::string oid, std::string path)
  { return std::move(mode) + " " + std::move(type) + " " + std::move(oid) + "\t" + std::move(path) + '\0'; }

  std::string blob_record(std::string_view oid, std::string_view type, std::string_view data)
  { return std::string(oid) + " " + std::string(type) + " " + std::to_string(data.size()) + "\n" +
      std::string(data) + "\n"; }

  struct FixtureTreeEntry
  {
    std::string path;
    std::string mode;
    std::string objectId;
  };

  std::string build_fixture_tree(GitCommandRunner &runner, const std::string &gitDir,
    const std::vector<FixtureTreeEntry> &entries)
  {
    std::map<std::string, FixtureTreeEntry> leaves;
    std::map<std::string, std::vector<FixtureTreeEntry>> directories;
    for (const auto &entry : entries)
    {
      const auto slash = entry.path.find('/');
      if (slash == std::string::npos)
      {
        leaves.emplace(entry.path, entry);
        continue;
      }
      const auto directory = entry.path.substr(0, slash);
      directories[directory].push_back({entry.path.substr(slash + 1), entry.mode, entry.objectId});
    }

    std::map<std::string, std::string> serializedEntries;
    for (const auto &[name, entry] : leaves)
    {
      const auto type = entry.mode == "160000" ? "commit" : "blob";
      serializedEntries.emplace(name,
        entry.mode + " " + type + " " + entry.objectId + "\t" + name + "\n");
    }
    for (const auto &[name, children] : directories)
    {
      const auto childTreeOid = build_fixture_tree(runner, gitDir, children);
      serializedEntries.emplace(name,
        "040000 tree " + childTreeOid + "\t" + name + "\n");
    }
    std::string treeInput;
    for (const auto &[name, serializedEntry] : serializedEntries)
    {
      (void)name;
      treeInput += serializedEntry;
    }
    const auto treeResult = run(runner, {"git", "--git-dir", gitDir, "mktree"},
      std::move(treeInput));
    return trim_line(treeResult.standardOutput);
  }

  GitPackageContentSourceResult acquire_fake(FakeGitScript &script, const fs::path &workRoot,
    std::string repository = "https://example.invalid/repo")
  {
    FakeGitRunner runner(script);
    GitCliPackageContentSource source(workRoot, runner);
    const auto expected = PackageContentId::parse("sha256:" + std::string(64, '0'));
    assert(expected);
    return source.acquire({std::move(repository), true, locked_commit, *expected});
  }

  bool has_command(const FakeGitScript &script, std::string_view name)
  {
    return std::any_of(script.commands.begin(), script.commands.end(), [&](const auto &command)
    { return std::find(command.arguments.begin(), command.arguments.end(), name) != command.arguments.end(); });
  }

  void require_command_condition(bool condition, std::string_view expected,
    const FakeGitScript &script)
  {
    if (condition) return;
    std::cerr << "Git command expectation failed: " << expected << "\nrecorded argv:\n";
    for (const auto &command : script.commands)
    {
      std::cerr << "  [";
      for (std::size_t index = 0; index < command.arguments.size(); ++index)
      {
        if (index) std::cerr << "][";
        std::cerr << command.arguments[index];
      }
      std::cerr << "]\n";
    }
    std::abort();
  }

  void parser_and_tree_contract_tests(const fs::path &workRoot)
  {
    {
      FakeGitScript script;
      script.lsTree = tree_record("160000", "commit", blob_oid, "submodule");
      const auto result = acquire_fake(script, workRoot / "gitlink");
      assert(result.status == PackageContentSourceStatus::available && !result.candidate);
      assert(result.diagnostic.find("submodule/gitlink") != std::string::npos);
      assert(script.batchCalls == 0);
    }
    {
      FakeGitScript script;
      script.lsTree = tree_record("100644", "blob", blob_oid, "tab\tinside") +
        tree_record("100644", "blob", blob_oid, "line\nbreak with space") +
        tree_record("100644", "blob", blob_oid, "caf\xc3\xa9.hpp");
      const std::string binary{"\0\n\r\n\xff", 5};
      script.batch = blob_record(blob_oid, "blob", binary);
      script.batch += blob_record(blob_oid, "blob", binary);
      script.batch += blob_record(blob_oid, "blob", binary);
      const auto result = acquire_fake(script, workRoot / "special-paths");
      assert(result.status == PackageContentSourceStatus::available && result.candidate);
      const auto &entries = result.candidate->tree().entries;
      assert(entries.size() == 3);
      assert(entries[0].path == "tab\tinside");
      assert(entries[1].path == "line\nbreak with space");
      assert(entries[2].path == "caf\xc3\xa9.hpp");
      assert(entries[0].content.size() == binary.size());
      assert(entries[0].content[0] == std::byte{0});
      assert(entries[0].content[1] == std::byte{'\n'});
      assert(entries[0].content[4] == std::byte{0xff});
    }
    {
      FakeGitScript script;
      script.lsTree = tree_record("100644", "blob", blob_oid, std::string("bad\xffpath", 8));
      const auto result = acquire_fake(script, workRoot / "invalid-utf8");
      assert(!result.candidate && script.batchCalls == 0);
      assert(result.diagnostic.find("UTF-8") != std::string::npos);
    }
    {
      FakeGitScript script;
      const auto result = acquire_fake(script, workRoot / "empty-tree");
      assert(result.status == PackageContentSourceStatus::available && result.candidate);
      assert(result.candidate->tree().entries.empty() && script.batchCalls == 0);
    }
    {
      FakeGitScript script;
      script.lsTree = tree_record("100644", "blob", blob_oid, "plain") +
        tree_record("100755", "blob", blob_oid, "executable");
      const std::string bytes{"x\0\xff\r\n", 5};
      script.batch = blob_record(blob_oid, "blob", bytes) + blob_record(blob_oid, "blob", bytes);
      const auto result = acquire_fake(script, workRoot / "modes");
      assert(result.candidate && result.candidate->tree().entries.size() == 2);
      const auto &entries = result.candidate->tree().entries;
      assert(!entries[0].executable && entries[1].executable);
    }
  }

  void parser_failure_tests(const fs::path &workRoot)
  {
    const std::vector<std::string> malformedTrees{
      "100644 blob " + std::string(blob_oid) + " no-tab\0",
      "100644 " + std::string(blob_oid) + "\tmissing-type\0",
      tree_record("100664", "blob", blob_oid, "bad-mode"),
      tree_record("100644", "blob", "", "missing-oid"),
      std::string("100644 blob ") + blob_oid + "\tunterminated",
      tree_record("100644", "blob", blob_oid, "a//b"),
    };
    for (std::size_t index = 0; index < malformedTrees.size(); ++index)
    {
      FakeGitScript script; script.lsTree = malformedTrees[index];
      const auto result = acquire_fake(script, workRoot / ("malformed-tree-" + std::to_string(index)));
      assert(!result.candidate && script.batchCalls == 0);
      assert(!result.diagnostic.empty());
    }
    {
      FakeGitScript script;
      script.lsTree = tree_record("100644", "blob", blob_oid, "file");
      script.batch = std::string(blob_oid) + " blob 5\nabc";
      const auto result = acquire_fake(script, workRoot / "truncated-batch");
      assert(!result.candidate && result.status == PackageContentSourceStatus::available);
      assert(result.diagnostic.find("cat-file batch protocol") != std::string::npos);
    }
    for (const auto type : {"tree", "commit", "tag"})
    {
      FakeGitScript script;
      script.lsTree = tree_record("100644", "blob", blob_oid, "file");
      script.batch = blob_record(blob_oid, type, "data");
      const auto result = acquire_fake(script, workRoot / (std::string("wrong-type-") + type));
      assert(!result.candidate);
      assert(result.diagnostic.find("cat-file batch protocol") != std::string::npos);
    }
    {
      FakeGitScript script;
      script.lsTree = tree_record("100644", "blob", blob_oid, "file");
      script.batch = std::string(blob_oid) + " missing\n";
      const auto result = acquire_fake(script, workRoot / "missing-blob");
      assert(!result.candidate);
      assert(result.diagnostic.find("cat-file batch protocol") != std::string::npos);
    }
  }

  void fetch_state_and_command_tests(const fs::path &workRoot)
  {
    {
      FakeGitScript script; script.failToStart = true;
      const auto result = acquire_fake(script, workRoot / "git-start-failure");
      assert(!result.candidate && result.status == PackageContentSourceStatus::transport_failure);
    }
    {
      FakeGitScript script; script.lsTree.clear();
      const auto result = acquire_fake(script, workRoot / "target-success");
      assert(result.candidate && script.targetedFetchCalls == 1 && script.fallbackFetchCalls == 0);
      const auto fetch = std::find_if(script.commands.begin(), script.commands.end(), [](const auto &command)
      { return std::find(command.arguments.begin(), command.arguments.end(), "fetch") != command.arguments.end(); });
      assert(fetch != script.commands.end());
      assert(std::find(fetch->arguments.begin(), fetch->arguments.end(), locked_commit) != fetch->arguments.end());
    }
    {
      FakeGitScript script; script.commitTypes = {"", "commit"}; script.targetedFetchExit = 1;
      const auto result = acquire_fake(script, workRoot / "fallback-success");
      assert(result.candidate && script.targetedFetchCalls == 1 && script.fallbackFetchCalls == 1);
      const auto fallbackFetch = std::find_if(script.commands.begin(), script.commands.end(), [](const auto &command)
      {
        return std::find(command.arguments.begin(), command.arguments.end(), "fetch") != command.arguments.end() &&
          !command.arguments.empty() && command.arguments.back().find("refs/vix-acquisition/") != std::string::npos;
      });
      require_command_condition(fallbackFetch != script.commands.end(),
        "fallback fetch destinations use the private acquisition namespace", script);
      const auto &refspec = fallbackFetch->arguments.back();
      const auto colon = refspec.find(':');
      require_command_condition(refspec.size() > 1 && refspec.front() == '+' && colon != std::string::npos &&
        refspec.substr(0, colon) == "+refs/*" &&
        refspec.substr(colon + 1) == "refs/vix-acquisition/*",
        "fallback fetches advertised refs into refs/vix-acquisition/*", script);
      const auto typeQuery = std::find_if(script.commands.begin(), script.commands.end(), [](const auto &command)
      { return std::find(command.arguments.begin(), command.arguments.end(), "-t") != command.arguments.end(); });
      require_command_condition(typeQuery != script.commands.end() &&
        typeQuery->arguments.back() == locked_commit,
        "post-fallback commit type query uses the exact locked OID", script);
      const auto treeQuery = std::find_if(script.commands.begin(), script.commands.end(), [](const auto &command)
      { return std::find(command.arguments.begin(), command.arguments.end(), "ls-tree") != command.arguments.end(); });
      require_command_condition(treeQuery != script.commands.end() &&
        treeQuery->arguments.back() == locked_commit,
        "tree extraction uses the exact locked OID, not a fetched ref", script);
    }
    {
      FakeGitScript script; script.commitTypes = {"", ""};
      const auto result = acquire_fake(script, workRoot / "fallback-missing");
      assert(!result.candidate && script.fallbackFetchCalls == 1);
      assert(result.diagnostic.find("exact locked Git commit is unavailable") != std::string::npos);
    }
    {
      FakeGitScript script; script.commitTypes = {"", ""};
      script.targetedFetchExit = 1; script.fallbackFetchExit = 1;
      const auto result = acquire_fake(script, workRoot / "transport-failure");
      assert(!result.candidate && result.status == PackageContentSourceStatus::transport_failure);
    }
    for (const auto type : {"blob", "tree", "tag"})
    {
      FakeGitScript script; script.commitTypes = {type};
      const auto result = acquire_fake(script, workRoot / (std::string("locked-not-commit-") + type));
      assert(!result.candidate && script.fallbackFetchCalls == 0);
    }
    {
      FakeGitScript script; script.lsTree.clear();
      const auto result = acquire_fake(script, workRoot / "argv-safety", "repo path;$(not-a-shell)");
      assert(result.candidate);
      for (const auto &command : script.commands)
      {
        assert(std::find(command.arguments.begin(), command.arguments.end(), "sh") == command.arguments.end());
        assert(std::find(command.arguments.begin(), command.arguments.end(), "-c") == command.arguments.end());
        assert(std::find(command.arguments.begin(), command.arguments.end(), "checkout") == command.arguments.end());
        assert(std::find(command.arguments.begin(), command.arguments.end(), "switch") == command.arguments.end());
        assert(std::find(command.arguments.begin(), command.arguments.end(), "worktree") == command.arguments.end());
      }
      assert(has_command(script, "repo path;$(not-a-shell)"));
    }
    {
      FakeGitScript script;
      std::string listing, batch;
      constexpr std::string_view hex = "0123456789abcdef";
      for (unsigned index = 0; index < 128; ++index)
      {
        std::string suffix;
        suffix.push_back(hex[(index >> 4) & 15]);
        suffix.push_back(hex[index & 15]);
        const auto oid = std::string(38, 'a') + suffix;
        listing += tree_record("100644", "blob", oid, "file-" + std::to_string(index));
        batch += blob_record(oid, "blob", "x");
      }
      script.lsTree = std::move(listing); script.batch = std::move(batch);
      const auto result = acquire_fake(script, workRoot / "batch-scale");
      assert(result.candidate && result.candidate->tree().entries.size() == 128);
      assert(script.batchCalls == 1);
    }
  }

  void exact_commit_object_test()
  {
    TempRoot root;
    NativeGitCommandRunner runner;
    const auto remote = (root.path() / "remote.git").string();
    run(runner, {"git", "init", "--bare", "--quiet", remote});

    const std::string contractA = "format = 1\nname = \"demo\"\n";
    const std::string contractB = "format = 1\nname = \"dEmo\"\n";
    const auto packageTreeA = package_tree(contractA);
    const auto packageTreeB = package_tree(contractB);
    const auto canonicalA = canonicalize_package_content_tree(packageTreeA);
    const auto canonicalB = canonicalize_package_content_tree(packageTreeB);
    assert(canonicalA.valid() && canonicalB.valid());
    assert(canonicalA.contentId != canonicalB.contentId);

    const std::string lockedBytes{"docs content\r\n", 14};
    const std::string binaryBytes{"\0\xff\n", 3};
    const auto blobA = trim_line(run(runner,
      {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, lockedBytes).standardOutput);
    const auto contractBlobA = trim_line(run(runner,
      {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, contractA).standardOutput);
    const auto contractBlobB = trim_line(run(runner,
      {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, contractB).standardOutput);
    const auto rootTreeOidA = build_fixture_tree(runner, remote, {
      {"vix.package.toml", "100644", contractBlobA},
      {"include/demo.hpp", "100644", trim_line(run(runner,
        {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "#pragma once\n").standardOutput)},
      {"src/demo.cpp", "100644", trim_line(run(runner,
        {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "int demo() { return 1; }\n").standardOutput)},
      {"script.sh", "100755", trim_line(run(runner,
        {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "echo hi\n").standardOutput)},
      {"link.hpp", "120000", trim_line(run(runner,
        {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "include/demo.hpp").standardOutput)},
      {"binary.dat", "100644", trim_line(run(runner,
        {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, binaryBytes).standardOutput)},
      {"line.txt", "100644", trim_line(run(runner,
        {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "a\r\n").standardOutput)},
      {"docs/my file.txt", "100644", blobA}});
    const std::string identity = "Vix Test <vix@example.invalid> 1700000000 +0000\n";
    const auto commitA = trim_line(run(runner,
      {"git", "--git-dir", remote, "hash-object", "-t", "commit", "-w", "--stdin"},
      "tree " + rootTreeOidA + "\nauthor " + identity + "committer " + identity + "\nlocked commit\n").standardOutput);

    const auto repositoryLock = git_lock(remote, commitA, *canonicalA.contentId);
    const auto lockBefore = serialize_lockfile(repositoryLock);
    assert(lockBefore.valid());
    assert(std::get<GitLockNode>(repositoryLock.nodes.front()).commit == commitA);

    // The lock is already fixed to A before the advertised branch and tag move to B.
    const auto rootTreeOidB = build_fixture_tree(runner, remote,
      {{"vix.package.toml", "100644", contractBlobB},
       {"include/demo.hpp", "100644", trim_line(run(runner,
         {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "#pragma once\n").standardOutput)},
       {"src/demo.cpp", "100644", trim_line(run(runner,
         {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "int demo() { return 1; }\n").standardOutput)},
       {"script.sh", "100755", trim_line(run(runner,
         {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "echo hi\n").standardOutput)},
       {"link.hpp", "120000", trim_line(run(runner,
         {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "include/demo.hpp").standardOutput)},
       {"binary.dat", "100644", trim_line(run(runner,
         {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, binaryBytes).standardOutput)},
       {"line.txt", "100644", trim_line(run(runner,
         {"git", "--git-dir", remote, "hash-object", "-w", "--stdin"}, "a\r\n").standardOutput)},
       {"docs/my file.txt", "100644", blobA}});
    const auto commitB = trim_line(run(runner,
      {"git", "--git-dir", remote, "hash-object", "-t", "commit", "-w", "--stdin"},
      "tree " + rootTreeOidB + "\nparent " + commitA + "\nauthor " + identity +
      "committer " + identity + "\nmoved ref\n").standardOutput);
    run(runner, {"git", "--git-dir", remote, "update-ref", "refs/heads/main", commitB});
    run(runner, {"git", "--git-dir", remote, "update-ref", "refs/tags/v1", commitB});

    {
      GitCliPackageContentSource sourceForB(root.path() / "work-commit-b", runner);
      const auto commitBContent = sourceForB.acquire({remote, true, commitB, *canonicalB.contentId});
      assert(commitBContent.status == PackageContentSourceStatus::available && commitBContent.candidate);
      const auto actualB = canonicalize_package_content_tree(commitBContent.candidate->tree());
      assert(actualB.valid() && actualB.contentId == canonicalB.contentId);
    }

    const auto objectRoot = root.path() / "content-store";
    const auto emptyRegistryDirectory = build_registry_directory({});
    assert(emptyRegistryDirectory.valid());
    {
      GitCliPackageContentSource source(root.path() / "work-success", runner);
      FilesystemPackageContentStore store(objectRoot);
      NoRegistrySource registry;
      const auto result = acquire_locked_package_content(repositoryLock, AcquisitionMode::online, *emptyRegistryDirectory.value,
        registry, source, store);
      assert(result.valid() && result.entries.size() == 1);
      assert(result.entries.front().state == PackageContentAcquisitionState::acquired);
      assert(registry.calls == 0);
      assert(store.inspect(*canonicalA.contentId).value == PackageContentAvailability::available);
      assert(read_bytes(persistent_object_path(objectRoot, *canonicalA.contentId)) == *canonicalA.canonicalBytes);
      assert(serialize_lockfile(repositoryLock).text == lockBefore.text);
      const auto stored = parse_canonical_package_content(read_bytes(
        persistent_object_path(objectRoot, *canonicalA.contentId)));
      assert(stored.valid());
      const auto find = [&](const std::string &path) -> const CanonicalPackageEntry &
      {
        for (const auto &entry : stored.tree->entries) if (entry.path == path) return entry;
        assert(false);
        return stored.tree->entries.front();
      };
      assert(find("vix.package.toml").content.size() == contractA.size());
      assert(find("script.sh").executable);
      assert(find("link.hpp").kind == CanonicalPackageEntryKind::symlink);
      assert(find("link.hpp").target == "include/demo.hpp");
      assert(find("binary.dat").content.size() == binaryBytes.size());
      assert(find("binary.dat").content[0] == std::byte{0});
      assert(find("binary.dat").content[1] == std::byte{0xff});
      assert(find("binary.dat").content[2] == std::byte{'\n'});
      assert(find("line.txt").content.size() == 3);
      assert(find("line.txt").content[1] == std::byte{'\r'});
      assert(find("line.txt").content[2] == std::byte{'\n'});
      assert(find("docs/my file.txt").content.size() == lockedBytes.size());
    }
    {
      CountingFailGitRunner forbiddenGit;
      GitCliPackageContentSource source(root.path() / "work-reuse", forbiddenGit);
      FilesystemPackageContentStore reopenedStore(objectRoot);
      NoRegistrySource registry;
      const auto reused = acquire_locked_package_content(repositoryLock, AcquisitionMode::online, *emptyRegistryDirectory.value,
        registry, source, reopenedStore);
      assert(reused.valid() && reused.entries.size() == 1);
      assert(reused.entries.front().state == PackageContentAcquisitionState::reused);
      assert(forbiddenGit.calls == 0 && registry.calls == 0);
      assert(serialize_lockfile(repositoryLock).text == lockBefore.text);
    }
    {
      CountingFailGitRunner forbiddenGit;
      GitCliPackageContentSource source(root.path() / "work-offline", forbiddenGit);
      FilesystemPackageContentStore emptyStore(objectRoot / "offline-miss");
      NoRegistrySource registry;
      const auto before = serialize_lockfile(repositoryLock);
      const auto offlineMiss = acquire_locked_package_content(repositoryLock, AcquisitionMode::offline,
        *emptyRegistryDirectory.value, registry, source, emptyStore);
      assert(!offlineMiss.valid() && offlineMiss.issues.front().code ==
        PackageContentAcquisitionIssueCode::offline_package_content_unavailable);
      assert(forbiddenGit.calls == 0 && registry.calls == 0);
      assert(serialize_lockfile(repositoryLock).text == before.text);
    }
    {
      const auto mismatchLock = git_lock(remote, commitA, *canonicalB.contentId);
      const auto mismatchBefore = serialize_lockfile(mismatchLock);
      assert(mismatchBefore.valid());
      FilesystemPackageContentStore mismatchStore(root.path() / "content-store-mismatch");
      GitCliPackageContentSource source(root.path() / "work-mismatch", runner);
      NoRegistrySource registry;
      const auto mismatch = acquire_locked_package_content(mismatchLock, AcquisitionMode::online, *emptyRegistryDirectory.value,
        registry, source, mismatchStore);
      assert(!mismatch.valid() && mismatch.entries.empty());
      assert(mismatch.issues.front().code == PackageContentAcquisitionIssueCode::package_content_digest_mismatch);
      assert(mismatchStore.inspect(*canonicalA.contentId).value == PackageContentAvailability::missing);
      assert(mismatchStore.inspect(*canonicalB.contentId).value == PackageContentAvailability::missing);
      assert(serialize_lockfile(mismatchLock).text == mismatchBefore.text);
    }
  }
}

int main()
{
  exact_commit_object_test();
  TempRoot root;
  parser_and_tree_contract_tests(root.path());
  parser_failure_tests(root.path());
  fetch_state_and_command_tests(root.path());
  return 0;
}
