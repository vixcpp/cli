#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/PackageContentDigest.hpp>
#include <vix/cli/project/native/Validation.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace vix::cli::project::native;

namespace
{
  CanonicalPackageEntry file(std::string path, std::string_view bytes, bool executable = false)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    entry.kind = CanonicalPackageEntryKind::file;
    entry.executable = executable;
    for (const unsigned char byte : bytes) entry.content.push_back(static_cast<std::byte>(byte));
    return entry;
  }

  CanonicalPackageEntry link(std::string path, std::string target)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    entry.kind = CanonicalPackageEntryKind::symlink;
    entry.target = std::move(target);
    return entry;
  }

  CanonicalPackageContentResult digest(CanonicalPackageTree tree)
  { return canonicalize_package_content_tree(tree); }

  bool has_code(const CanonicalPackageContentResult &result, CanonicalPackageContentIssueCode code)
  {
    return std::any_of(result.issues.begin(), result.issues.end(),
      [code](const auto &issue) { return issue.code == code; });
  }

  void frozen_vector_tests()
  {
    const auto empty = digest({});
    assert(empty.valid());
    assert(empty.canonicalBytes == "VIX-PACKAGE-CONTENT-v1\nentry-count=0\n");
    assert(empty.contentId->digest() == "sha256:9b059bd13a4a359dbbe59af8a5f21e7cbad75537d026f3d596552340ff15ee6e");

    CanonicalPackageTree single{{file("README.md", "hello\n")}};
    const auto one = digest(single);
    assert(one.valid());
    assert(one.contentId->digest() == "sha256:d633ec17ea4a23eb01253549ffee6864784f3cf934ba30c809bbd08ecaefe6b3");
    assert(one.canonicalBytes == "VIX-PACKAGE-CONTENT-v1\nentry-count=1\nentry\npath=9:README.md\n"
      "type=4:file\nexecutable=1:0\ncontent=6:hello\n\n");

    CanonicalPackageTree fileAndLink{{file("include/a.hpp", "#pragma once\n"), link("include/current.hpp", "a.hpp")}};
    const auto linked = digest(fileAndLink);
    assert(linked.valid());
    assert(linked.contentId->digest() == "sha256:7cbbb75b21dc387ca40e1305ee1a2a2910bbed20dbce546e32b4c83a4d43d8b3");

    auto binaryEntry = file("bin.dat", "");
    binaryEntry.content = {std::byte{0x00}, std::byte{0xff}, std::byte{0x0a}};
    CanonicalPackageTree binary{{std::move(binaryEntry)}};
    const auto binaryResult = digest(binary);
    assert(binaryResult.valid());
    assert(binaryResult.contentId->digest() == "sha256:b17df7f4477641653766ecf5ea571f17f6d5c99fc9538f2da8d796a26b7ddc00");

    const auto nonExecutable = digest({{file("script.sh", "echo hi\n", false)}});
    const auto executable = digest({{file("script.sh", "echo hi\n", true)}});
    assert(nonExecutable.valid() && executable.valid());
    assert(nonExecutable.contentId->digest() == "sha256:e6217611e3a2202ebddfdcb4cb2ccd0335c8d48ea2529c080beb1d6e1a7d80a4");
    assert(executable.contentId->digest() == "sha256:5a9b020d62e4f32ff4e3acbcbce427cab26195c56d56e5f7b1a2ee0d1ab4b224");
    assert(nonExecutable.contentId != executable.contentId);

    const auto lf = digest({{file("line.txt", "a\n")}});
    const auto crlf = digest({{file("line.txt", "a\r\n")}});
    assert(lf.valid() && crlf.valid());
    assert(lf.contentId->digest() == "sha256:5d4c2a8bc88d5562e359c62a4903e3f4d369144c2a914e78a672a28b63a8ef08");
    assert(crlf.contentId->digest() == "sha256:879b82cc12cc3651072be9681deaa400e4f11f99b7dd7a860295e1878bde9b6b");
    assert(lf.contentId != crlf.contentId);
  }

  void ordering_and_semantic_field_tests()
  {
    const auto first = digest({{file("z.hpp", "z"), file("a.hpp", "a"), file("é.hpp", "u")}});
    const auto reordered = digest({{file("é.hpp", "u"), file("z.hpp", "z"), file("a.hpp", "a")}});
    assert(first.valid() && reordered.valid());
    assert(first.canonicalBytes == reordered.canonicalBytes);
    assert(first.contentId == reordered.contentId);
    const auto a = first.canonicalBytes->find("path=5:a.hpp\n");
    const auto z = first.canonicalBytes->find("path=5:z.hpp\n");
    const auto nonAscii = first.canonicalBytes->find("path=6:é.hpp\n");
    assert(a < z && z < nonAscii);

    const auto composed = digest({{file("é.hpp", "x")}});
    const auto decomposed = digest({{file("e\xcc\x81.hpp", "x")}});
    assert(composed.valid() && decomposed.valid());
    assert(composed.contentId != decomposed.contentId);

    assert(digest({{file("a.hpp", "same")}}).contentId != digest({{file("b.hpp", "same")}}).contentId);
    assert(digest({{file("a.hpp", "x")}}).contentId != digest({{file("a.hpp", "y")}}).contentId);

    const auto packageContract = digest({{file("vix.package.toml", "name='one'\n")}});
    const auto changedContract = digest({{file("vix.package.toml", "name='two'\n")}});
    assert(packageContract.valid() && changedContract.valid());
    assert(packageContract.contentId != changedContract.contentId);

    // The canonical model has no directory, time, owner, mode, source, or root
    // fields: only leaf path/type/content and executable state are representable.
    static_assert(std::is_same_v<decltype(CanonicalPackageEntry::executable), bool>);
    static_assert(std::is_same_v<decltype(CanonicalPackageEntry::content), std::vector<std::byte>>);
  }

  void source_transport_and_root_independence_test()
  {
    struct AcquisitionContext
    {
      std::string source;
      std::string transport;
      std::string outerRoot;
      std::string archiveName;
    };
    const AcquisitionContext registry{"registry", "zip", "/tmp/download-A/pkg", "pkg.zip"};
    const AcquisitionContext git{"git", "git-object-tree", "/home/cache/checkout-B/other-name", ""};
    const CanonicalPackageTree logicalTree{{file("include/api.hpp", "#pragma once\n"),
      file("src/api.cpp", "int api() {}\n")}};
    const auto registryContent = digest(logicalTree);
    const auto gitContent = digest(logicalTree);
    assert(registry.source != git.source && registry.transport != git.transport);
    assert(registry.outerRoot != git.outerRoot && registry.archiveName != git.archiveName);
    assert(registryContent.valid() && gitContent.valid());
    assert(registryContent.contentId == gitContent.contentId);

    CanonicalPackageContentVerifier verifier;
    InMemoryPackageContentStore store;
    const auto registryVerified = verifier.verify(
      std::make_shared<CanonicalPackageContentCandidate>(logicalTree), *registryContent.contentId);
    const auto gitVerified = verifier.verify(
      std::make_shared<CanonicalPackageContentCandidate>(logicalTree), *gitContent.contentId);
    assert(registryVerified.valid() && gitVerified.valid());
    assert(store.admit(*registryVerified.value).valid());
    assert(store.admit(*gitVerified.value).valid());
    assert(store.size() == 1);
  }

  void invalid_tree_tests()
  {
    const std::vector<std::string> invalidPaths{
      "/absolute", "", ".", "..", "a/../b", "a//b", "a/", std::string("a\0b", 3), std::string("\xc0\xaf", 2)};
    for (const auto &path : invalidPaths)
      assert(!digest({{file(path, "x")}}).valid());
    assert(has_code(digest({{file(std::string("\xc0\xaf", 2), "x")}}), CanonicalPackageContentIssueCode::invalid_utf8));

    const auto duplicate = digest({{file("same", "a"), file("same", "b")}});
    assert(!duplicate.valid() && has_code(duplicate, CanonicalPackageContentIssueCode::duplicate_package_content_path));
    const auto fileParent = digest({{file("a", "x"), file("a/b.cpp", "y")}});
    assert(!fileParent.valid() && has_code(fileParent, CanonicalPackageContentIssueCode::package_content_tree_conflict));
    const auto linkParent = digest({{link("include", "target"), file("include/a.hpp", "x")}});
    assert(!linkParent.valid() && has_code(linkParent, CanonicalPackageContentIssueCode::package_content_tree_conflict));

    assert(digest({{link("include/current.hpp", "a.hpp")}}).valid());
    assert(digest({{link("include/current.hpp", "../a.hpp")}}).valid());
    assert(!digest({{link("link", "/etc/passwd")}}).valid());
    assert(!digest({{link("link", "C:/outside")}}).valid());
    assert(!digest({{link("include/link", "../../outside")}}).valid());
    assert(!digest({{link("link", "")}}).valid());
    assert(!digest({{link("link", std::string("bad\0target", 10))}}).valid());
    assert(!digest({{link("link", std::string("\xc0\xaf", 2))}}).valid());
    assert(digest({{link("include/dangling", "missing.hpp")}}).valid());
    assert(digest({{link("include/link", "a.hpp")}}).contentId !=
      digest({{link("include/link", "./a.hpp")}}).contentId);
    assert(digest({{link("first", "second"), link("second", "first")}}).valid());

    CanonicalPackageEntry unsupported = file("device", "");
    unsupported.kind = static_cast<CanonicalPackageEntryKind>(99);
    const auto unsupportedResult = digest({{std::move(unsupported)}});
    assert(!unsupportedResult.valid());
    assert(has_code(unsupportedResult, CanonicalPackageContentIssueCode::unsupported_package_content_entry));
  }

  void verifier_and_store_integration_test()
  {
    CanonicalPackageContentVerifier verifier;
    InMemoryPackageContentStore store;
    const CanonicalPackageTree tree{{file("src/main.cpp", "int main() {}\n")}};
    const auto canonical = digest(tree);
    assert(canonical.valid());
    auto candidate = std::make_shared<CanonicalPackageContentCandidate>(tree);
    const auto accepted = verifier.verify(candidate, *canonical.contentId);
    assert(accepted.valid());
    assert(accepted.value->id() == *canonical.contentId);
    assert(store.admit(*accepted.value).valid());
    assert(store.inspect(*canonical.contentId).value == PackageContentAvailability::available);

    const auto wrong = PackageContentId::parse("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(wrong && *wrong != *canonical.contentId);
    const auto rejected = verifier.verify(candidate, *wrong);
    assert(!rejected.valid());
    assert(rejected.issues.front().code == PackageContentIssueCode::package_content_verification_failed);
    assert(store.inspect(*wrong).value == PackageContentAvailability::missing);
    assert(store.size() == 1);

    static_assert(!std::is_default_constructible_v<VerifiedPackageContent>);
    static_assert(!std::is_constructible_v<VerifiedPackageContent, PackageContentId,
      std::shared_ptr<const PackageContentCandidate>, std::string>);
  }

  void lock_digest_compatibility_test()
  {
    const auto canonical = digest({{file("vix.package.toml", "name='demo'\n")}});
    assert(canonical.valid());
    RegistryPackageIdentity identity{"vix", "example", "demo"};
    const Version version{1, 0, 0, std::nullopt};
    const auto id = registry_node_id(RegistryNodeIdentity{identity, version});
    const NormalizedDeclaredRegistry declared{identity, ExactVersion{version}};
    Lockfile lock;
    lock.rootRequirementsDigest = root_requirements_digest({RootRequirementDigestEntry{"demo", declared}});
    lock.roots.push_back({"demo", declared, id});
    lock.nodes.emplace_back(RegistryLockNode{id, identity, version, canonical.contentId->digest(), {}});
    assert(id != canonical.contentId->digest());
    assert(validate_lockfile(lock).valid());
    assert(validate_lockfile_integrity(lock).valid());
    const auto before = serialize_lockfile(lock);
    assert(before.valid());
    const auto plan = required_package_content(lock);
    assert(plan.valid() && plan.required.size() == 1);
    assert(plan.required.front().id == *canonical.contentId);
    assert(serialize_lockfile(lock).text == before.text);
  }
}

int main()
{
  frozen_vector_tests();
  ordering_and_semantic_field_tests();
  source_transport_and_root_independence_test();
  invalid_tree_tests();
  verifier_and_store_integration_test();
  lock_digest_compatibility_test();
  return 0;
}
