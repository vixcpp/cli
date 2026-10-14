#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>
#include <vix/cli/project/native/PackageContentAcquisition.hpp>
#include <vix/cli/project/native/RegistryContentLayout.hpp>
#include <vix/cli/project/native/RegistryContentTransport.hpp>
#include <vix/requests/Url.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  static_assert(!std::is_same_v<RegistryLocation, RegistryContentLocation>);
  static_assert(!std::is_convertible_v<RegistryLocation, RegistryContentLocation>);
  static_assert(!std::is_convertible_v<RegistryContentLocation, RegistryLocation>);
  static_assert(std::is_same_v<decltype(&RegistryPackageContentSource::acquire),
    RegistryPackageContentSourceResult (RegistryPackageContentSource::*)(
      const RegistryContentLocation &, const RegistryPackageContentRequest &)>);
  static_assert(std::is_same_v<decltype(&registry_format1_content_object_path),
    std::string (*)(const PackageContentId &)>);

  class TestRoot
  {
  public:
    TestRoot()
    {
      path_ = fs::temp_directory_path() / ("vix-p10qa-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      std::error_code ec;
      fs::remove_all(path_, ec);
    }
    ~TestRoot() { std::error_code ec; fs::remove_all(path_, ec); }
    [[nodiscard]] const fs::path &path() const noexcept { return path_; }
  private:
    fs::path path_;
  };

  std::string file_location(const fs::path &root)
  {
    const auto pathBytes = fs::absolute(root).lexically_normal().generic_u8string();
    const std::string path{reinterpret_cast<const char *>(pathBytes.data()), pathBytes.size()};
#ifdef _WIN32
    return "file:///" + path;
#else
    return "file://" + path;
#endif
  }

  fs::path mirror_object_path(const fs::path &root, const PackageContentId &id)
  {
    const auto relative = registry_format1_content_object_path(id);
    return root / fs::path(relative);
  }

  void write_bytes(const fs::path &path, std::string_view bytes)
  {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
  }

  std::string read_bytes(const fs::path &path)
  {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  class RecordingTransport final : public RegistryContentTransport
  {
  public:
    explicit RecordingTransport(RegistryContentTransport &inner) : inner_(inner) {}
    std::vector<std::string> calls;
    RegistryContentTransportResult fetch(const RegistryContentLocation &location,
      std::string_view path, std::uint64_t maximumBytes) override
    {
      calls.push_back(location.descriptor + "|" + std::string{path});
      return inner_.fetch(location, path, maximumBytes);
    }
  private:
    RegistryContentTransport &inner_;
  };

  class CountingTransport final : public RegistryContentTransport
  {
  public:
    std::size_t calls{0};
    RegistryContentTransportResult fetch(const RegistryContentLocation &,
      std::string_view, std::uint64_t) override
    {
      ++calls;
      return {RegistryContentTransportStatus::transport_failure, std::nullopt, "unexpected transport invocation"};
    }
  };

  class ScriptedHttpsExecutor final : public RegistryHttpsRequestExecutor
  {
  public:
    struct Reply
    {
      bool completed{true};
      RegistryHttpsResponseHead head;
      std::string bytes;
      std::string diagnostic;
      std::size_t chunkSize{3};
    };
    std::vector<std::string> urls;
    std::vector<Reply> replies;
    std::size_t next{0};
    std::size_t bodyChunksDelivered{0};

    RegistryHttpsExecutionResult get(std::string_view url, const HeadSink &headSink,
      const BodySink &bodySink) override
    {
      urls.emplace_back(url);
      if (next >= replies.size())
        return {false, "unexpected HTTPS request"};
      const auto reply = replies[next++];
      if (reply.completed)
      {
        headSink(reply.head);
        for (std::size_t offset = 0; offset < reply.bytes.size(); offset += reply.chunkSize)
        {
          const auto count = std::min(reply.chunkSize, reply.bytes.size() - offset);
          ++bodyChunksDelivered;
          bodySink(std::span<const std::byte>{
            reinterpret_cast<const std::byte *>(reply.bytes.data() + offset), count});
        }
      }
      return {reply.completed, reply.diagnostic};
    }
  };

  ScriptedHttpsExecutor::Reply https_reply(int status, std::string bytes = {},
    std::optional<std::string> location = std::nullopt,
    std::optional<std::uint64_t> contentLength = std::nullopt)
  {
    ScriptedHttpsExecutor::Reply reply;
    reply.head = {status, contentLength, std::move(location), std::nullopt};
    reply.bytes = std::move(bytes);
    return reply;
  }

  void diagnose_content_length_accounting(const ScriptedHttpsExecutor &executor,
    std::size_t beforeUrls, std::size_t beforeScript, std::size_t beforeChunks,
    RegistryContentTransportStatus status)
  {
    const auto afterUrls = executor.urls.size();
    const auto afterScript = executor.next;
    const auto afterChunks = executor.bodyChunksDelivered;
    const char *statusName = "other";
    if (status == RegistryContentTransportStatus::available) statusName = "available";
    else if (status == RegistryContentTransportStatus::not_found) statusName = "not_found";
    else if (status == RegistryContentTransportStatus::transport_failure) statusName = "transport_failure";
    else if (status == RegistryContentTransportStatus::permission_denied) statusName = "permission_denied";
    else if (status == RegistryContentTransportStatus::configuration_error) statusName = "configuration_error";
    else if (status == RegistryContentTransportStatus::payload_too_large) statusName = "payload_too_large";
    if (status == RegistryContentTransportStatus::payload_too_large &&
        afterUrls == beforeUrls + 1 && afterScript == beforeScript + 1 &&
        afterChunks == beforeChunks)
      return;

    std::cerr << "Content-Length limit request accounting: before=" << beforeUrls
      << " after=" << afterUrls << " delta=" << (afterUrls - beforeUrls)
      << " status=" << statusName
      << " script-before=" << beforeScript << " script-after=" << afterScript
      << " body-chunks-before=" << beforeChunks << " body-chunks-after=" << afterChunks
      << " urls=[";
    for (std::size_t index = 0; index < executor.urls.size(); ++index)
    {
      if (index != 0)
        std::cerr << ", ";
      std::cerr << executor.urls[index];
    }
    std::cerr << "]\n";
  }

  CanonicalPackageEntry file(std::string path, std::string_view bytes)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    for (const unsigned char byte : bytes) entry.content.push_back(static_cast<std::byte>(byte));
    return entry;
  }

  CanonicalPackageTree tree(std::string path, std::string_view bytes)
  { return {{file(std::move(path), bytes)}}; }

  CanonicalPackageContentResult encoded(const CanonicalPackageTree &value)
  { return canonicalize_package_content_tree(value); }

  std::string registry_node_id_for(const RegistryPackageIdentity &identity, const Version &version)
  { return registry_node_id(RegistryNodeIdentity{identity, version}); }

  Lockfile lock_from_nodes(std::vector<LockNode> nodes)
  {
    Lockfile lock;
    std::vector<RootRequirementDigestEntry> digestEntries;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
      const auto nodeId = std::visit([](const auto &node) { return node.id; }, nodes[index]);
      const auto alias = "root-" + nodeId.substr(7);
      std::visit([&](const auto &node)
      {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, RegistryLockNode>)
        {
          const NormalizedDeclaredRegistry declared{node.identity, ExactVersion{node.version}};
          lock.roots.push_back({alias, declared, node.id});
          digestEntries.push_back({alias, declared});
        }
        else
        {
          const NormalizedDeclaredGit declared{node.normalizedRepository, true, CommitRevision{node.commit}};
          lock.roots.push_back({alias, declared, node.id});
          digestEntries.push_back({alias,
            NormalizedRootGitRequirement{node.normalizedRepository, declared.revision}});
        }
      }, nodes[index]);
      lock.nodes.push_back(std::move(nodes[index]));
    }
    lock.rootRequirementsDigest = root_requirements_digest(digestEntries);
    return lock;
  }

  RegistryLockNode registry_node(std::string registry, std::string name, const PackageContentId &content)
  {
    const RegistryPackageIdentity identity{std::move(registry), "example", std::move(name)};
    const Version version{1, 2, 3, std::nullopt};
    return {registry_node_id_for(identity, version), identity, version, content.digest(), {}};
  }

  GitLockNode git_node(std::string repository, std::string commit, const PackageContentId &content)
  {
    return {git_node_id(GitNodeIdentity{repository, commit}), std::move(repository), true,
      std::move(commit), content.digest(), {}};
  }

  RegistryDirectory registry_directory(std::vector<RegistryDefinition> definitions)
  {
    auto result = build_registry_directory(std::move(definitions));
    assert(result.valid());
    return std::move(*result.value);
  }

  class FakeRegistrySource final : public RegistryPackageContentSource
  {
  public:
    std::vector<std::string> calls;
    std::vector<RegistryPackageContentRequest> requests;
    std::vector<std::pair<std::string, RegistryPackageContentSourceResult>> responses;
    std::vector<std::string> *events{nullptr};

    RegistryPackageContentSourceResult acquire(const RegistryContentLocation &location,
      const RegistryPackageContentRequest &request) override
    {
      calls.push_back(location.descriptor);
      requests.push_back(request);
      if (events) events->push_back("registry:" + location.descriptor);
      const auto found = std::find_if(responses.begin(), responses.end(), [&](const auto &item)
      { return item.first == location.descriptor; });
      return found == responses.end() ? RegistryPackageContentSourceResult{} : found->second;
    }
  };

  class FakeGitSource final : public GitPackageContentSource
  {
  public:
    std::vector<GitPackageContentRequest> requests;
    GitPackageContentSourceResult response;
    std::vector<std::string> *events{nullptr};

    GitPackageContentSourceResult acquire(const GitPackageContentRequest &request) override
    {
      requests.push_back(request);
      if (events) events->push_back("git:" + request.normalizedRepository);
      return response;
    }
  };

  class CorruptStore final : public PackageContentStore
  {
  public:
    PackageContentStoreResult inspect(const PackageContentId &id) const override
    {
      return {PackageContentAvailability::corrupt, {{PackageContentIssueCode::package_content_store_corrupt,
        id.digest(), {}, "fixture corruption"}}};
    }
    PackageContentAdmissionResult admit(const VerifiedPackageContent &) override
    { assert(false); return {}; }
  };

  class MixedAvailabilityStore final : public PackageContentStore
  {
  public:
    std::vector<std::string> inspected;
    std::vector<std::pair<std::string, PackageContentAvailability>> states;

    PackageContentStoreResult inspect(const PackageContentId &id) const override
    {
      const_cast<MixedAvailabilityStore *>(this)->inspected.push_back(id.digest());
      const auto found = std::find_if(states.begin(), states.end(), [&](const auto &entry)
        { return entry.first == id.digest(); });
      const auto state = found == states.end() ? PackageContentAvailability::missing : found->second;
      if (state == PackageContentAvailability::corrupt)
        return {state, {{PackageContentIssueCode::package_content_store_corrupt,
          id.digest(), {}, "fixture corruption"}}};
      return {state, {}};
    }
    PackageContentAdmissionResult admit(const VerifiedPackageContent &) override
    { assert(false); return {}; }
  };

  PackageContentAcquisitionResult acquire(const Lockfile &lock, const RegistryDirectory &directory,
    RegistryPackageContentSource &registry, GitPackageContentSource &git, PackageContentStore &store)
  { return acquire_locked_package_content(lock, AcquisitionMode::online, directory, registry, git, store); }

  void reuse_and_registry_acquisition_test()
  {
    const auto contentTree = tree("README.md", "registry bytes\n");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "sqlite", *object.contentId)});
    const auto lockBefore = serialize_lockfile(lock);
    assert(lockBefore.valid());
    const auto directory = registry_directory({{"vix", {{"metadata-A"}}, {{"mirror-A"}}}});
    InMemoryPackageContentStore store;
    CanonicalPackageContentVerifier verifier;
    const auto token = verifier.verify(std::make_shared<CanonicalPackageContentCandidate>(contentTree), *object.contentId);
    assert(token.valid() && store.admit(*token.value).valid());
    FakeRegistrySource registry;
    FakeGitSource git;
    const auto reused = acquire(lock, directory, registry, git, store);
    assert(reused.valid() && reused.entries.size() == 1);
    assert(reused.entries.front().state == PackageContentAcquisitionState::reused);
    assert(registry.calls.empty() && git.requests.empty());

    InMemoryPackageContentStore empty;
    registry.responses.push_back({"mirror-A", {RegistryPackageContentSourceStatus::available,
      *object.canonicalBytes, {}}});
    const auto fetched = acquire(lock, directory, registry, git, empty);
    assert(fetched.valid() && fetched.entries.size() == 1);
    assert(fetched.entries.front().state == PackageContentAcquisitionState::acquired);
    assert(registry.calls == std::vector<std::string>{"mirror-A"});
    assert(registry.requests.front().identity == std::get<RegistryLockNode>(lock.nodes.front()).identity);
    assert(registry.requests.front().version == std::get<RegistryLockNode>(lock.nodes.front()).version);
    assert(registry.requests.front().expectedContent == *object.contentId);
    assert(empty.inspect(*object.contentId).value == PackageContentAvailability::available);
    assert(serialize_lockfile(lock).text == lockBefore.text);
  }

  void git_acquisition_and_exact_request_test()
  {
    const auto contentTree = tree("src/git.cpp", "git tree\n");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const std::string commit(40, 'a');
    const auto lock = lock_from_nodes({git_node("https://example.invalid/repo", commit, *object.contentId)});
    const auto directory = registry_directory({});
    FakeRegistrySource registry;
    FakeGitSource git;
    git.response = {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(contentTree), {}};
    InMemoryPackageContentStore store;
    const auto result = acquire(lock, directory, registry, git, store);
    assert(result.valid() && result.entries.front().state == PackageContentAcquisitionState::acquired);
    assert(registry.calls.empty() && git.requests.size() == 1);
    assert(git.requests.front().normalizedRepository == "https://example.invalid/repo");
    assert(git.requests.front().fullCommit == commit);
    assert(git.requests.front().expectedContent == *object.contentId);
    assert(store.inspect(*object.contentId).value == PackageContentAvailability::available);
  }

  void digest_mismatch_and_lock_immutability_test()
  {
    const auto expectedTree = tree("expected", "locked");
    const auto wrongTree = tree("wrong", "payload");
    const auto expected = encoded(expectedTree);
    const auto wrong = encoded(wrongTree);
    assert(expected.valid() && wrong.valid() && expected.contentId != wrong.contentId);
    const auto lock = lock_from_nodes({registry_node("vix", "mismatch", *expected.contentId)});
    const auto before = serialize_lockfile(lock);
    assert(before.valid());
    const auto directory = registry_directory({{"vix", {{"metadata-A"}}, {{"A"}, {"B"}}}});
    FakeRegistrySource registry;
    registry.responses.push_back({"A", {RegistryPackageContentSourceStatus::available, *wrong.canonicalBytes, {}}});
    registry.responses.push_back({"B", {RegistryPackageContentSourceStatus::available, *expected.canonicalBytes, {}}});
    FakeGitSource git;
    InMemoryPackageContentStore store;
    const auto result = acquire(lock, directory, registry, git, store);
    assert(!result.valid() && result.issues.front().code == PackageContentAcquisitionIssueCode::package_content_digest_mismatch);
    assert(registry.calls == std::vector<std::string>{"A"});
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(lock).text == before.text);

    const std::string commit(40, 'b');
    const auto gitLock = lock_from_nodes({git_node("https://example.invalid/wrong", commit, *expected.contentId)});
    git.response = {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(wrongTree), {}};
    const auto gitBefore = serialize_lockfile(gitLock);
    const auto gitResult = acquire(gitLock, directory, registry, git, store);
    assert(!gitResult.valid() && gitResult.issues.front().code == PackageContentAcquisitionIssueCode::package_content_digest_mismatch);
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(gitLock).text == gitBefore.text);
  }

  void unsafe_git_symlink_rejected_by_p10o_test()
  {
    const auto expected = encoded(tree("expected", "locked"));
    assert(expected.valid());
    CanonicalPackageEntry unsafe;
    unsafe.path = "include/link";
    unsafe.kind = CanonicalPackageEntryKind::symlink;
    unsafe.target = "../../outside";
    const CanonicalPackageTree untrustedTree{{unsafe}};
    const auto lock = lock_from_nodes({git_node("https://example.invalid/symlink",
      std::string(40, 'e'), *expected.contentId)});
    FakeRegistrySource registry;
    FakeGitSource git;
    git.response = {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(untrustedTree), {}};
    InMemoryPackageContentStore store;
    const auto result = acquire(lock, registry_directory({}), registry, git, store);
    assert(!result.valid());
    assert(result.issues.front().code == PackageContentAcquisitionIssueCode::package_content_integrity_failure);
    assert(!result.issues.front().canonicalIssues.empty());
    assert(result.issues.front().canonicalIssues.front().code == CanonicalPackageContentIssueCode::invalid_symlink_target);
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
  }

  void registry_integrity_and_mirror_routing_test()
  {
    const auto contentTree = tree("pkg", "expected\n");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "routing", *object.contentId)});

    {
      const auto directory = registry_directory({{"vix", {{"metadata-A"}}, {{"A"}, {"B"}, {"C"}}}});
      FakeRegistrySource registry;
      registry.responses.push_back({"A", {RegistryPackageContentSourceStatus::not_found, std::nullopt, "offline"}});
      registry.responses.push_back({"B", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      registry.responses.push_back({"C", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      FakeGitSource git;
      InMemoryPackageContentStore store;
      const auto result = acquire(lock, directory, registry, git, store);
      assert(result.valid() && registry.calls == std::vector<std::string>({"A", "B"}));
    }
    {
      const auto directory = registry_directory({{"vix", {{"metadata-A"}}, {{"A"}, {"B"}}}});
      FakeRegistrySource registry;
      registry.responses.push_back({"A", {RegistryPackageContentSourceStatus::available,
        std::string{"not canonical"}, {}}});
      registry.responses.push_back({"B", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      FakeGitSource git;
      InMemoryPackageContentStore store;
      const auto result = acquire(lock, directory, registry, git, store);
      assert(!result.valid());
      assert(result.issues.front().code == PackageContentAcquisitionIssueCode::package_content_integrity_failure);
      assert(registry.calls == std::vector<std::string>{"A"});
      assert(store.inspect(*object.contentId).value == PackageContentAvailability::missing);
    }
    {
      const auto directory = registry_directory({{"vix", {{"metadata-A"}}, {{"A"}, {"B"}}}});
      FakeRegistrySource registry;
      registry.responses.push_back({"A", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      registry.responses.push_back({"B", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}});
      FakeGitSource git;
      InMemoryPackageContentStore store;
      const auto result = acquire(lock, directory, registry, git, store);
      assert(result.valid() && registry.calls == std::vector<std::string>{"A"});
    }
  }

  void registry_content_routing_separation_and_empty_test()
  {
    const auto contentTree = tree("content", "payload");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "routing-separation", *object.contentId)});
    const auto lockBefore = serialize_lockfile(lock);
    assert(lockBefore.valid());

    RegistryDefinition definition;
    definition.id = "vix";
    definition.locations = {{"metadata-A"}, {"metadata-B"}};
    definition.contentLocations = {{"content-Z"}, {"content-A"}, {"content-M"}};
    const auto directory = registry_directory({definition});
    FakeRegistrySource registry;
    registry.responses = {
      {"content-Z", {RegistryPackageContentSourceStatus::not_found, std::nullopt, "not here"}},
      {"content-A", {RegistryPackageContentSourceStatus::transport_failure, std::nullopt, "offline"}},
      {"content-M", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}},
      {"metadata-A", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}},
      {"metadata-B", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}},
    };
    FakeGitSource git;
    InMemoryPackageContentStore store;
    const auto result = acquire(lock, directory, registry, git, store);
    assert(result.valid());
    assert((registry.calls == std::vector<std::string>{"content-Z", "content-A", "content-M"}));
    assert(std::find(registry.calls.begin(), registry.calls.end(), "metadata-A") == registry.calls.end());
    assert(std::find(registry.calls.begin(), registry.calls.end(), "metadata-B") == registry.calls.end());
    assert(serialize_lockfile(lock).text == lockBefore.text);

    RegistryDefinition metadataOnly;
    metadataOnly.id = "vix";
    metadataOnly.locations = {{"metadata-A"}, {"metadata-B"}};
    const auto metadataOnlyDirectory = registry_directory({metadataOnly});
    FakeRegistrySource unusedSource;
    FakeGitSource unusedGit;
    InMemoryPackageContentStore emptyStore;
    const auto unavailable = acquire(lock, metadataOnlyDirectory, unusedSource, unusedGit, emptyStore);
    assert(!unavailable.valid());
    assert(unavailable.issues.front().code ==
      PackageContentAcquisitionIssueCode::package_content_source_unavailable);
    assert(unavailable.issues.front().message == "registry has no package content locations configured");
    assert(unusedSource.calls.empty());
    assert(emptyStore.inspect(*object.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(lock).text == lockBefore.text);
  }

  void registry_content_object_path_tests()
  {
    const auto zero = PackageContentId::parse("sha256:0000000000000000000000000000000000000000000000000000000000000000");
    const auto alpha = PackageContentId::parse("sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    assert(zero && alpha);
    const std::string zeroPath = registry_format1_content_object_path(*zero);
    const std::string alphaPath = registry_format1_content_object_path(*alpha);
    assert(zeroPath == "objects/sha256/00/00000000000000000000000000000000000000000000000000000000000000");
    assert(alphaPath == "objects/sha256/ab/cdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    assert(zeroPath.size() == 80 && alphaPath.size() == 80);

    const RegistryPackageIdentity contextA{"vix", "publisher-a", "package-a"};
    const RegistryPackageIdentity contextB{"other", "publisher-b", "package-b"};
    const Version versionA{1, 0, 0, std::nullopt};
    const Version versionB{99, 2, 3, std::nullopt};
    const RegistryContentLocation locationA{"https://mirror-a.invalid"};
    const RegistryContentLocation locationB{"file:///mirror-b"};
    assert(contextA != contextB && versionA != versionB && locationA != locationB);
    const RegistryPackageContentRequest requestA{contextA, versionA, *zero};
    const RegistryPackageContentRequest requestB{contextB, versionB, *zero};
    assert(registry_format1_content_object_path(requestA.expectedContent) ==
      registry_format1_content_object_path(requestB.expectedContent));
    assert(zeroPath != alphaPath);
  }

  void local_corruption_and_all_transport_failure_test()
  {
    const auto contentTree = tree("pkg", "payload");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "unavailable", *object.contentId)});
    const auto directory = registry_directory({{"vix", {{"metadata-A"}}, {{"A"}, {"B"}}}});
    FakeRegistrySource registry;
    FakeGitSource git;
    CorruptStore corrupt;
    const auto corruptResult = acquire(lock, directory, registry, git, corrupt);
    assert(!corruptResult.valid() && registry.calls.empty() && git.requests.empty());
    assert(corruptResult.issues.front().code == PackageContentAcquisitionIssueCode::package_content_store_failure);

    InMemoryPackageContentStore empty;
    registry.responses.push_back({"A", {RegistryPackageContentSourceStatus::not_found, std::nullopt, "absent"}});
    registry.responses.push_back({"B", {RegistryPackageContentSourceStatus::transport_failure, std::nullopt, "temporary"}});
    const auto unavailable = acquire(lock, directory, registry, git, empty);
    assert(!unavailable.valid());
    assert(unavailable.issues.front().code == PackageContentAcquisitionIssueCode::package_content_transport_failure);
    assert(registry.calls == std::vector<std::string>({"A", "B"}));
    assert(empty.inspect(*object.contentId).value == PackageContentAvailability::missing);

    const auto unknownLock = lock_from_nodes({registry_node("missing", "unknown", *object.contentId)});
    const auto unknown = registry_directory({});
    FakeRegistrySource unknownRegistry;
    FakeGitSource unknownGit;
    const auto unknownResult = acquire(unknownLock, unknown, unknownRegistry, unknownGit, empty);
    assert(!unknownResult.valid());
    assert(unknownResult.issues.front().code == PackageContentAcquisitionIssueCode::unknown_registry);
    assert(unknownRegistry.calls.empty());
  }

  void mixed_sources_dedup_and_order_test()
  {
    const auto treeA = tree("a", "registry A");
    const auto treeB = tree("b", "git B");
    const auto treeC = tree("c", "registry C");
    const auto objectA = encoded(treeA);
    const auto objectB = encoded(treeB);
    const auto objectC = encoded(treeC);
    assert(objectA.valid() && objectB.valid() && objectC.valid());
    const auto regA = registry_node("vix", "a", *objectA.contentId);
    const auto gitB = git_node("https://example.invalid/b", std::string(40, 'c'), *objectB.contentId);
    const auto regC = registry_node("acme", "c", *objectC.contentId);
    auto lockA = lock_from_nodes({regA, gitB, regC});
    auto lockB = lock_from_nodes({regC, regA, gitB});
    const auto dir = registry_directory({{"acme", {{"metadata-C"}}, {{"C"}}}, {"vix", {{"metadata-A"}}, {{"A"}}}});

    const auto run = [&](const Lockfile &lock, std::vector<std::string> &events)
    {
      FakeRegistrySource registry;
      registry.events = &events;
      registry.responses = {{"A", {RegistryPackageContentSourceStatus::available, *objectA.canonicalBytes, {}}},
        {"C", {RegistryPackageContentSourceStatus::available, *objectC.canonicalBytes, {}}}};
      FakeGitSource git;
      git.events = &events;
      git.response = {PackageContentSourceStatus::available,
        std::make_shared<CanonicalPackageContentCandidate>(treeB), {}};
      InMemoryPackageContentStore store;
      const auto acquired = acquire(lock, dir, registry, git, store);
      assert(acquired.valid() && acquired.entries.size() == 3);
      assert(store.inspect(*objectA.contentId).value == PackageContentAvailability::available);
      assert(store.inspect(*objectB.contentId).value == PackageContentAvailability::available);
      assert(store.inspect(*objectC.contentId).value == PackageContentAvailability::available);
    };
    std::vector<std::string> orderA;
    std::vector<std::string> orderB;
    run(lockA, orderA);
    run(lockB, orderB);
    assert(orderA == orderB);

    // Duplicate digest across two same-source nodes is acquired once.
    auto sharedLock = lock_from_nodes({registry_node("vix", "first", *objectA.contentId),
      registry_node("acme", "second", *objectA.contentId)});
    FakeRegistrySource sharedRegistry;
    sharedRegistry.responses = {{"A", {RegistryPackageContentSourceStatus::available, *objectA.canonicalBytes, {}}}};
    FakeGitSource sharedGit;
    InMemoryPackageContentStore sharedStore;
    const auto sharedDir = registry_directory({{"vix", {{"metadata-vix"}}, {{"A"}}}, {"acme", {{"metadata-acme"}}, {{"A"}}}});
    const auto sharedResult = acquire(sharedLock, sharedDir, sharedRegistry, sharedGit, sharedStore);
    assert(sharedResult.valid() && sharedResult.entries.size() == 1);
    assert(sharedRegistry.calls.size() == 1 && sharedGit.requests.empty());
    assert(sharedResult.entries.front().nodeIds.size() == 2);

    // If Registry and Git nodes share a digest, sorted node ID selects exactly
    // one source; the other source is never used as implicit fallback.
    const auto crossLock = lock_from_nodes({registry_node("vix", "cross", *objectA.contentId),
      git_node("https://example.invalid/cross", std::string(40, 'd'), *objectA.contentId)});
    const auto crossPlan = required_package_content(crossLock);
    assert(crossPlan.valid() && crossPlan.required.size() == 1);
    const auto selected = std::find_if(crossLock.nodes.begin(), crossLock.nodes.end(), [&](const auto &node)
      { return std::visit([&](const auto &value) { return value.id == crossPlan.required.front().nodeIds.front(); }, node); });
    assert(selected != crossLock.nodes.end());
    FakeRegistrySource crossRegistry;
    crossRegistry.responses = {{"A", {RegistryPackageContentSourceStatus::available, *objectA.canonicalBytes, {}}}};
    FakeGitSource crossGit;
    crossGit.response = {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(treeA), {}};
    InMemoryPackageContentStore crossStore;
    const auto crossDir = registry_directory({{"vix", {{"metadata-A"}}, {{"A"}}}});
    const auto crossResult = acquire(crossLock, crossDir, crossRegistry, crossGit, crossStore);
    assert(crossResult.valid() && crossResult.entries.size() == 1);
    const bool selectedRegistry = std::holds_alternative<RegistryLockNode>(*selected);
    assert((crossRegistry.calls.size() == 1) == selectedRegistry);
    assert((crossGit.requests.size() == 1) == !selectedRegistry);

    FakeRegistrySource failedRegistry;
    failedRegistry.responses = {{"A", {RegistryPackageContentSourceStatus::transport_failure, std::nullopt, "failed"}}};
    FakeGitSource failedGit;
    failedGit.response = {PackageContentSourceStatus::transport_failure, nullptr, "failed"};
    InMemoryPackageContentStore failedStore;
    const auto failedCross = acquire(crossLock, crossDir, failedRegistry, failedGit, failedStore);
    assert(!failedCross.valid());
    assert((failedRegistry.calls.size() == 1) == selectedRegistry);
    assert((failedGit.requests.size() == 1) == !selectedRegistry);
    assert(failedStore.inspect(*objectA.contentId).value == PackageContentAvailability::missing);
  }

  void restart_reuse_and_provider_boundary_test()
  {
    const auto contentTree = tree("persisted", "payload");
    const auto object = encoded(contentTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "restart", *object.contentId)});
    const auto directory = registry_directory({{"vix", {{"metadata-A"}}, {{"A"}}}});
    TestRoot root;
    {
      FilesystemPackageContentStore first{root.path()};
      FakeRegistrySource registry;
      registry.responses = {{"A", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}}};
      FakeGitSource git;
      const auto result = acquire(lock, directory, registry, git, first);
      assert(result.valid() && result.entries.front().state == PackageContentAcquisitionState::acquired);
      assert(registry.calls.size() == 1);
    }
    FilesystemPackageContentStore reopened{root.path()};
    FakeRegistrySource registry;
    FakeGitSource git;
    const auto reused = acquire(lock, directory, registry, git, reopened);
    assert(reused.valid() && reused.entries.front().state == PackageContentAcquisitionState::reused);
    assert(registry.calls.empty() && git.requests.empty());

    static_assert(!std::is_constructible_v<VerifiedPackageContent, PackageContentId,
      std::shared_ptr<const PackageContentCandidate>, std::string>);
    static_assert(std::is_same_v<decltype(GitPackageContentSourceResult::candidate),
      std::shared_ptr<const CanonicalPackageContentCandidate>>);
  }

  void file_transport_contract_tests()
  {
    FileRegistryContentTransport fileTransport;
    const auto id = PackageContentId::parse(
      "sha256:0000000000000000000000000000000000000000000000000000000000000000");
    assert(id);
    TestRoot root;
    const std::string binaryBytes{"\0\xff\n\r", 4};
    write_bytes(mirror_object_path(root.path(), *id), binaryBytes);
    const RegistryContentLocation location{file_location(root.path())};
    const auto hit = fileTransport.fetch(location, registry_format1_content_object_path(*id), 32);
    assert(hit.status == RegistryContentTransportStatus::available);
    assert(hit.bytes && *hit.bytes == binaryBytes);

    const auto missing = fileTransport.fetch({file_location(root.path() / "absent")},
      registry_format1_content_object_path(*id), 32);
    assert(missing.status == RegistryContentTransportStatus::not_found);

    for (const auto &descriptor : {std::string{}, std::string{"file:relative"},
        std::string{"file://relative/path"}, std::string{"file://host/path"},
        std::string{"file:/tmp/malformed"}, std::string{"https://example.invalid/mirror"}})
    {
      const auto rejected = fileTransport.fetch({descriptor}, registry_format1_content_object_path(*id), 32);
      assert(rejected.status == RegistryContentTransportStatus::configuration_error);
    }

    const auto oversized = fileTransport.fetch(location, registry_format1_content_object_path(*id), 3);
    assert(oversized.status == RegistryContentTransportStatus::payload_too_large);

    const auto directoryObject = mirror_object_path(root.path(), *PackageContentId::parse(
      "sha256:1111111111111111111111111111111111111111111111111111111111111111"));
    fs::create_directories(directoryObject);
    const auto directoryResult = fileTransport.fetch(location,
      "objects/sha256/11/11111111111111111111111111111111111111111111111111111111111111", 32);
    assert(directoryResult.status == RegistryContentTransportStatus::configuration_error);

#ifndef _WIN32
    const auto external = root.path() / "external";
    write_bytes(external, "must not be followed");
    const auto finalLinkId = PackageContentId::parse(
      "sha256:2222222222222222222222222222222222222222222222222222222222222222");
    assert(finalLinkId);
    fs::create_directories(mirror_object_path(root.path(), *finalLinkId).parent_path());
    fs::create_symlink(external, mirror_object_path(root.path(), *finalLinkId));
    const auto finalLink = fileTransport.fetch(location,
      registry_format1_content_object_path(*finalLinkId), 128);
    assert(finalLink.status == RegistryContentTransportStatus::configuration_error);
    assert(!finalLink.bytes);

    TestRoot intermediateRoot;
    fs::create_directories(intermediateRoot.path() / "elsewhere" / "sha256" / "33");
    fs::create_symlink(intermediateRoot.path() / "elsewhere",
      intermediateRoot.path() / "objects");
    const auto intermediate = fileTransport.fetch({file_location(intermediateRoot.path())},
      "objects/sha256/33/33333333333333333333333333333333333333333333333333333333333333", 128);
    assert(intermediate.status == RegistryContentTransportStatus::configuration_error);
#endif
  }

  void file_transport_acquisition_and_fallback_tests()
  {
    CanonicalPackageTree packageTree;
    auto manifest = file("vix.package.toml", "name = 'demo'\n");
    auto binary = file("binary.dat", std::string_view{"\0\xff\n", 3});
    auto crlf = file("line.txt", "a\r\n");
    auto script = file("script.sh", "echo hi\n");
    script.executable = true;
    CanonicalPackageEntry link;
    link.path = "link.hpp";
    link.kind = CanonicalPackageEntryKind::symlink;
    link.target = "vix.package.toml";
    packageTree.entries = {manifest, binary, crlf, script, link};
    const auto canonical = encoded(packageTree);
    assert(canonical.valid());

    TestRoot mirrorA;
    TestRoot mirrorB;
    write_bytes(mirror_object_path(mirrorB.path(), *canonical.contentId), *canonical.canonicalBytes);
    const auto mirrorAUri = file_location(mirrorA.path());
    const auto mirrorBUri = file_location(mirrorB.path());
    const auto lock = lock_from_nodes({registry_node("vix", "file-mirror", *canonical.contentId)});
    const auto lockBefore = serialize_lockfile(lock);
    assert(lockBefore.valid());
    const auto directory = registry_directory({RegistryDefinition{
      "vix", {{"metadata-only"}}, {{mirrorAUri}, {mirrorBUri}}}});

    FileRegistryContentTransport concreteTransport;
    RecordingTransport recording{concreteTransport};
    TransportRegistryPackageContentSource source{recording};
    FakeGitSource git;
    TestRoot storeRoot;
    {
      FilesystemPackageContentStore store{storeRoot.path()};
      const auto result = acquire(lock, directory, source, git, store);
      assert(result.valid() && result.entries.size() == 1);
      assert(result.entries.front().state == PackageContentAcquisitionState::acquired);
      assert(recording.calls.size() == 2);
      assert(recording.calls[0] == mirrorAUri + "|" + registry_format1_content_object_path(*canonical.contentId));
      assert(recording.calls[1] == mirrorBUri + "|" + registry_format1_content_object_path(*canonical.contentId));
      assert(store.inspect(*canonical.contentId).value == PackageContentAvailability::available);
      const auto digest = canonical.contentId->digest().substr(7);
      const auto stored = storeRoot.path() / "objects" / "sha256" / digest.substr(0, 2) / digest.substr(2);
      assert(read_bytes(stored) == *canonical.canonicalBytes);
      assert(serialize_lockfile(lock).text == lockBefore.text);
    }

    FilesystemPackageContentStore reopened{storeRoot.path()};
    CountingTransport mustNotRun;
    TransportRegistryPackageContentSource reusedSource{mustNotRun};
    const auto reused = acquire(lock, directory, reusedSource, git, reopened);
    assert(reused.valid() && reused.entries.front().state == PackageContentAcquisitionState::reused);
    assert(mustNotRun.calls == 0);

    // The same digest addresses identical bytes independently of mirror root.
    TestRoot mirrorC;
    write_bytes(mirror_object_path(mirrorC.path(), *canonical.contentId), *canonical.canonicalBytes);
    assert(mirror_object_path(mirrorB.path(), *canonical.contentId).lexically_relative(mirrorB.path()) ==
      mirror_object_path(mirrorC.path(), *canonical.contentId).lexically_relative(mirrorC.path()));
    TestRoot otherStoreRoot;
    FilesystemPackageContentStore otherMirrorStore{otherStoreRoot.path()};
    const auto otherMirrorDirectory = registry_directory({RegistryDefinition{
      "vix", {{"metadata-only"}}, {{file_location(mirrorC.path())}}}});
    RecordingTransport otherMirrorRecording{concreteTransport};
    TransportRegistryPackageContentSource otherMirrorSource{otherMirrorRecording};
    const auto otherMirrorResult = acquire(lock, otherMirrorDirectory, otherMirrorSource, git, otherMirrorStore);
    assert(otherMirrorResult.valid() && otherMirrorResult.entries.front().content == *canonical.contentId);
    assert(otherMirrorStore.inspect(*canonical.contentId).value == PackageContentAvailability::available);
    const auto digest = canonical.contentId->digest().substr(7);
    const auto otherStored = otherStoreRoot.path() / "objects" / "sha256" / digest.substr(0, 2) / digest.substr(2);
    assert(read_bytes(otherStored) == *canonical.canonicalBytes);

    // A missing first mirror falls through in declaration order to the exact object.
    TestRoot fallbackA;
    const auto fallbackDirectory = registry_directory({RegistryDefinition{
      "vix", {{"metadata"}}, {{file_location(fallbackA.path())}, {mirrorBUri}}}});
    TestRoot fallbackStoreRoot;
    FilesystemPackageContentStore fallbackStore{fallbackStoreRoot.path()};
    RecordingTransport fallbackRecording{concreteTransport};
    TransportRegistryPackageContentSource fallbackSource{fallbackRecording};
    const auto fallbackResult = acquire(lock, fallbackDirectory, fallbackSource, git, fallbackStore);
    assert(fallbackResult.valid());
    assert(fallbackRecording.calls.size() == 2);
    assert(fallbackRecording.calls[0].starts_with(file_location(fallbackA.path())));
    assert(fallbackRecording.calls[1].starts_with(mirrorBUri));
    assert(serialize_lockfile(lock).text == lockBefore.text);
  }

  void file_transport_terminal_failure_tests()
  {
    const auto expectedTree = tree("expected", "locked");
    const auto wrongTree = tree("wrong", "content");
    const auto expected = encoded(expectedTree);
    const auto wrong = encoded(wrongTree);
    assert(expected.valid() && wrong.valid() && expected.contentId != wrong.contentId);
    const auto lock = lock_from_nodes({registry_node("vix", "file-failure", *expected.contentId)});
    const auto before = serialize_lockfile(lock);
    assert(before.valid());
    TestRoot first;
    TestRoot second;
    write_bytes(mirror_object_path(first.path(), *expected.contentId), "malformed canonical object");
    write_bytes(mirror_object_path(second.path(), *expected.contentId), *expected.canonicalBytes);
    const auto directory = registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{file_location(first.path())}, {file_location(second.path())}}}});
    FileRegistryContentTransport concrete;
    RecordingTransport recording{concrete};
    TransportRegistryPackageContentSource source{recording};
    FakeGitSource git;
    TestRoot storeRoot;
    FilesystemPackageContentStore store{storeRoot.path()};
    const auto malformed = acquire(lock, directory, source, git, store);
    assert(!malformed.valid());
    assert(malformed.issues.front().code == PackageContentAcquisitionIssueCode::package_content_integrity_failure);
    assert(recording.calls.size() == 1);
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(lock).text == before.text);

    write_bytes(mirror_object_path(first.path(), *expected.contentId), *wrong.canonicalBytes);
    recording.calls.clear();
    const auto mismatch = acquire(lock, directory, source, git, store);
    assert(!mismatch.valid());
    assert(mismatch.issues.front().code == PackageContentAcquisitionIssueCode::package_content_digest_mismatch);
    assert(recording.calls.size() == 1);
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
    assert(store.inspect(*wrong.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(lock).text == before.text);

    // Oversize is a terminal transport result, even if the next mirror has the object.
    write_bytes(mirror_object_path(first.path(), *expected.contentId), *expected.canonicalBytes);
    RecordingTransport oversizedRecording{concrete};
    TransportRegistryPackageContentSource oversizedSource{oversizedRecording, 1};
    const auto oversized = acquire(lock, directory, oversizedSource, git, store);
    assert(!oversized.valid());
    assert(oversized.issues.front().code == PackageContentAcquisitionIssueCode::package_content_payload_too_large);
    assert(oversizedRecording.calls.size() == 1);
    assert(store.inspect(*expected.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(lock).text == before.text);
  }

  void registry_transport_failure_classification_test()
  {
    const auto object = encoded(tree("classification", "payload"));
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "classification", *object.contentId)});
    const auto before = serialize_lockfile(lock);
    assert(before.valid());
    const auto directory = registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{"permission-A"}, {"available-B"}}}});
    FakeRegistrySource source;
    source.responses = {
      {"permission-A", {RegistryPackageContentSourceStatus::permission_denied, std::nullopt, "denied"}},
      {"available-B", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}},
    };
    FakeGitSource git;
    InMemoryPackageContentStore store;
    const auto acquired = acquire(lock, directory, source, git, store);
    assert(acquired.valid() && source.calls == std::vector<std::string>({"permission-A", "available-B"}));
    assert(serialize_lockfile(lock).text == before.text);

    FakeRegistrySource malformedLocation;
    malformedLocation.responses = {
      {"permission-A", {RegistryPackageContentSourceStatus::configuration_error, std::nullopt, "bad descriptor"}},
      {"available-B", {RegistryPackageContentSourceStatus::available, *object.canonicalBytes, {}}},
    };
    InMemoryPackageContentStore empty;
    const auto rejected = acquire(lock, directory, malformedLocation, git, empty);
    assert(!rejected.valid());
    assert(rejected.issues.front().code ==
      PackageContentAcquisitionIssueCode::package_content_source_configuration_error);
    assert(malformedLocation.calls == std::vector<std::string>{"permission-A"});
    assert(empty.inspect(*object.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(lock).text == before.text);
  }

  void locked_content_acquisition_mode_tests()
  {
    const auto registryTree = tree("offline/registry.hpp", "registry package\n");
    const auto gitTree = tree("offline/git.hpp", "git package\n");
    const auto registryObject = encoded(registryTree);
    const auto gitObject = encoded(gitTree);
    assert(registryObject.valid() && gitObject.valid());
    const auto registryLock = lock_from_nodes({registry_node("vix", "offline-registry", *registryObject.contentId)});
    const auto gitLock = lock_from_nodes({git_node("file:///local/repository", std::string(40, 'c'), *gitObject.contentId)});
    const auto directory = registry_directory({{"vix", {{"metadata"}}, {{"file:///mirror"}, {"https://mirror.invalid"}}}});
    const auto registryBefore = serialize_lockfile(registryLock);
    const auto gitBefore = serialize_lockfile(gitLock);
    assert(registryBefore.valid() && gitBefore.valid());

    // Offline complete content and online complete content both reuse P10P.
    InMemoryPackageContentStore complete;
    CanonicalPackageContentVerifier verifier;
    for (const auto &item : {std::pair{registryTree, *registryObject.contentId},
           std::pair{gitTree, *gitObject.contentId}})
    {
      const auto verified = verifier.verify(std::make_shared<CanonicalPackageContentCandidate>(item.first), item.second);
      assert(verified.valid() && complete.admit(*verified.value).valid());
    }
    FakeRegistrySource registry;
    FakeGitSource git;
    auto reusedRegistry = acquire_locked_package_content(registryLock, AcquisitionMode::offline,
      directory, registry, git, complete);
    auto reusedGit = acquire_locked_package_content(gitLock, AcquisitionMode::offline,
      directory, registry, git, complete);
    assert(reusedRegistry.valid() && reusedRegistry.entries.front().state == PackageContentAcquisitionState::reused);
    assert(reusedGit.valid() && reusedGit.entries.front().state == PackageContentAcquisitionState::reused);
    auto onlineReuse = acquire_locked_package_content(registryLock, AcquisitionMode::online,
      directory, registry, git, complete);
    assert(onlineReuse.valid() && onlineReuse.entries.front().state == PackageContentAcquisitionState::reused);
    assert(registry.calls.empty() && git.requests.empty());
    assert(serialize_lockfile(registryLock).text == registryBefore.text);
    assert(serialize_lockfile(gitLock).text == gitBefore.text);

    // Missing content has a distinct offline policy result for both source kinds.
    InMemoryPackageContentStore empty;
    const auto registryMiss = acquire_locked_package_content(registryLock, AcquisitionMode::offline,
      directory, registry, git, empty);
    const auto gitMiss = acquire_locked_package_content(gitLock, AcquisitionMode::offline,
      directory, registry, git, empty);
    assert(!registryMiss.valid() && registryMiss.issues.front().code ==
      PackageContentAcquisitionIssueCode::offline_package_content_unavailable);
    assert(!gitMiss.valid() && gitMiss.issues.front().code ==
      PackageContentAcquisitionIssueCode::offline_package_content_unavailable);
    assert(registryMiss.issues.front().digest == registryObject.contentId->digest());
    assert(registryMiss.issues.front().message.find("not available locally") != std::string::npos);
    assert(registryMiss.issues.front().message.find("offline mode prohibits package source acquisition") != std::string::npos);
    assert(registryMiss.issues.front().message.find(registryObject.contentId->digest()) != std::string::npos);
    assert(registry.calls.empty() && git.requests.empty());
    assert(empty.inspect(*registryObject.contentId).value == PackageContentAvailability::missing);
    assert(empty.inspect(*gitObject.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(registryLock).text == registryBefore.text);
    assert(serialize_lockfile(gitLock).text == gitBefore.text);

    // Offline policy blocks the complete file/HTTPS transport stack before it
    // can inspect a file mirror or start an HTTPS request.
    ScriptedHttpsExecutor httpsExecutor;
    SchemeRegistryContentTransport schemeTransport{httpsExecutor};
    RecordingTransport recording{schemeTransport};
    TransportRegistryPackageContentSource transportSource{recording};
    const auto transportMiss = acquire_locked_package_content(registryLock, AcquisitionMode::offline,
      directory, transportSource, git, empty);
    assert(!transportMiss.valid() && transportMiss.issues.front().code ==
      PackageContentAcquisitionIssueCode::offline_package_content_unavailable);
    assert(recording.calls.empty() && httpsExecutor.urls.empty());

    // A corrupt entry wins over an earlier missing ID in deterministic plan
    // order and all required objects are inspected before policy is applied.
    auto low = PackageContentId::parse("sha256:0000000000000000000000000000000000000000000000000000000000000000");
    auto middle = PackageContentId::parse("sha256:8888888888888888888888888888888888888888888888888888888888888888");
    auto high = PackageContentId::parse("sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    assert(low && middle && high);
    const auto precedenceLock = lock_from_nodes({
      registry_node("vix", "missing-first", *low),
      git_node("file:///local/corrupt-repository", std::string(40, 'd'), *middle),
      registry_node("vix", "missing-after-corrupt", *high)});
    MixedAvailabilityStore mixed;
    mixed.states = {{middle->digest(), PackageContentAvailability::corrupt}};
    FakeRegistrySource neverRegistry;
    FakeGitSource neverGit;
    const auto precedence = acquire_locked_package_content(precedenceLock, AcquisitionMode::offline,
      directory, neverRegistry, neverGit, mixed);
    assert(!precedence.valid() && precedence.issues.front().code ==
      PackageContentAcquisitionIssueCode::package_content_store_failure);
    assert(mixed.inspected == std::vector<std::string>({low->digest(), middle->digest(), high->digest()}));
    assert(neverRegistry.calls.empty() && neverGit.requests.empty());

    // Corruption is fail-closed in both modes; it is never repaired from a source.
    CorruptStore corrupt;
    const auto offlineCorrupt = acquire_locked_package_content(registryLock, AcquisitionMode::offline,
      directory, neverRegistry, neverGit, corrupt);
    const auto onlineCorrupt = acquire_locked_package_content(registryLock, AcquisitionMode::online,
      directory, neverRegistry, neverGit, corrupt);
    assert(!offlineCorrupt.valid() && !onlineCorrupt.valid());
    assert(offlineCorrupt.issues.front().code == PackageContentAcquisitionIssueCode::package_content_store_failure);
    assert(onlineCorrupt.issues.front().code == PackageContentAcquisitionIssueCode::package_content_store_failure);
    assert(neverRegistry.calls.empty() && neverGit.requests.empty());
    assert(serialize_lockfile(registryLock).text == registryBefore.text);
    assert(serialize_lockfile(gitLock).text == gitBefore.text);

    // A shared digest referenced by Registry and Git lock nodes remains one
    // required P10P object and is reused without selecting either source.
    const auto sharedLock = lock_from_nodes({
      registry_node("vix", "shared-registry", *registryObject.contentId),
      git_node("file:///local/shared", std::string(40, 'e'), *registryObject.contentId)});
    const auto sharedBefore = serialize_lockfile(sharedLock);
    auto shared = acquire_locked_package_content(sharedLock, AcquisitionMode::offline,
      directory, registry, git, complete);
    assert(shared.valid() && shared.entries.size() == 1);
    assert(shared.entries.front().state == PackageContentAcquisitionState::reused);
    assert(registry.calls.empty() && git.requests.empty());
    assert(serialize_lockfile(sharedLock).text == sharedBefore.text);
  }

  void offline_restart_and_multi_project_reuse_test()
  {
    const auto packageTree = tree("include/shared.hpp", "shared durable package\n");
    const auto object = encoded(packageTree);
    assert(object.valid());
    const auto projectALock = lock_from_nodes({registry_node("vix", "project-a", *object.contentId)});
    const auto projectBLock = lock_from_nodes({registry_node("vix", "project-b", *object.contentId)});
    const auto projectABefore = serialize_lockfile(projectALock);
    const auto projectBBefore = serialize_lockfile(projectBLock);
    assert(projectABefore.valid() && projectBBefore.valid());
    TestRoot mirror;
    write_bytes(mirror_object_path(mirror.path(), *object.contentId), *object.canonicalBytes);
    const auto directory = registry_directory({{"vix", {{"metadata"}}, {{file_location(mirror.path())}}}});
    TestRoot sharedStoreRoot;
    FileRegistryContentTransport fileTransport;
    RecordingTransport onlineRecording{fileTransport};
    TransportRegistryPackageContentSource onlineSource{onlineRecording};
    FakeGitSource git;
    {
      FilesystemPackageContentStore firstStore{sharedStoreRoot.path()};
      FakeRegistrySource unused;
      assert(firstStore.inspect(*object.contentId).value == PackageContentAvailability::missing);
      const auto acquired = acquire_locked_package_content(projectALock, AcquisitionMode::online,
        directory, onlineSource, git, firstStore);
      assert(acquired.valid() && acquired.entries.front().state == PackageContentAcquisitionState::acquired);
      assert(onlineRecording.calls.size() == 1);
      assert(firstStore.inspect(*object.contentId).value == PackageContentAvailability::available);
      assert(serialize_lockfile(projectALock).text == projectABefore.text);
    }

    CountingTransport forbiddenTransport;
    TransportRegistryPackageContentSource offlineSource{forbiddenTransport};
    {
      // Different project identity, reopened durable store, and offline mode.
      FilesystemPackageContentStore reopenedStore{sharedStoreRoot.path()};
      FakeRegistrySource forbiddenRegistry;
      const auto reused = acquire_locked_package_content(projectBLock, AcquisitionMode::offline,
        directory, offlineSource, git, reopenedStore);
      assert(reused.valid() && reused.entries.front().state == PackageContentAcquisitionState::reused);
      assert(forbiddenTransport.calls == 0 && forbiddenRegistry.calls.empty() && git.requests.empty());
      assert(serialize_lockfile(projectBLock).text == projectBBefore.text);
    }
  }

  void https_transport_url_status_redirect_and_limit_tests()
  {
    const std::string digest = "sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    const auto parsedId = PackageContentId::parse(digest);
    assert(parsedId);
    const auto objectPath = registry_format1_content_object_path(*parsedId);
    const std::string expectedSuffix = "/base/" + objectPath;

    ScriptedHttpsExecutor executor;
    HttpsRegistryContentTransport https{executor};
    const std::string binary{"\0\xff\n\r", 4};
    executor.replies.push_back(https_reply(200, binary, std::nullopt, binary.size()));
    auto result = https.fetch({"https://registry.example.org/base"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::available && result.bytes == binary);
    assert(executor.urls.back() == "https://registry.example.org" + expectedSuffix);

    executor.replies.push_back(https_reply(200, binary));
    result = https.fetch({"https://registry.example.org/base/"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::available);
    assert(executor.urls.back() == executor.urls[0]);

    for (const auto &bad : {"http://registry.example.org", "ftp://registry.example.org",
         "https://user:secret@example.org", "https://example.org/base?x=1",
         "https://example.org/base#fragment", "https://example.org/base?",
         "https://example.org/base#"})
    {
      const auto callsBefore = executor.urls.size();
      result = https.fetch({bad}, objectPath, 16);
      assert(result.status == RegistryContentTransportStatus::configuration_error);
      assert(result.diagnostic.find("secret") == std::string::npos);
      assert(executor.urls.size() == callsBefore);
    }

    executor.replies.push_back(https_reply(404));
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::not_found);
    executor.replies.push_back(https_reply(410));
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::not_found);
    executor.replies.push_back(https_reply(403));
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::permission_denied);
    executor.replies.push_back(https_reply(503));
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);
    executor.replies.push_back(https_reply(206, binary));
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);

    executor.replies.push_back(https_reply(302, "ignored", "https://cdn.example.org/object"));
    executor.replies.push_back(https_reply(200, binary));
    const auto beforeRedirect = executor.urls.size();
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::available && result.bytes == binary);
    assert(executor.urls.size() == beforeRedirect + 2);
    assert(executor.urls.back() == "https://cdn.example.org/object");

    executor.replies.push_back(https_reply(301, {}, "/relative/object"));
    executor.replies.push_back(https_reply(200, binary));
    result = https.fetch({"https://registry.example.org/base/"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::available);
    assert(executor.urls.back() == "https://registry.example.org/relative/object");

    executor.replies.push_back(https_reply(302, {}, "next"));
    executor.replies.push_back(https_reply(200, binary));
    result = https.fetch({"https://registry.example.org/base/"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::available);
    assert(executor.urls.back() == "https://registry.example.org/base/objects/sha256/ab/next");

    executor.replies.push_back(https_reply(302, {}, "http://downgrade.example.org/object"));
    const auto beforeDowngrade = executor.urls.size();
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);
    assert(executor.urls.size() == beforeDowngrade + 1);

    executor.replies.push_back(https_reply(302));
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);

    executor.replies.push_back(https_reply(200, binary));
    executor.replies.back().head.contentEncoding = "gzip";
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);

    for (int redirect = 0; redirect < 6; ++redirect)
      executor.replies.push_back(https_reply(302, {}, "https://redirect.example.org/" + std::to_string(redirect)));
    const auto beforeCap = executor.urls.size();
    result = https.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);
    assert(executor.urls.size() == beforeCap + 6); // initial plus five followed redirects

    executor.replies.push_back(https_reply(200, std::string(33, 'x'), std::nullopt, 33));
    const auto beforeLengthLimit = executor.urls.size();
    const auto scriptBeforeLengthLimit = executor.next;
    const auto chunksBeforeLengthLimit = executor.bodyChunksDelivered;
    result = https.fetch({"https://registry.example.org"}, objectPath, 32);
    assert(result.status == RegistryContentTransportStatus::payload_too_large);
    diagnose_content_length_accounting(executor, beforeLengthLimit,
      scriptBeforeLengthLimit, chunksBeforeLengthLimit, result.status);
    assert(executor.urls.size() == beforeLengthLimit + 1);
    assert(executor.next == scriptBeforeLengthLimit + 1);
    assert(executor.bodyChunksDelivered == chunksBeforeLengthLimit);

    auto deceptiveLength = https_reply(200, std::string(40, 'x'), std::nullopt, 1);
    deceptiveLength.chunkSize = 10;
    executor.replies.push_back(std::move(deceptiveLength));
    const auto beforeDeceptiveLength = executor.urls.size();
    const auto scriptBeforeDeceptiveLength = executor.next;
    const auto chunksBeforeDeceptiveLength = executor.bodyChunksDelivered;
    result = https.fetch({"https://registry.example.org"}, objectPath, 32);
    assert(result.status == RegistryContentTransportStatus::payload_too_large);
    diagnose_content_length_accounting(executor, beforeDeceptiveLength,
      scriptBeforeDeceptiveLength, chunksBeforeDeceptiveLength, result.status);
    assert(executor.urls.size() == beforeDeceptiveLength + 1);
    assert(executor.next == scriptBeforeDeceptiveLength + 1);

    auto streamed = https_reply(200, std::string(40, 'x'));
    streamed.chunkSize = 10;
    executor.replies.push_back(std::move(streamed));
    result = https.fetch({"https://registry.example.org"}, objectPath, 32);
    assert(result.status == RegistryContentTransportStatus::payload_too_large);

    ScriptedHttpsExecutor unavailable;
    unavailable.replies.push_back({false, {}, {}, "TLS/connection failure"});
    HttpsRegistryContentTransport failingHttps{unavailable};
    result = failingHttps.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);

    ScriptedHttpsExecutor unsupportedRedirect;
    unsupportedRedirect.replies.push_back(https_reply(302, {}, "file:///tmp/object"));
    HttpsRegistryContentTransport noOtherScheme{unsupportedRedirect};
    result = noOtherScheme.fetch({"https://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::transport_failure);
    assert(unsupportedRedirect.urls.size() == 1);

    ScriptedHttpsExecutor unknownScheme;
    SchemeRegistryContentTransport dispatcher{unknownScheme};
    result = dispatcher.fetch({"http://registry.example.org"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::configuration_error);
    result = dispatcher.fetch({"custom:descriptor"}, objectPath, 16);
    assert(result.status == RegistryContentTransportStatus::configuration_error);
    assert(unknownScheme.urls.empty());
  }

  void https_acquisition_end_to_end_tests()
  {
    const auto packageTree = tree("vix.package.toml", "name = \"demo\"\n");
    const auto object = encoded(packageTree);
    assert(object.valid());
    const auto lock = lock_from_nodes({registry_node("vix", "https-demo", *object.contentId)});
    const auto lockBefore = serialize_lockfile(lock);
    assert(lockBefore.valid());
    const auto directory = registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{"https://mirror-a.example/base"}}}});
    ScriptedHttpsExecutor executor;
    executor.replies.push_back(https_reply(200, *object.canonicalBytes, std::nullopt,
      object.canonicalBytes->size()));
    SchemeRegistryContentTransport transport{executor};
    TransportRegistryPackageContentSource source{transport};
    FakeGitSource git;
    TestRoot storeRoot;
    {
      FilesystemPackageContentStore store{storeRoot.path()};
      const auto acquired = acquire(lock, directory, source, git, store);
      assert(acquired.valid() && acquired.entries.front().state == PackageContentAcquisitionState::acquired);
      assert(store.inspect(*object.contentId).value == PackageContentAvailability::available);
      const auto digest = object.contentId->digest().substr(7);
      const auto stored = storeRoot.path() / "objects" / "sha256" / digest.substr(0, 2) / digest.substr(2);
      assert(read_bytes(stored) == *object.canonicalBytes);
      assert(serialize_lockfile(lock).text == lockBefore.text);
    }
    const auto requestCount = executor.urls.size();
    FilesystemPackageContentStore reopened{storeRoot.path()};
    const auto reused = acquire(lock, directory, source, git, reopened);
    assert(reused.valid() && reused.entries.front().state == PackageContentAcquisitionState::reused);
    assert(executor.urls.size() == requestCount);

    const auto wrongTree = tree("vix.package.toml", "name = \"other\"\n");
    const auto wrong = encoded(wrongTree);
    assert(wrong.valid() && wrong.contentId != object.contentId);
    const auto expectedWrongLock = lock_from_nodes({registry_node("vix", "https-mismatch", *wrong.contentId)});
    const auto wrongBefore = serialize_lockfile(expectedWrongLock);
    assert(wrongBefore.valid());
    // Return the canonical object whose ID differs from the locked ID; P10O must stop admission.
    ScriptedHttpsExecutor mismatchExecutor;
    mismatchExecutor.replies.push_back(https_reply(200, *object.canonicalBytes));
    SchemeRegistryContentTransport mismatchTransport{mismatchExecutor};
    TransportRegistryPackageContentSource mismatchSource{mismatchTransport};
    TestRoot mismatchRoot;
    FilesystemPackageContentStore mismatchStore{mismatchRoot.path()};
    const auto mismatchDirectory = registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{"https://mirror-a.example"}, {"https://mirror-b.example"}}}});
    const auto mismatch = acquire(expectedWrongLock, mismatchDirectory, mismatchSource, git, mismatchStore);
    assert(!mismatch.valid());
    assert(mismatch.issues.front().code == PackageContentAcquisitionIssueCode::package_content_digest_mismatch);
    assert(mismatchExecutor.urls.size() == 1);
    assert(mismatchStore.inspect(*wrong.contentId).value == PackageContentAvailability::missing);
    assert(mismatchStore.inspect(*object.contentId).value == PackageContentAvailability::missing);
    assert(serialize_lockfile(expectedWrongLock).text == wrongBefore.text);

    // Availability fallback across HTTPS mirrors remains owned by P10Q-A.
    ScriptedHttpsExecutor fallbackExecutor;
    fallbackExecutor.replies.push_back(https_reply(404));
    fallbackExecutor.replies.push_back(https_reply(200, *object.canonicalBytes));
    SchemeRegistryContentTransport fallbackTransport{fallbackExecutor};
    TransportRegistryPackageContentSource fallbackSource{fallbackTransport};
    TestRoot fallbackRoot;
    FilesystemPackageContentStore fallbackStore{fallbackRoot.path()};
    const auto fallback = acquire(lock, registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{"https://mirror-a.example"}, {"https://mirror-b.example"}}}}), fallbackSource, git, fallbackStore);
    assert(fallback.valid() && fallback.entries.front().state == PackageContentAcquisitionState::acquired);
    assert(fallbackExecutor.urls.size() == 2);
    assert(serialize_lockfile(lock).text == lockBefore.text);

    for (const int firstStatus : {404, 410, 401, 403, 408, 425, 429, 503, 0})
    {
      ScriptedHttpsExecutor statusFallback;
      if (firstStatus == 0)
        statusFallback.replies.push_back({false, {}, {}, "TLS/connection failure"});
      else
        statusFallback.replies.push_back(https_reply(firstStatus));
      statusFallback.replies.push_back(https_reply(200, *object.canonicalBytes));
      SchemeRegistryContentTransport statusTransport{statusFallback};
      TransportRegistryPackageContentSource statusSource{statusTransport};
      TestRoot statusRoot;
      FilesystemPackageContentStore statusStore{statusRoot.path()};
      const auto statusResult = acquire(lock, registry_directory({RegistryDefinition{"vix", {{"metadata"}},
        {{"https://mirror-a.example"}, {"https://mirror-b.example"}}}}), statusSource, git, statusStore);
      assert(statusResult.valid() && statusFallback.urls.size() == 2);
      assert(serialize_lockfile(lock).text == lockBefore.text);
    }

    TestRoot mixedFileRoot;
    write_bytes(mirror_object_path(mixedFileRoot.path(), *object.contentId), *object.canonicalBytes);
    ScriptedHttpsExecutor mixedExecutor;
    mixedExecutor.replies.push_back(https_reply(503));
    SchemeRegistryContentTransport mixedTransport{mixedExecutor};
    TransportRegistryPackageContentSource mixedSource{mixedTransport};
    TestRoot mixedStoreRoot;
    FilesystemPackageContentStore mixedStore{mixedStoreRoot.path()};
    const auto mixedDirectory = registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{"https://mirror-a.example"}, {file_location(mixedFileRoot.path())}}}});
    const auto mixedResult = acquire(lock, mixedDirectory, mixedSource, git, mixedStore);
    assert(mixedResult.valid() && mixedResult.entries.front().state == PackageContentAcquisitionState::acquired);
    assert(mixedExecutor.urls.size() == 1);
    assert(mixedStore.inspect(*object.contentId).value == PackageContentAvailability::available);

    // A malformed object from mirror A is an integrity failure, not a fallback condition.
    ScriptedHttpsExecutor malformedExecutor;
    malformedExecutor.replies.push_back(https_reply(200, "not a canonical P10O object"));
    SchemeRegistryContentTransport malformedTransport{malformedExecutor};
    TransportRegistryPackageContentSource malformedSource{malformedTransport};
    TestRoot malformedRoot;
    FilesystemPackageContentStore malformedStore{malformedRoot.path()};
    const auto malformed = acquire(lock, registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{"https://mirror-a.example"}, {"https://mirror-b.example"}}}}), malformedSource, git, malformedStore);
    assert(!malformed.valid() && malformedExecutor.urls.size() == 1);
    assert(serialize_lockfile(lock).text == lockBefore.text);

    // Payload limits are terminal and cannot be bypassed at another mirror.
    ScriptedHttpsExecutor largeExecutor;
    largeExecutor.replies.push_back(https_reply(200, *object.canonicalBytes));
    SchemeRegistryContentTransport largeTransport{largeExecutor};
    TransportRegistryPackageContentSource smallLimitSource{largeTransport, 1};
    TestRoot largeRoot;
    FilesystemPackageContentStore largeStore{largeRoot.path()};
    const auto tooLarge = acquire(lock, registry_directory({RegistryDefinition{"vix", {{"metadata"}},
      {{"https://mirror-a.example"}, {"https://mirror-b.example"}}}}), smallLimitSource, git, largeStore);
    assert(!tooLarge.valid() && largeExecutor.urls.size() == 1);
    assert(tooLarge.issues.front().code == PackageContentAcquisitionIssueCode::package_content_payload_too_large);
    assert(serialize_lockfile(lock).text == lockBefore.text);
  }
}

int main()
{
  reuse_and_registry_acquisition_test();
  git_acquisition_and_exact_request_test();
  digest_mismatch_and_lock_immutability_test();
  unsafe_git_symlink_rejected_by_p10o_test();
  registry_integrity_and_mirror_routing_test();
  registry_content_routing_separation_and_empty_test();
  registry_content_object_path_tests();
  local_corruption_and_all_transport_failure_test();
  mixed_sources_dedup_and_order_test();
  restart_reuse_and_provider_boundary_test();
  file_transport_contract_tests();
  file_transport_acquisition_and_fallback_tests();
  file_transport_terminal_failure_tests();
  registry_transport_failure_classification_test();
  https_transport_url_status_redirect_and_limit_tests();
  https_acquisition_end_to_end_tests();
  locked_content_acquisition_mode_tests();
  offline_restart_and_multi_project_reuse_test();
  return 0;
}
