#include <vix/cli/project/native/CanonicalHash.hpp>
#include <vix/cli/project/native/FilesystemPackageContentStore.hpp>
#include <vix/cli/project/native/PackageMaterialization.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace vix::cli::project::native;
namespace fs = std::filesystem;

namespace
{
  struct TempRoot
  {
    fs::path path;
    explicit TempRoot(std::string_view name)
      : path(fs::temp_directory_path() / ("vix-p10r-" + std::string{name} + "-" +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {}
    ~TempRoot() { std::error_code ec; fs::remove_all(path, ec); }
  };

  CanonicalPackageEntry file(std::string path, std::string_view bytes, bool executable = false)
  {
    CanonicalPackageEntry entry;
    entry.path = std::move(path);
    entry.kind = CanonicalPackageEntryKind::file;
    entry.executable = executable;
    for (const char value : bytes)
      entry.content.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
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

  CanonicalPackageContentResult canonical(const CanonicalPackageTree &tree)
  { return canonicalize_package_content_tree(tree); }

  void admit_tree(FilesystemPackageContentStore &store, const CanonicalPackageContentResult &content)
  {
    assert(content.valid() && content.contentId);
    auto candidate = std::make_shared<CanonicalPackageContentCandidate>(
      *parse_canonical_package_content(*content.canonicalBytes).tree);
    CanonicalPackageContentVerifier verifier;
    auto verified = verifier.verify(candidate, *content.contentId);
    assert(verified.valid() && verified.value);
    assert(store.admit(*verified.value).valid());
  }

  fs::path object_path(const fs::path &root, const PackageContentId &id)
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

  void write_bytes(const fs::path &path, std::string_view bytes)
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(output.good());
  }

  void exact_materialization_and_reuse_test()
  {
    TempRoot storeRoot{"exact-store"};
    TempRoot outputRoot{"exact-output"};
    FilesystemPackageContentStore store{storeRoot.path};
    CanonicalPackageTree tree{{
      file("assets/data.bin", std::string_view{"\0\xff\n\r", 4}),
      file("docs/readme.txt", "line\r\n", false),
      file("script.sh", "#!/bin/sh\n", true),
      symlink("include/demo.hpp", "../docs/readme.txt")
    }};
    const auto content = canonical(tree);
    admit_tree(store, content);

    auto result = materialize_package_content(*content.contentId, store, outputRoot.path);
    assert(result.valid() && result.state && *result.state == PackageMaterializationState::materialized);
    const auto destination = *result.path;
    assert(fs::is_directory(destination / "assets"));
    assert(read_bytes(destination / "assets/data.bin") == std::string("\0\xff\n\r", 4));
    assert(read_bytes(destination / "docs/readme.txt") == "line\r\n");
    const auto executableStatus = fs::status(destination / "script.sh");
    assert((executableStatus.permissions() & fs::perms::owner_exec) != fs::perms::none);
    assert(fs::is_symlink(fs::symlink_status(destination / "include/demo.hpp")));
    assert(fs::read_symlink(destination / "include/demo.hpp").generic_string() == "../docs/readme.txt");

    auto reused = materialize_package_content(*content.contentId, store, outputRoot.path);
    assert(reused.valid() && reused.state && *reused.state == PackageMaterializationState::reused);
  }

  void missing_and_corrupt_p10p_test()
  {
    TempRoot storeRoot{"missing-store"};
    TempRoot outputRoot{"missing-output"};
    CanonicalPackageTree tree{{file("file.txt", "payload")}};
    const auto content = canonical(tree);
    FilesystemPackageContentStore store{storeRoot.path};
    auto missing = materialize_package_content(*content.contentId, store, outputRoot.path);
    assert(!missing.valid() && !fs::exists(outputRoot.path / "packages"));
    assert(!missing.issues.empty() && !missing.issues.front().contentIssues.empty());

    admit_tree(store, content);
    write_bytes(object_path(storeRoot.path, *content.contentId), "corrupt P10P object");
    auto corrupt = materialize_package_content(*content.contentId, store, outputRoot.path);
    assert(!corrupt.valid() && !fs::exists(outputRoot.path / "packages"));
    assert(!corrupt.issues.front().contentIssues.empty());
    assert(corrupt.issues.front().contentIssues.front().code == PackageContentIssueCode::package_content_store_corrupt);
  }

  void invalid_tree_rejection_tests()
  {
    CanonicalPackageTree traversal{{file("../escape", "x")}};
    CanonicalPackageTree absolute{{file("/absolute", "x")}};
    CanonicalPackageTree prefix{{file("node", "x"), file("node/child", "y")}};
    CanonicalPackageTree unsafeLink{{symlink("link", "../../outside")}};
    assert(!canonical(traversal).valid());
    assert(!canonical(absolute).valid());
    assert(!canonical(prefix).valid());
    assert(!canonical(unsafeLink).valid());
  }

  void corrupt_destination_and_reopen_test()
  {
    TempRoot storeRoot{"restart-store"};
    TempRoot outputRoot{"restart-output"};
    CanonicalPackageTree tree{{file("nested/file.txt", "original")}};
    const auto content = canonical(tree);
    {
      FilesystemPackageContentStore store{storeRoot.path};
      admit_tree(store, content);
      auto first = materialize_package_content(*content.contentId, store, outputRoot.path);
      assert(first.valid() && first.state && *first.state == PackageMaterializationState::materialized);
      write_bytes(*first.path / "nested/file.txt", "tampered");
    }
    FilesystemPackageContentStore reopened{storeRoot.path};
    auto corruptDestination = materialize_package_content(*content.contentId, reopened, outputRoot.path);
    assert(!corruptDestination.valid());
    assert(corruptDestination.issues.front().code == PackageMaterializationErrorCode::destination_conflict);

    TempRoot projectA{"project-a"};
    TempRoot projectB{"project-b"};
    auto a = materialize_package_content(*content.contentId, reopened, projectA.path);
    auto b = materialize_package_content(*content.contentId, reopened, projectB.path);
    assert(a.valid() && b.valid());
    assert(a.path->filename() == b.path->filename());
    assert(read_bytes(*a.path / "nested/file.txt") == read_bytes(*b.path / "nested/file.txt"));
  }
}

int main()
{
  exact_materialization_and_reuse_test();
  missing_and_corrupt_p10p_test();
  invalid_tree_rejection_tests();
  corrupt_destination_and_reopen_test();
}
