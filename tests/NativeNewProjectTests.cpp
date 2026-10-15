#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/InitCommand.hpp>
#include <vix/cli/project/ProjectDiscovery.hpp>
#include <vix/cli/project/native/TomlParser.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vix::cli::project::native;

namespace
{
  struct CurrentPathGuard
  {
    fs::path original{fs::current_path()};
    ~CurrentPathGuard() { std::error_code ec; fs::current_path(original, ec); }
  };

  void write_text(const fs::path &path, const std::string &text)
  {
    std::ofstream output(path, std::ios::binary);
    output << text;
  }
}

int main()
{
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path root = fs::temp_directory_path() /
      ("vix-native-new-project-" + std::to_string(stamp));
  fs::create_directories(root);

  assert(vix::commands::NewCommand::run({"example", "--dir", root.string()}) == 0);
  const fs::path projectRoot = root / "example";
  assert(fs::is_regular_file(projectRoot / "vix.toml"));
  assert(fs::is_regular_file(projectRoot / "src" / "main.cpp"));
  assert(!fs::exists(projectRoot / "vix.json"));
  assert(!fs::exists(projectRoot / "vix.app"));
  assert(!fs::exists(projectRoot / "CMakeLists.txt"));
  assert(!fs::exists(projectRoot / "vix.lock"));

  const auto parsed = parse_native_manifest_file(projectRoot / "vix.toml");
  assert(parsed.valid());
  assert(parsed.value->name == "example");
  assert(parsed.value->packages.empty());
  assert(parsed.value->targets.size() == 1);
  assert(parsed.value->targets.front().name == "example");
  assert(parsed.value->targets.front().kind == TargetKind::executable);
  assert(parsed.value->targets.front().sources == std::vector<std::string>{"src/main.cpp"});

  const auto discovered = vix::cli::project::discover_project(projectRoot);
  assert(discovered.success());
  assert(discovered.location->system == vix::cli::project::ProjectSystem::native_vix);
  assert(discovered.location->root == projectRoot);

  const CurrentPathGuard pathGuard;
  const fs::path neutral = root / "adopt-existing";
  fs::create_directories(neutral);
  write_text(neutral / "keep.cpp", "int preserved = 7;\n");
  fs::current_path(neutral);
  assert(vix::commands::InitCommand::run({}) == 0);
  assert(fs::is_regular_file(neutral / "vix.toml"));
  assert(fs::is_regular_file(neutral / "keep.cpp"));
  {
    std::ifstream input(neutral / "keep.cpp", std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    assert(bytes == "int preserved = 7;\n");
  }
  assert(!fs::exists(neutral / "src"));
  const auto initialized = parse_native_manifest_file(neutral / "vix.toml");
  assert(initialized.valid() && initialized.value->targets.empty());
  const auto initializedDiscovery = vix::cli::project::discover_project(neutral);
  assert(initializedDiscovery.success());
  assert(initializedDiscovery.location->system == vix::cli::project::ProjectSystem::native_vix);

  const fs::path alreadyNative = root / "already-native";
  fs::create_directories(alreadyNative);
  write_text(alreadyNative / "vix.toml", "original bytes\n");
  fs::current_path(alreadyNative);
  assert(vix::commands::InitCommand::run({}) != 0);
  {
    std::ifstream input(alreadyNative / "vix.toml", std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    assert(bytes == "original bytes\n");
  }

  const fs::path externalCmake = root / "external-cmake";
  fs::create_directories(externalCmake);
  write_text(externalCmake / "CMakeLists.txt", "project(existing)\n");
  fs::current_path(externalCmake);
  assert(vix::commands::InitCommand::run({}) != 0);
  assert(!fs::exists(externalCmake / "vix.toml"));

  const fs::path legacyOnly = root / "legacy-only";
  fs::create_directories(legacyOnly);
  write_text(legacyOnly / "vix.json", "not read by init\n");
  write_text(legacyOnly / "vix.app", "not read by init\n");
  fs::current_path(legacyOnly);
  assert(vix::commands::InitCommand::run({}) == 0);
  assert(fs::exists(legacyOnly / "vix.json") && fs::exists(legacyOnly / "vix.app"));
  assert(parse_native_manifest_file(legacyOnly / "vix.toml").valid());

  std::error_code error;
  fs::current_path(pathGuard.original);
  fs::remove_all(root, error);
  return 0;
}
