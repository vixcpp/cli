#include <vix/cli/util/Hash.hpp>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void write_file(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << content;
}

static int run(const std::string &cmd)
{
  return std::system(cmd.c_str());
}

static std::string quote(const fs::path &path)
{
  std::string s = path.string();
  std::string out = "'";
  for (char c : s)
  {
    if (c == '\'')
      out += "'\\''";
    else
      out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

static fs::path make_root()
{
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path root = fs::temp_directory_path() / ("vix-package-hash-test-" + std::to_string(now));
  fs::remove_all(root);
  fs::create_directories(root);
  return root;
}

static fs::path make_git_repo(const fs::path &root)
{
  fs::path repo = root / "repo";
  fs::create_directories(repo);
  write_file(repo / "include" / "pkg.hpp", "#pragma once\ninline int pkg() { return 42; }\n");
  write_file(repo / "src" / "pkg.cpp", "int pkg_impl() { return 42; }\n");
  write_file(repo / ".github" / "workflows" / "ci.yml", "name: ci\n");
  write_file(repo / "build" / "generated.txt", "ignored generated file\n");
  write_file(repo / ".vix" / "state.txt", "ignored vix state\n");

  assert(run("git -C " + quote(repo) + " init -q") == 0);
  assert(run("git -C " + quote(repo) + " config user.email test@example.invalid") == 0);
  assert(run("git -C " + quote(repo) + " config user.name 'Vix Test'") == 0);
  assert(run("git -C " + quote(repo) + " add include src .github") == 0);
  assert(run("git -C " + quote(repo) + " commit -q -m init") == 0);
  return repo;
}

static void test_reproducible_hash()
{
  fs::path root = make_root();
  fs::path repo = make_git_repo(root);
  auto a = vix::cli::util::sha256_package_directory(repo);
  auto b = vix::cli::util::sha256_package_directory(repo);
  assert(a.has_value());
  assert(b.has_value());
  assert(*a == *b);
  fs::remove_all(root);
}

static void test_git_metadata_and_untracked_files_are_excluded()
{
  fs::path root = make_root();
  fs::path repo = make_git_repo(root);
  const auto before = vix::cli::util::sha256_package_directory(repo);
  assert(before.has_value());

  write_file(repo / ".git" / "vix-test-metadata", "local git metadata changes must not affect package hash\n");
  write_file(repo / "untracked.txt", "untracked files must not affect package hash\n");
  write_file(repo / "build" / "new-generated.txt", "generated files must not affect package hash\n");
  write_file(repo / ".vix" / "new-state.txt", "project state must not affect package hash\n");

  const auto after = vix::cli::util::sha256_package_directory(repo);
  assert(after.has_value());
  assert(*before == *after);
  fs::remove_all(root);
}

static void test_tracked_file_modification_changes_hash()
{
  fs::path root = make_root();
  fs::path repo = make_git_repo(root);
  const auto before = vix::cli::util::sha256_package_directory(repo);
  assert(before.has_value());

  write_file(repo / "src" / "pkg.cpp", "int pkg_impl() { return 43; }\n");

  const auto after = vix::cli::util::sha256_package_directory(repo);
  assert(after.has_value());
  assert(*before != *after);
  fs::remove_all(root);
}

int main()
{
  test_reproducible_hash();
  test_git_metadata_and_untracked_files_are_excluded();
  test_tracked_file_modification_changes_hash();
  std::cout << "PackageHashTests passed\n";
  return 0;
}
