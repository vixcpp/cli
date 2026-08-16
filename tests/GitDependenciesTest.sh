#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
cleanup() { rm -rf "$ROOT"; }
trap cleanup EXIT

export HOME="$ROOT/home"
mkdir -p "$HOME"

git_init_fixture() {
  local dir="$1"
  git -C "$dir" init -q
  git -C "$dir" config user.email test@example.invalid
  git -C "$dir" config user.name "Vix Test"
  git -C "$dir" add .
  git -C "$dir" commit -q -m init
  git -C "$dir" rev-parse HEAD
}

HEADER_REPO="$ROOT/sample-headers"
mkdir -p "$HEADER_REPO/include/sample"
cat > "$HEADER_REPO/include/sample/sample.hpp" <<'HPP'
#pragma once
namespace sample { inline int value() { return 42; } }
HPP
HEADER_COMMIT="$(git_init_fixture "$HEADER_REPO")"

CMAKE_REPO="$ROOT/sample-cmake"
mkdir -p "$CMAKE_REPO/include/cm" "$CMAKE_REPO/src"
cat > "$CMAKE_REPO/include/cm/cm.hpp" <<'HPP'
#pragma once
namespace cm { int value(); }
HPP
cat > "$CMAKE_REPO/src/cm.cpp" <<'CPP'
#include <cm/cm.hpp>
namespace cm { int value() { return 42; } }
CPP
cat > "$CMAKE_REPO/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(sample_cmake LANGUAGES CXX)
add_library(cm_lib src/cm.cpp)
add_library(cm::cm ALIAS cm_lib)
target_include_directories(cm_lib PUBLIC include)
CMAKE
CMAKE_COMMIT="$(git_init_fixture "$CMAKE_REPO")"

HEADER_APP="$ROOT/header-app"
mkdir -p "$HEADER_APP"
cat > "$HEADER_APP/vix.app" <<APP
name = "hello_headers"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]

[dependencies.sample]
git = "$HEADER_REPO"
rev = "$HEADER_COMMIT"
header_only = true
include = "include"
APP
cat > "$HEADER_APP/main.cpp" <<'CPP'
#include <sample/sample.hpp>
int main() { return sample::value() == 42 ? 0 : 1; }
CPP
(cd "$HEADER_APP" && "$VIX_BIN" install >/dev/null && "$VIX_BIN" run main.cpp >/dev/null)

grep -q '"source": "git"' "$HEADER_APP/vix.lock"
test -e "$HEADER_APP/.vix/deps/sample"

CMAKE_APP="$ROOT/cmake-app"
mkdir -p "$CMAKE_APP"
cat > "$CMAKE_APP/vix.app" <<APP
name = "hello_cmake"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]

[dependencies.cm]
git = "$CMAKE_REPO"
rev = "$CMAKE_COMMIT"
target = "cm::cm"
APP
cat > "$CMAKE_APP/main.cpp" <<'CPP'
#include <cm/cm.hpp>
int main() { return cm::value() == 42 ? 0 : 1; }
CPP
(cd "$CMAKE_APP" && "$VIX_BIN" install >/dev/null && "$VIX_BIN" run main.cpp >/dev/null)

grep -q 'cm::cm' "$CMAKE_APP/vix.lock"

# A lockfile-driven no-op install must not resolve the remote again, recreate
# an already-correct dependency link, or rewrite generated CMake integration.
NOOP_LINK="$CMAKE_APP/.vix/deps/cm"
NOOP_CMAKE="$CMAKE_APP/.vix/vix_deps.cmake"
NOOP_LINK_TIME="$(stat -c %Y "$NOOP_LINK")"
NOOP_CMAKE_TIME="$(stat -c %Y "$NOOP_CMAKE")"
GIT_REAL="$(command -v git)"
FAKE_GIT_BIN="$ROOT/fake-git-bin"
mkdir -p "$FAKE_GIT_BIN"
cat > "$FAKE_GIT_BIN/git" <<EOF
#!/usr/bin/env bash
if [ "\${1:-}" = "ls-remote" ]; then
  echo "unexpected git ls-remote during lockfile install" >&2
  exit 88
fi
exec "$GIT_REAL" "\$@"
EOF
chmod +x "$FAKE_GIT_BIN/git"
sleep 1
(cd "$CMAKE_APP" && PATH="$FAKE_GIT_BIN:$PATH" "$VIX_BIN" install >/dev/null)
test "$NOOP_LINK_TIME" = "$(stat -c %Y "$NOOP_LINK")"
test "$NOOP_CMAKE_TIME" = "$(stat -c %Y "$NOOP_CMAKE")"

# Removing project integration must reuse the immutable checkout without a
# remote lookup and repair both the dependency link and CMake integration.
rm "$NOOP_LINK" "$NOOP_CMAKE"
(cd "$CMAKE_APP" && PATH="$FAKE_GIT_BIN:$PATH" "$VIX_BIN" install >/dev/null)
test -e "$NOOP_LINK"
test -e "$NOOP_CMAKE"

