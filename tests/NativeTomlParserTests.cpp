#include <vix/cli/project/native/TomlParser.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

using namespace vix::cli::project::native;

namespace
{
  template <typename T>
  bool has_error(const ParseResult<T> &result, ParseErrorCode code, std::string_view path = {})
  {
    for (const auto &entry : result.issues)
      if (entry.code == code && (path.empty() || entry.fieldPath == path)) return true;
    return false;
  }

  constexpr std::string_view minimal = R"toml(format = 1
[project]
name = "demo"
[[targets]]
name = "app"
kind = "executable"
sources = ["main.cpp"]
)toml";

  constexpr std::string_view goodLock = R"toml(format = 1
root-requirements-digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
[[roots]]
alias = "fmt"
declared = { source = "registry", registry = "vix", publisher = "fmtlib", name = "fmt", compatible = "11.0.0" }
node = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
[[nodes]]
id = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
source = "registry"
registry = "vix"
publisher = "fmtlib"
name = "fmt"
version = "11.1.2"
digest = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
)toml";

  std::string lock_with_declared(std::string_view declared)
  {
    return "format=1\nroot-requirements-digest='sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'\n"
      "[[roots]]\nalias='pkg'\ndeclared={" + std::string{declared} + "}\nnode='node'\n"
      "[[nodes]]\nid='node'\nsource='git'\nrepository='https://example.invalid/r'\n"
      "commit='0123456789abcdef0123456789abcdef01234567'\n"
      "digest='sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'\n";
  }

  void manifest_tests()
  {
    const auto parsed = parse_native_manifest_text(minimal);
    assert(parsed.valid());
    assert(validate_native_project(*parsed.value).valid());

    const auto complete = parse_native_manifest_text(R"toml(format = 1
[project]
name = "demo"
cxx-standard = "c++23"
[[packages]]
alias = "fmt"
source = "registry"
registry = "vix"
publisher = "fmtlib"
name = "fmt"
requirement = { exact = "1.2.3-beta.2" }
[[packages]]
alias = "proto"
source = "git"
repository = "https://example.invalid/proto.git"
revision = { branch = "main" }
[[targets]]
name = "core"
kind = "static"
sources = ["core.cpp"]
[targets.includes]
public = ["include"]
[targets.uses]
private = []
[targets.definitions]
public = ["API_VERSION=3"]
[[targets.package-uses]]
package = "fmt"
target = "fmt"
visibility = "public"
[[targets]]
name = "shared"
kind = "shared"
sources = ["shared.cpp"]
[[targets]]
name = "api"
kind = "interface"
[targets.includes]
interface = ["include"]
[[tests]]
name = "repeated"
target = "app"
[[tests]]
name = "repeated"
target = "app"
)toml");
    assert(complete.valid() && complete.value->targets.size() == 3);
    assert(complete.value->tests.size() == 2 && complete.value->packages.size() == 2);

    assert(has_error(parse_native_manifest_text("[project]\nname='x'"), ParseErrorCode::missing_field, "format"));
    assert(has_error(parse_native_manifest_text("format=2\n[project]\nname='x'"), ParseErrorCode::unsupported_format));
    assert(has_error(parse_native_manifest_text("format='1'\n[project]\nname='x'"), ParseErrorCode::wrong_type));
    assert(has_error(parse_native_manifest_text("format=1.0\n[project]\nname='x'"), ParseErrorCode::wrong_type));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\nextra=1"), ParseErrorCode::unknown_field, "project.extra"));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[targets]]\nname='app'\nkind='executable'\nsources=['main.cpp']\nextra=1"), ParseErrorCode::unknown_field, "targets[0].extra"));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[targets]]\nname='app'\nkind='exe'"), ParseErrorCode::invalid_value, "targets[0].kind"));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[targets]]\nname='app'\nkind='executable'\nsources='main.cpp'"), ParseErrorCode::wrong_type));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[targets]]\nname='app'\nkind='executable'\nsources=['main.cpp']\n[targets.includes]\nextra=[]"), ParseErrorCode::unknown_field));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[targets]]\nname='app'\nkind='executable'\nsources=['main.cpp']\n[[targets.package-uses]]\npackage='x'\ntarget='x'\nvisibility='bogus'"), ParseErrorCode::invalid_value));

    for (const auto mode : {"exact", "compatible"})
    {
      auto declaration = "alias='fmt'\nsource='registry'\nregistry='vix'\npublisher='fmtlib'\nname='fmt'\nrequirement={" + std::string(mode) + "='1.2.3'}";
      assert(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[packages]]\n" + declaration + "\n[[targets]]\nname='a'\nkind='executable'\nsources=['a.cpp']").valid());
    }
    for (const auto mode : {"commit", "tag", "branch"})
      assert(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[packages]]\nalias='proto'\nsource='git'\nrepository='https://example.invalid/r'\nrevision={" + std::string(mode) + "='rev'}\n[[targets]]\nname='a'\nkind='executable'\nsources=['a.cpp']").valid());
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[packages]]\nalias='fmt'\nsource='registry'\nregistry='vix'\npublisher='fmtlib'\nname='fmt'\nrequirement={}\n[[targets]]\nname='a'\nkind='executable'\nsources=['a.cpp']"), ParseErrorCode::invalid_discriminated_shape));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[packages]]\nalias='fmt'\nsource='registry'\nregistry='vix'\npublisher='fmtlib'\nname='fmt'\nrequirement={exact='1.2.3',compatible='1.2.3'}\n[[targets]]\nname='a'\nkind='executable'\nsources=['a.cpp']"), ParseErrorCode::invalid_discriminated_shape));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[packages]]\nalias='git'\nsource='git'\nrepository='r'\nregistry='vix'\nrevision={tag='v1'}\n[[targets]]\nname='a'\nkind='executable'\nsources=['a.cpp']"), ParseErrorCode::unknown_field));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[packages]]\nalias='git'\nsource='git'\nrepository='r'\n[[targets]]\nname='a'\nkind='executable'\nsources=['a.cpp']"), ParseErrorCode::missing_field));
    assert(has_error(parse_native_manifest_text("format=1\n[project]\nname='x'\n[[packages]]\nalias='fmt'\nsource='registry'\nregistry='vix'\npublisher='fmtlib'\nname='fmt'\nrevision={tag='v1'}\nrequirement={exact='1.2.3'}\n[[targets]]\nname='a'\nkind='executable'\nsources=['a.cpp']"), ParseErrorCode::unknown_field));
  }

  void contract_tests()
  {
    const auto native = parse_package_contract_text(R"toml(format = 1
[package]
source = "registry"
registry = "vix"
publisher = "example"
name = "netkit"
version = "1.2.0"
[integration]
type = "native-vix"
include-roots = ["include"]
[[exports]]
name = "core"
project-target = "internal-core"
)toml");
    assert(native.valid() && validate_package_contract(*native.value).valid());
    assert(parse_package_contract_text("format=1\n[package]\nsource='git'\n[integration]\ntype='native-vix'\ninclude-roots=[]\n[[exports]]\nname='api'\nproject-target='api'").valid());
    assert(parse_package_contract_text("format=1\n[package]\nsource='registry'\nregistry='vix'\npublisher='example'\nname='fmt'\nversion='1.0.0'\n[integration]\ntype='cmake-config'\nconfig-name='fmt'\n[[exports]]\nname='fmt'\ncmake-target='fmt::fmt'").valid());
    assert(parse_package_contract_text("format=1\n[package]\nsource='git'\n[[packages]]\nalias='tls'\nsource='registry'\nregistry='vix'\npublisher='example'\nname='tls'\nrequirement={compatible='2.0.0'}\n[integration]\ntype='native-vix'\ninclude-roots=[]\n[[exports]]\nname='api'\nproject-target='api'").valid());
    assert(has_error(parse_package_contract_text("format=1\n[package]\nsource='git'\nrepository='https://example.invalid/r'\n[integration]\ntype='native-vix'\ninclude-roots=[]\n[[exports]]\nname='api'\nproject-target='api'"), ParseErrorCode::unknown_field));
    assert(has_error(parse_package_contract_text("format=1\nextra=1\n[package]\nsource='git'\n[integration]\ntype='native-vix'\ninclude-roots=[]\n[[exports]]\nname='api'\nproject-target='api'"), ParseErrorCode::unknown_field));
    assert(has_error(parse_package_contract_text("format=1\n[package]\nsource='registry'\nregistry='vix'\npublisher='example'\nname='fmt'\n[integration]\ntype='native-vix'\ninclude-roots=[]\n[[exports]]\nname='fmt'\nproject-target='fmt'"), ParseErrorCode::missing_field));
    assert(has_error(parse_package_contract_text("format=1\n[package]\nsource='git'\n[integration]\ntype='cmake-config'\ninclude-roots=[]\nconfig-name='fmt'\n[[exports]]\nname='fmt'\ncmake-target='fmt::fmt'"), ParseErrorCode::unknown_field));
    assert(has_error(parse_package_contract_text("format=1\n[package]\nsource='git'\n[integration]\ntype='cmake-config'\n[[exports]]\nname='fmt'\ncmake-target='fmt::fmt'"), ParseErrorCode::missing_field));
    assert(has_error(parse_package_contract_text("format=1\n[package]\nsource='git'\n[integration]\ntype='native-vix'\ninclude-roots=[]\n[[exports]]\nname='fmt'\ncmake-target='fmt::fmt'"), ParseErrorCode::unknown_field));
    assert(has_error(parse_package_contract_text("format=1\n[package]\nsource='git'\n[integration]\ntype='cmake-config'\nconfig-name='fmt'\n[[exports]]\nname='fmt'\nproject-target='fmt'"), ParseErrorCode::unknown_field));
  }

  void lock_tests()
  {
    assert(parse_lockfile_text(goodLock).valid());
    assert(parse_lockfile_text(lock_with_declared("source='registry',registry='vix',publisher='fmtlib',name='fmt',compatible='11.0.0'")).valid());
    assert(parse_lockfile_text(lock_with_declared("source='git',repository='https://example.invalid/r',tag='v1.0.0'")).valid());
    assert(has_error(parse_lockfile_text(lock_with_declared("source='registry',registry='vix',publisher='fmtlib',name='fmt',compatible='11.0.0',typo='x'")), ParseErrorCode::unknown_field));
    assert(has_error(parse_lockfile_text(lock_with_declared("source='git',repository='https://example.invalid/r',tag='v1.0.0',typo='x'")), ParseErrorCode::unknown_field));
    assert(has_error(parse_lockfile_text(lock_with_declared("source='registry',registry='vix',publisher='fmtlib',name='fmt',exact='11.0.0',compatible='11.0.0'")), ParseErrorCode::invalid_discriminated_shape));
    assert(has_error(parse_lockfile_text(lock_with_declared("source='git',repository='https://example.invalid/r',tag='v1',branch='main'")), ParseErrorCode::invalid_discriminated_shape));
    assert(has_error(parse_lockfile_text(lock_with_declared("source='registry',registry='vix',publisher='fmtlib',name='fmt'")), ParseErrorCode::invalid_discriminated_shape));
    assert(has_error(parse_lockfile_text(lock_with_declared("source='git',repository='https://example.invalid/r'")), ParseErrorCode::invalid_discriminated_shape));
    assert(parse_lockfile_text(R"toml(format = 1
root-requirements-digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
[[roots]]
alias = "proto"
declared = { source = "git", repository = "https://example.invalid/r", tag = "v1.0.0" }
node = "git-node"
[[nodes]]
id = "git-node"
source = "git"
repository = "https://example.invalid/r"
commit = "0123456789abcdef0123456789abcdef01234567"
digest = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
)toml").valid());
    const auto misplaced = parse_lockfile_text("format=1\n[[roots]]\nalias='fmt'\nroot-requirements-digest='sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'\ndeclared={}\nnode='x'\n[[nodes]]\nid='x'\nsource='git'\nrepository='r'\ncommit='0123456789abcdef0123456789abcdef01234567'\ndigest='sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'");
    assert(has_error(misplaced, ParseErrorCode::unknown_field, "roots[0].root-requirements-digest"));
    assert(has_error(parse_lockfile_text("format=1\nroots=[]\nnodes=[]"), ParseErrorCode::missing_field, "root-requirements-digest"));
    assert(has_error(parse_lockfile_text("format=1\nroot-requirements-digest=1\nroots=[]\nnodes=[]"), ParseErrorCode::wrong_type));
    assert(has_error(parse_lockfile_text("format=1\nroot-requirements-digest='sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'\nroots=[]\nnodes=[]\nextra=1"), ParseErrorCode::unknown_field));
    assert(has_error(parse_lockfile_text("format=1\nroot-requirements-digest='sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'\nroots=[]\n[[nodes]]\nid='x'\nsource='git'\nrepository='r'\ncommit='0123456789abcdef0123456789abcdef01234567'\ntag='v1'\ndigest='sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'"), ParseErrorCode::unknown_field));
    assert(has_error(parse_lockfile_text("format=1\nroot-requirements-digest='sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'\nroots=[]\nnodes=[]\ndependencies=[]"), ParseErrorCode::unknown_field));
    const auto emptyEdges = parse_lockfile_text("format=1\nroot-requirements-digest='sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'\nroots=[]\n[[nodes]]\nid='x'\nsource='git'\nrepository='r'\ncommit='0123456789abcdef0123456789abcdef01234567'\ndigest='sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'\ndependencies=[]");
    assert(has_error(emptyEdges, ParseErrorCode::invalid_discriminated_shape));

    const auto nested = parse_lockfile_text(R"toml(format = 1
root-requirements-digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
roots = []
[[nodes]]
id = "a"
source = "git"
repository = "https://example.invalid/a"
commit = "0123456789abcdef0123456789abcdef01234567"
digest = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
[[nodes.dependencies]]
alias = "b"
node = "b"
[[nodes]]
id = "b"
source = "git"
repository = "https://example.invalid/b"
commit = "1123456789abcdef0123456789abcdef01234567"
digest = "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
)toml");
    assert(nested.valid());
    assert(std::get<GitLockNode>(nested.value->nodes[0]).dependencies.size() == 1);
    assert(std::get<GitLockNode>(nested.value->nodes[1]).dependencies.empty());
    const auto sharedNode = parse_lockfile_text(R"toml(format = 1
root-requirements-digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
[[roots]]
alias = "first"
declared = { source = "git", repository = "https://example.invalid/r", tag = "v1" }
node = "one"
[[roots]]
alias = "second"
declared = { source = "git", repository = "https://example.invalid/r", tag = "v1" }
node = "one"
[[nodes]]
id = "one"
source = "git"
repository = "https://example.invalid/r"
commit = "0123456789abcdef0123456789abcdef01234567"
digest = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
)toml");
    assert(sharedNode.valid() && sharedNode.value->roots.size() == 2 && sharedNode.value->nodes.size() == 1);
  }

  void location_tests()
  {
    const auto syntax = parse_native_manifest_text("format=1\n[project\nname='x'", "bad.toml");
    assert(has_error(syntax, ParseErrorCode::toml_syntax_error));
    assert(syntax.issues.front().location && syntax.issues.front().location->line && syntax.issues.front().location->column);
    const auto unknown = parse_native_manifest_text("format=1\n[project]\nname='x'\nextra=1", "unknown.toml");
    assert(has_error(unknown, ParseErrorCode::unknown_field, "project.extra"));
    assert(unknown.issues.front().location && unknown.issues.front().location->source == "unknown.toml");
    const auto wrongType = parse_native_manifest_text("format=1\n[project]\nname=7", "type.toml");
    assert(has_error(wrongType, ParseErrorCode::wrong_type, "project.name"));
    assert(wrongType.issues.front().location && wrongType.issues.front().location->line);
  }

  void native_manifest_mutation_tests()
  {
    auto parsed = parse_native_manifest_text(R"toml(format = 1
[project]
name = "writer-check"
cxx-standard = "c++23"
[[targets]]
name = "core"
kind = "static"
sources = ["src/core.cpp"]
[targets.includes]
public = ["include"]
[targets.definitions]
public = ["CORE_API=1"]
[[targets]]
name = "core-test"
kind = "executable"
sources = ["tests/core_test.cpp"]
[[tests]]
name = "core-test"
target = "core-test"
)toml");
    assert(parsed.valid() && parsed.value);

    const auto first = serialize_native_manifest(*parsed.value);
    if (!first.valid() || !first.text)
      for (const auto &issue : first.issues)
        std::cerr << to_string(issue.code) << " [" << issue.subject << "]: " << issue.message << '\n';
    assert(first.valid() && first.text);
    const auto reparsed = parse_native_manifest_text(*first.text);
    assert(reparsed.valid() && reparsed.value);
    const auto second = serialize_native_manifest(*reparsed.value);
    assert(second.valid() && second.text && *first.text == *second.text);
    assert(reparsed.value->name == parsed.value->name);
    assert(reparsed.value->cxxStandard == parsed.value->cxxStandard);
    assert(reparsed.value->targets.size() == 2 && reparsed.value->tests.size() == 1);
    assert(reparsed.value->targets.front().sources == parsed.value->targets.front().sources);
    assert(reparsed.value->targets.front().includes.size() == 1);
    assert(reparsed.value->targets.front().definitions.size() == 1);

    NativeProject project = *parsed.value;
    PackageRequirement registry{"fmt", RegistryRequirement{{"vix", "fmtlib", "fmt"},
      CompatibleVersion{Version{10, 0, 0, std::nullopt}}}};
    assert(add_root_package_requirement(project, registry).valid());
    assert(project.packages.size() == 1);
    assert(!add_root_package_requirement(project, registry).valid());
    PackageRequirement invalidRequirement{"", RegistryRequirement{{"vix", "fmtlib", "bad"},
      ExactVersion{Version{1, 0, 0, std::nullopt}}}};
    assert(!add_root_package_requirement(project, invalidRequirement).valid());
    assert(project.packages.size() == 1);
    PackageRequirement git{"proto", GitRequirement{"https://example.invalid/proto.git", true,
      TagRevision{"v1.2.0"}}};
    assert(add_root_package_requirement(project, git).valid());
    assert(project.packages.size() == 2);
    const auto mixed = serialize_native_manifest(project);
    assert(mixed.valid() && mixed.text);
    const auto mixedParsed = parse_native_manifest_text(*mixed.text);
    assert(mixedParsed.valid() && mixedParsed.value && mixedParsed.value->packages.size() == 2);
    assert(std::holds_alternative<RegistryRequirement>(mixedParsed.value->packages[0].source));
    assert(std::holds_alternative<GitRequirement>(mixedParsed.value->packages[1].source));

    PackageRequirement replacement{"fmt", RegistryRequirement{{"vix", "fmtlib", "fmt"},
      ExactVersion{Version{10, 1, 0, std::nullopt}}}};
    assert(replace_root_package_requirement(project, "fmt", replacement).valid());
    assert(std::get<ExactVersion>(std::get<RegistryRequirement>(project.packages.front().source).version).version ==
      (Version{10, 1, 0, std::nullopt}));
    assert(remove_root_package_requirement(project, "proto").valid());
    assert(remove_root_package_requirement(project, "fmt").valid());
    assert(project.packages.empty());
    assert(!remove_root_package_requirement(project, "missing").valid());
    assert(project.targets.size() == 2 && project.tests.size() == 1);

    NativeProject invalid = *parsed.value;
    invalid.format = 9;
    const auto invalidSerialized = serialize_native_manifest(invalid);
    assert(!invalidSerialized.valid());

    const auto root = std::filesystem::temp_directory_path() / "vix-native-manifest-writer-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    assert(!error);
    const auto path = root / "vix.toml";
    { std::ofstream sentinel(path, std::ios::binary); sentinel << "old-content\n"; }
    const auto rejected = write_native_manifest_atomic(path, invalid);
    assert(!rejected.valid());
    { std::ifstream existing(path, std::ios::binary); const std::string bytes{std::istreambuf_iterator<char>{existing}, {}};
      assert(bytes == "old-content\n"); }
    const auto written = write_native_manifest_atomic(path, *parsed.value);
    assert(written.valid());
    const auto disk = parse_native_manifest_file(path);
    assert(disk.valid() && disk.value && disk.value->name == parsed.value->name);
    std::filesystem::remove_all(root, error);
  }
}

int main()
{
  manifest_tests();
  contract_tests();
  lock_tests();
  location_tests();
  native_manifest_mutation_tests();
}
