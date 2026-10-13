#include <vix/cli/project/native/TomlParser.hpp>

#include <cassert>
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
}

int main()
{
  manifest_tests();
  contract_tests();
  lock_tests();
  location_tests();
}