# Structured Git execution keeps local repository paths with spaces as one
# argument through direct installation.
SPACED_REPO="$ROOT/header repo with spaces"
git clone -q "$HEADER_REPO" "$SPACED_REPO"
SPACED_APP="$ROOT/space-path-app"
mkdir -p "$SPACED_APP"
cat > "$SPACED_APP/vix.app" <<'APP'
name = "space_path"
type = "executable"
standard = "c++20"
APP
(cd "$SPACED_APP" && "$VIX_BIN" install "$SPACED_REPO" --name spaced --rev "$HEADER_COMMIT" --header-only --include include >/dev/null)
test -e "$SPACED_APP/.vix/deps/spaced"

AUTO_RUN_APP="$ROOT/auto-run-app"
mkdir -p "$AUTO_RUN_APP"
cat > "$AUTO_RUN_APP/vix.app" <<APP
name = "auto_run_cmake"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]

[dependencies.cm]
git = "$CMAKE_REPO"
rev = "$CMAKE_COMMIT"
target = "cm::cm"
APP
cat > "$AUTO_RUN_APP/main.cpp" <<'CPP'
#include <cm/cm.hpp>
int main() { return cm::value() == 42 ? 0 : 1; }
CPP
(cd "$AUTO_RUN_APP" && "$VIX_BIN" run main.cpp >/dev/null)
test -e "$AUTO_RUN_APP/.vix/vix_deps.cmake"
grep -q 'cm::cm' "$AUTO_RUN_APP/vix.lock"

CLI_APP="$ROOT/cli-add-app"
mkdir -p "$CLI_APP"
cat > "$CLI_APP/vix.app" <<'APP'
name = "cli_add"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]
APP
cat > "$CLI_APP/main.cpp" <<'CPP'
#include <sample/sample.hpp>
int main() { return sample::value() == 42 ? 0 : 1; }
CPP
(cd "$CLI_APP" && "$VIX_BIN" install "$HEADER_REPO" --name sample --header-only --include include >/dev/null && "$VIX_BIN" run main.cpp >/dev/null)
grep -q '\[dependencies.sample\]' "$CLI_APP/vix.app"
(cd "$CLI_APP" && "$VIX_BIN" uninstall sample >/dev/null)
! grep -q '\[dependencies.sample\]' "$CLI_APP/vix.app"
test ! -e "$CLI_APP/.vix/deps/sample"

# A freshly initialized app may not have sources yet. Installing a dependency
# is dependency work, not build-completeness validation.
FRESH_INIT_APP="$ROOT/fresh-init-app"
mkdir -p "$FRESH_INIT_APP"
(cd "$FRESH_INIT_APP" && "$VIX_BIN" init >/dev/null && "$VIX_BIN" install "$HEADER_REPO" --name sample --rev "$HEADER_COMMIT" --header-only --include include >/dev/null)
grep -q '\[dependencies.sample\]' "$FRESH_INIT_APP/vix.app"
test -e "$FRESH_INIT_APP/.vix/deps/sample"

# A failed direct install must restore both user-visible dependency state files.
FAILED_APP="$ROOT/failed-install-app"
mkdir -p "$FAILED_APP"
cat > "$FAILED_APP/vix.app" <<'APP'
name = "failed_install"
type = "executable"
standard = "c++20"
APP
cp "$FAILED_APP/vix.app" "$FAILED_APP/vix.app.before"
if (cd "$FAILED_APP" && "$VIX_BIN" install "file://$ROOT/does-not-exist" --name missing >/dev/null 2>&1); then
  echo "install unexpectedly resolved a missing repository" >&2
  exit 1
fi
cmp "$FAILED_APP/vix.app.before" "$FAILED_APP/vix.app"
test ! -e "$FAILED_APP/vix.lock"
if (cd "$FAILED_APP" && "$VIX_BIN" install "file://$ROOT/does-not-exist" --name missing 2>"$FAILED_APP/retry.err"); then
  echo "retry unexpectedly resolved a missing repository" >&2
  exit 1
fi
! grep -q 'dependency already exists' "$FAILED_APP/retry.err"
cmp "$FAILED_APP/vix.app.before" "$FAILED_APP/vix.app"

# An existing identical declaration is recoverable when it has not yet been
# materialized; a differing declaration remains a conflict.
DECLARED_APP="$ROOT/declared-install-app"
mkdir -p "$DECLARED_APP"
cat > "$DECLARED_APP/vix.app" <<APP
name = "declared_install"
type = "executable"
standard = "c++20"

