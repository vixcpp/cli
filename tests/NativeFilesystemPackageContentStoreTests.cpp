#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>
#include <vix/cli/project/native/PackageContentDigest.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  class TestRoot
  {
  public:
    explicit TestRoot(std::string_view name)
    {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      path_ = fs::temp_directory_path() / ("vix-p10p-" + std::string(name) + "-" + std::to_string(stamp));
      std::error_code ec;
      fs::remove_all(path_, ec);
    }
    ~TestRoot() { std::error_code ec; fs::remove_all(path_, ec); }
    [[nodiscard]] const fs::path &path() const noexcept { return path_; }
  private:
    fs::path path_;
  };

  CanonicalPackageEntry file(std::string path, std::string_view bytes, bool executable = false)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    entry.kind = CanonicalPackageEntryKind::file;
    entry.executable = executable;
    for (const unsigned char byte : bytes) entry.content.push_back(static_cast<std::byte>(byte));
    return entry;
  }

  CanonicalPackageEntry symlink(std::string path, std::string target)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    entry.kind = CanonicalPackageEntryKind::symlink;
    entry.target = std::move(target);
    return entry;
  }

  CanonicalPackageContentResult canonical(CanonicalPackageTree tree)
  { return canonicalize_package_content_tree(tree); }

  PackageContentId id_for(std::string_view bytes)
  {
    const auto parsed = PackageContentId::parse(sha256_digest(bytes));
    assert(parsed);
    return *parsed;
  }

  fs::path object_path(const fs::path &root, const PackageContentId &id)
  {
    const auto &digest = id.digest();
    const auto hex = digest.substr(7);
    return root / "objects" / "sha256" / hex.substr(0, 2) / hex.substr(2);
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

  VerifiedPackageContent verify(const CanonicalPackageTree &tree, const PackageContentId &id)
  {
    CanonicalPackageContentVerifier verifier;
    auto result = verifier.verify(std::make_shared<CanonicalPackageContentCandidate>(tree), id);
    assert(result.valid());
    return *result.value;
  }

  Lockfile root_lock(const RegistryPackageIdentity &identity, std::string alias,
    const PackageContentId &content)
  {
    const Version version{1, 0, 0, std::nullopt};
    const auto nodeId = registry_node_id(RegistryNodeIdentity{identity, version});
    const NormalizedDeclaredRegistry declared{identity, ExactVersion{version}};
    Lockfile lock;
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{alias, declared}});
    lock.roots.push_back({std::move(alias), declared, nodeId});
    lock.nodes.emplace_back(RegistryLockNode{nodeId, identity, version, content.digest(), {}});
    return lock;
  }

  void durable_reopen_exact_payload_and_idempotence_test()
  {
    TestRoot root{"reopen"};
    const CanonicalPackageTree tree{{file("README.md", "hello\n")}};
    const auto encoded = canonical(tree);
    assert(encoded.valid());
    assert(encoded.contentId->digest() == "sha256:d633ec17ea4a23eb01253549ffee6864784f3cf934ba30c809bbd08ecaefe6b3");
    const auto token = verify(tree, *encoded.contentId);
    const auto expectedPath = object_path(root.path(), *encoded.contentId);
    {
      FilesystemPackageContentStore first{root.path()};
      assert(first.inspect(*encoded.contentId).value == PackageContentAvailability::missing);
      assert(first.admit(token).valid());
      assert(read_bytes(expectedPath) == *encoded.canonicalBytes);
      const auto before = read_bytes(expectedPath);
      assert(first.admit(token).valid());
      assert(read_bytes(expectedPath) == before);
    }
    FilesystemPackageContentStore reopened{root.path()};
    assert(reopened.inspect(*encoded.contentId).value == PackageContentAvailability::available);
  }

  void multiple_instances_and_cross_context_reuse_test()
  {
    TestRoot root{"contexts"};
    const CanonicalPackageTree tree{{file("src/shared.cpp", "int shared() {}\n")}};
    const auto result = canonical(tree);
    assert(result.valid());
    FilesystemPackageContentStore registryStore{root.path()};
    FilesystemPackageContentStore gitStore{root.path()};
    assert(registryStore.admit(verify(tree, *result.contentId)).valid());
    assert(gitStore.inspect(*result.contentId).value == PackageContentAvailability::available);
    assert(gitStore.admit(verify(tree, *result.contentId)).valid());
    assert(registryStore.inspect(*result.contentId).value == PackageContentAvailability::available);
    const auto projectA = root_lock(RegistryPackageIdentity{"vix", "team-a", "shared"}, "left", *result.contentId);
    const auto projectB = root_lock(RegistryPackageIdentity{"acme", "team-b", "shared"}, "right", *result.contentId);
    assert(check_package_content_availability(projectA, registryStore).complete);
    assert(check_package_content_availability(projectB, gitStore).complete);
    assert(fs::exists(object_path(root.path(), *result.contentId)));
  }

  void missing_and_corruption_distinction_test()
  {
    TestRoot root{"corruption"};
    FilesystemPackageContentStore store{root.path()};
    const auto good = canonical({{file("a", "complete payload")}});
    assert(good.valid());
    const auto absent = id_for("another canonical object");
    assert(store.inspect(absent).value == PackageContentAvailability::missing);

    const auto token = verify({{file("a", "complete payload")}}, *good.contentId);
    assert(store.admit(token).valid());
    const auto path = object_path(root.path(), *good.contentId);
    write_bytes(path, good.canonicalBytes->substr(0, good.canonicalBytes->size() - 2));
    const auto truncated = store.inspect(*good.contentId);
    assert(!truncated.valid());
    assert(truncated.value == PackageContentAvailability::corrupt);
    assert(truncated.issues.size() == 1 && truncated.issues.front().code == PackageContentIssueCode::package_content_store_corrupt);

    const auto admitted = store.admit(token);
    assert(!admitted.valid());
    assert(admitted.issues.front().code == PackageContentIssueCode::package_content_store_corrupt);
    assert(read_bytes(path) == good.canonicalBytes->substr(0, good.canonicalBytes->size() - 2));
  }

  void wrong_object_and_bit_flip_test()
  {
    TestRoot root{"wrong-id"};
    FilesystemPackageContentStore store{root.path()};
    const auto a = canonical({{file("a", "A")}});
    const auto b = canonical({{file("b", "B")}});
    assert(a.valid() && b.valid() && a.contentId != b.contentId);
    write_bytes(object_path(root.path(), *b.contentId), *a.canonicalBytes);
    auto inspection = store.inspect(*b.contentId);
    assert(inspection.value == PackageContentAvailability::corrupt);
    assert(inspection.issues.front().code == PackageContentIssueCode::package_content_store_corrupt);

    const auto pathA = object_path(root.path(), *a.contentId);
    write_bytes(pathA, *a.canonicalBytes);
    auto corrupted = *a.canonicalBytes;
    corrupted.back() ^= 1;
    write_bytes(pathA, corrupted);
    inspection = store.inspect(*a.contentId);
    assert(inspection.value == PackageContentAvailability::corrupt);
  }

  void malformed_self_hashed_objects_test()
  {
    TestRoot root{"noncanonical"};
    FilesystemPackageContentStore store{root.path()};
    const auto base = canonical({{file("a", "x"), file("b", "y")}});
    assert(base.valid());
    std::vector<std::string> invalid{
      "wrong-domain\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=01\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=01:a\ntype=4:file\nexecutable=1:0\ncontent=1:x\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=1:a\ntype=4:file\nexecutable=1:0\ncontent=999999999999999999999999:x\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=1:a\ntype=4:file\nexecutable=1:0\ncontent=2:x\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=1:a\ntype=8:unknown\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=1:a\ntype=4:file\nexecutable=1:2\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=2\nentry\npath=1:a\ntype=4:file\nexecutable=1:0\ncontent=1:x\nentry\npath=1:a\ntype=4:file\nexecutable=1:0\ncontent=1:y\n",
      "VIX-PACKAGE-CONTENT-v1\nentry-count=2\nentry\npath=1:a\ntype=4:file\nexecutable=1:0\ncontent=1:x\nentry\npath=3:a/b\ntype=4:file\nexecutable=1:0\ncontent=1:y\n",
      *base.canonicalBytes + "garbage",
    };
    auto reversed = *base.canonicalBytes;
    const auto firstEntry = reversed.find("entry\n", reversed.find('\n') + 1);
    const auto secondEntry = reversed.find("entry\n", firstEntry + 1);
    const auto endSecond = reversed.find("content=1:y\n", secondEntry) + std::string("content=1:y\n").size();
    const auto firstRecord = reversed.substr(firstEntry, secondEntry - firstEntry);
    const auto secondRecord = reversed.substr(secondEntry, endSecond - secondEntry);
    reversed.replace(firstEntry, endSecond - firstEntry, secondRecord + firstRecord);
    invalid.push_back(std::move(reversed));

    for (const auto &bytes : invalid)
    {
      const auto id = id_for(bytes);
      write_bytes(object_path(root.path(), id), bytes);
      const auto inspection = store.inspect(id);
      assert(inspection.value == PackageContentAvailability::corrupt);
      assert(inspection.issues.front().code == PackageContentIssueCode::package_content_store_corrupt);
    }

    const auto malformedPaths = canonical({{file("bad/../path", "x")}});
    assert(!malformedPaths.valid());
    const std::string leadingLength = "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=09:README.md\ntype=4:file\nexecutable=1:0\ncontent=6:hello\n\n";
    const auto pathId = id_for(leadingLength);
    write_bytes(object_path(root.path(), pathId), leadingLength);
    assert(store.inspect(pathId).value == PackageContentAvailability::corrupt);

    std::string nulPath = "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=3:a";
    nulPath.push_back('\0');
    nulPath += "b\ntype=4:file\nexecutable=1:0\ncontent=0:\n";
    const auto nulId = id_for(nulPath);
    write_bytes(object_path(root.path(), nulId), nulPath);
    assert(store.inspect(nulId).value == PackageContentAvailability::corrupt);
  }

  void temporary_and_symlink_object_test()
  {
    TestRoot root{"temp-symlink"};
    FilesystemPackageContentStore store{root.path()};
    const auto object = canonical({{file("a", "payload")}});
    assert(object.valid());
    auto path = object_path(root.path(), *object.contentId);
    fs::create_directories(path.parent_path());
    fs::path temporary = path;
    temporary += ".tmp";
    write_bytes(temporary, *object.canonicalBytes);
    assert(store.inspect(*object.contentId).value == PackageContentAvailability::missing);
    std::error_code ec;
    fs::remove(temporary, ec);
    const auto other = root.path() / "outside-object";
    write_bytes(other, *object.canonicalBytes);
    fs::create_symlink(other, path, ec);
    if (!ec)
    {
      const auto inspection = store.inspect(*object.contentId);
      assert(inspection.value == PackageContentAvailability::corrupt);
    }

    const auto directoryId = id_for("directory object");
    fs::create_directories(object_path(root.path(), directoryId));
    const auto directoryInspection = store.inspect(directoryId);
    assert(directoryInspection.value == PackageContentAvailability::corrupt);

    TestRoot managedRoot{"managed-link"};
    auto outside = managedRoot.path();
    outside += "-outside";
    fs::create_directories(outside);
    std::error_code managedError;
    fs::create_symlink(outside, managedRoot.path() / "objects", managedError);
    if (!managedError)
    {
      FilesystemPackageContentStore managedStore{managedRoot.path()};
      const auto rejected = managedStore.inspect(*object.contentId);
      assert(!rejected.valid());
      assert(rejected.issues.front().code == PackageContentIssueCode::package_content_store_io_error);
    }
    std::error_code cleanupError;
    fs::remove_all(outside, cleanupError);
  }

  void canonical_roundtrip_and_lock_completeness_test()
  {
    TestRoot root{"roundtrip-lock"};
    auto binary = file("bin/data", "", true);
    binary.content = {std::byte{0x00}, std::byte{0xff}};
    CanonicalPackageTree tree{{file("README.md", "hello\n"), std::move(binary),
      symlink("include/current.hpp", "a.hpp")}};
    const auto encoded = canonical(tree);
    assert(encoded.valid());
    const auto parsed = parse_canonical_package_content(*encoded.canonicalBytes);
    assert(parsed.valid());
    const auto roundtrip = canonical(*parsed.tree);
    assert(roundtrip.valid() && roundtrip.canonicalBytes == encoded.canonicalBytes);

    RegistryPackageIdentity identity{"vix", "example", "demo"};
    const Version version{1, 0, 0, std::nullopt};
    const auto nodeId = registry_node_id(RegistryNodeIdentity{identity, version});
    const NormalizedDeclaredRegistry declared{identity, ExactVersion{version}};
    Lockfile lock;
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{"demo", declared}});
    lock.roots.push_back({"demo", declared, nodeId});
    lock.nodes.emplace_back(RegistryLockNode{nodeId, identity, version, encoded.contentId->digest(), {}});
    assert(validate_lockfile(lock).valid() && validate_lockfile_integrity(lock).valid());
    const auto before = serialize_lockfile(lock);
    assert(before.valid());

    FilesystemPackageContentStore store{root.path()};
    assert(store.admit(verify(tree, *encoded.contentId)).valid());
    const auto availability = check_package_content_availability(lock, store);
    assert(availability.valid() && availability.complete);
    const auto after = serialize_lockfile(lock);
    assert(after.valid() && after.text == before.text);
  }

  void persistent_completeness_and_corruption_test()
  {
    TestRoot root{"completeness"};
    const CanonicalPackageTree treeX{{file("x", "X")}};
    const CanonicalPackageTree treeY{{file("y", "Y")}};
    const CanonicalPackageTree treeZ{{file("z", "Z")}};
    const auto x = canonical(treeX);
    const auto y = canonical(treeY);
    const auto z = canonical(treeZ);
    assert(x.valid() && y.valid() && z.valid());

    RegistryPackageIdentity aIdentity{"vix", "example", "a"};
    RegistryPackageIdentity bIdentity{"vix", "example", "b"};
    RegistryPackageIdentity cIdentity{"vix", "example", "c"};
    const Version version{1, 0, 0, std::nullopt};
    RegistryLockNode a{registry_node_id(RegistryNodeIdentity{aIdentity, version}), aIdentity, version,
      x.contentId->digest(), {}};
    RegistryLockNode b{registry_node_id(RegistryNodeIdentity{bIdentity, version}), bIdentity, version,
      y.contentId->digest(), {}};
    RegistryLockNode c{registry_node_id(RegistryNodeIdentity{cIdentity, version}), cIdentity, version,
      z.contentId->digest(), {}};
    a.dependencies = {{"b", b.id}, {"c", c.id}};
    const NormalizedDeclaredRegistry declared{aIdentity, ExactVersion{version}};
    Lockfile lock;
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{"a", declared}});
    lock.roots.push_back({"a", declared, a.id});
    lock.nodes.emplace_back(a);
    lock.nodes.emplace_back(b);
    lock.nodes.emplace_back(c);
    assert(validate_lockfile(lock).valid() && validate_lockfile_integrity(lock).valid());
    const auto lockBefore = serialize_lockfile(lock);
    assert(lockBefore.valid());

    FilesystemPackageContentStore store{root.path()};
    assert(store.admit(verify(treeX, *x.contentId)).valid());
    assert(store.admit(verify(treeZ, *z.contentId)).valid());
    auto report = check_package_content_availability(lock, store);
    assert(report.valid() && !report.complete && report.missing.size() == 1);
    assert(report.missing.front().id == *y.contentId);
    assert(report.missing.front().nodeIds == std::vector<std::string>{b.id});

    auto bytes = read_bytes(object_path(root.path(), *x.contentId));
    bytes[bytes.size() - 2] ^= 1;
    write_bytes(object_path(root.path(), *x.contentId), bytes);
    report = check_package_content_availability(lock, store);
    assert(!report.valid() && !report.complete);
    assert(std::none_of(report.missing.begin(), report.missing.end(),
      [&](const auto &missing) { return missing.id == *x.contentId; }));
    assert(std::any_of(report.issues.begin(), report.issues.end(), [&](const auto &issue)
      { return issue.code == PackageContentIssueCode::package_content_store_corrupt && issue.digest == x.contentId->digest(); }));
    assert(serialize_lockfile(lock).text == lockBefore.text);
  }
}

int main()
{
  static_assert(!std::is_default_constructible_v<VerifiedPackageContent>);
  durable_reopen_exact_payload_and_idempotence_test();
  multiple_instances_and_cross_context_reuse_test();
  missing_and_corruption_distinction_test();
  wrong_object_and_bit_flip_test();
  malformed_self_hashed_objects_test();
  temporary_and_symlink_object_test();
  canonical_roundtrip_and_lock_completeness_test();
  persistent_completeness_and_corruption_test();
  return 0;
}