[dependencies.sample]
git = "$HEADER_REPO"
rev = "$HEADER_COMMIT"
header_only = true
include = "include"
APP
(cd "$DECLARED_APP" && "$VIX_BIN" install "$HEADER_REPO" --name sample --header-only --include include >/dev/null)
test -e "$DECLARED_APP/.vix/deps/sample"
if (cd "$DECLARED_APP" && "$VIX_BIN" install "$HEADER_REPO" --name sample --header-only --include other >/dev/null 2>"$DECLARED_APP/conflict.err"); then
  echo "install accepted a conflicting dependency declaration" >&2
  exit 1
fi
grep -q 'conflicting dependency declaration' "$DECLARED_APP/conflict.err"

# Build errors caused by vix.app must not cascade into a generic directory hint.
INVALID_APP="$ROOT/invalid-manifest-app"
mkdir -p "$INVALID_APP"
cat > "$INVALID_APP/vix.app" <<'APP'
name = "invalid_manifest"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]
unknown = "field"
APP
if (cd "$INVALID_APP" && "$VIX_BIN" build >"$INVALID_APP/build.out" 2>&1); then
  echo "build unexpectedly accepted an invalid vix.app" >&2
  exit 1
fi
grep -q "Unknown scalar field in vix.app: 'unknown'" "$INVALID_APP/build.out"
! grep -q 'Unable to determine the project directory' "$INVALID_APP/build.out"

# Reconciliation is manifest-driven: retain an unchanged direct lock entry,
# resolve only a newly declared entry, update CMake-only metadata without a
# remote lookup, and remove only explicitly-owned root entries.
RECON_APP="$ROOT/reconciliation-app"
mkdir -p "$RECON_APP"
cat > "$RECON_APP/vix.app" <<APP
name = "reconciliation"
type = "executable"
standard = "c++20"

[dependencies.headers]
git = "$HEADER_REPO"
rev = "$HEADER_COMMIT"
header_only = true
include = "include"
APP
(cd "$RECON_APP" && "$VIX_BIN" install >/dev/null)
HEADERS_LOCKED_COMMIT="$(python3 -c 'import json,sys; print(next(x["commit"] for x in json.load(open(sys.argv[1]))["dependencies"] if x["id"] == "headers"))' "$RECON_APP/vix.lock")"
cat >> "$RECON_APP/vix.app" <<APP

[dependencies.cm]
git = "$CMAKE_REPO"
rev = "$CMAKE_COMMIT"
target = "cm::cm"

[dependencies.cm.cmake]
CM_OPTION = true
APP
(cd "$RECON_APP" && PATH="$FAKE_GIT_BIN:$PATH" "$VIX_BIN" install >/dev/null)
test "$HEADERS_LOCKED_COMMIT" = "$(python3 -c 'import json,sys; print(next(x["commit"] for x in json.load(open(sys.argv[1]))["dependencies"] if x["id"] == "headers"))' "$RECON_APP/vix.lock")"
grep -q '"id": "cm"' "$RECON_APP/vix.lock"

sed -i 's/CM_OPTION = true/CM_OPTION = false/' "$RECON_APP/vix.app"
(cd "$RECON_APP" && PATH="$FAKE_GIT_BIN:$PATH" "$VIX_BIN" install >/dev/null)
grep -q 'set(CM_OPTION false CACHE STRING "" FORCE)' "$RECON_APP/.vix/vix_deps.cmake"

sed -i '/\[dependencies.cm\]/,$d' "$RECON_APP/vix.app"
(cd "$RECON_APP" && PATH="$FAKE_GIT_BIN:$PATH" "$VIX_BIN" install >/dev/null)
test ! -e "$RECON_APP/.vix/deps/cm"
! grep -q '"id": "cm"' "$RECON_APP/vix.lock"

# A failure while resolving a new declaration must not publish any partial
# reconciliation over the previously usable lockfile.
ATOMIC_APP="$ROOT/atomic-reconciliation-app"
mkdir -p "$ATOMIC_APP"
cat > "$ATOMIC_APP/vix.app" <<APP
name = "atomic_reconciliation"
type = "executable"
standard = "c++20"

[dependencies.headers]
git = "$HEADER_REPO"
rev = "$HEADER_COMMIT"
header_only = true
include = "include"
APP
(cd "$ATOMIC_APP" && "$VIX_BIN" install >/dev/null)
cp "$ATOMIC_APP/vix.lock" "$ATOMIC_APP/vix.lock.before"
cat >> "$ATOMIC_APP/vix.app" <<APP

[dependencies.missing]
git = "file://$ROOT/not-a-repository"
rev = "deadbeef"
header_only = true
include = "include"
APP
if (cd "$ATOMIC_APP" && "$VIX_BIN" install >/dev/null 2>&1); then
  echo "install unexpectedly resolved a missing reconciliation dependency" >&2
  exit 1
fi
cmp "$ATOMIC_APP/vix.lock.before" "$ATOMIC_APP/vix.lock"
