#!/usr/bin/env bash
set -euo pipefail
VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
fail() { echo "InstallReconciliationContractTest: $*" >&2; exit 1; }
fixture() { local name="$1"; mkdir -p "$ROOT/$name/include/$name"; printf '#pragma once\ninline int %s_value(){return 1;}\n' "$name" >"$ROOT/$name/include/$name/$name.hpp"; cat >"$ROOT/$name/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project($name LANGUAGES CXX)
add_library(${name}_lib INTERFACE)
add_library(example::$name ALIAS ${name}_lib)
target_include_directories(${name}_lib INTERFACE \${CMAKE_CURRENT_SOURCE_DIR}/include)
EOF
git -C "$ROOT/$name" init -q; git -C "$ROOT/$name" config user.email contract@example.invalid; git -C "$ROOT/$name" config user.name contract; git -C "$ROOT/$name" add .; git -C "$ROOT/$name" commit -qm init; git -C "$ROOT/$name" branch dev; git -C "$ROOT/$name" tag v1.0.0; git -C "$ROOT/$name" rev-parse HEAD; }
A_COMMIT="$(fixture a)"; B_COMMIT="$(fixture b)"; APP="$ROOT/app"; mkdir -p "$APP"
cat >"$APP/vix.app" <<EOF
name = "reconciliation" 
type = "executable"
standard = "c++20"
[dependencies.a]
git = "$ROOT/a"
tag = "v1.0.0"
target = "example::a"
EOF
(cd "$APP" && "$VIX_BIN" install >/dev/null)
cp "$APP/vix.lock" "$APP/lock.before"; LINK_TIME="$(stat -c %Y "$APP/.vix/deps/a")"; CMAKE_TIME="$(stat -c %Y "$APP/.vix/vix_deps.cmake")"
REAL_GIT="$(command -v git)"; mkdir "$ROOT/git-bin"; cat >"$ROOT/git-bin/git" <<EOF
#!/usr/bin/env bash
if [ "\${1:-}" = ls-remote ] && [[ "\$*" == *"$ROOT/a"* ]]; then
  echo 'unexpected remote resolution for unchanged dependency a' >&2
  exit 88
fi
exec "$REAL_GIT" "\$@"
EOF
chmod +x "$ROOT/git-bin/git"
sleep 1; (cd "$APP" && PATH="$ROOT/git-bin:$PATH" "$VIX_BIN" install >/dev/null)
cmp "$APP/lock.before" "$APP/vix.lock"; test "$LINK_TIME" = "$(stat -c %Y "$APP/.vix/deps/a")"; test "$CMAKE_TIME" = "$(stat -c %Y "$APP/.vix/vix_deps.cmake")"
rm "$APP/.vix/deps/a" "$APP/.vix/vix_deps.cmake"; (cd "$APP" && PATH="$ROOT/git-bin:$PATH" "$VIX_BIN" install >/dev/null); test -e "$APP/.vix/deps/a"
cat >>"$APP/vix.app" <<EOF
[dependencies.b]
git = "$ROOT/b"
branch = "dev"
target = "example::b"
EOF
(cd "$APP" && PATH="$ROOT/git-bin:$PATH" "$VIX_BIN" install >/dev/null)
grep -Fq "\"commit\": \"$A_COMMIT\"" "$APP/vix.lock"; grep -Fq "\"commit\": \"$B_COMMIT\"" "$APP/vix.lock"
cp "$APP/vix.lock" "$APP/lock.before"; cat >>"$APP/vix.app" <<EOF
[dependencies.bad]
git = "file://$ROOT/missing"
rev = "deadbeef"
target = "example::bad"
EOF
if (cd "$APP" && "$VIX_BIN" install >/dev/null 2>&1); then fail "invalid dependency resolved"; fi
cmp "$APP/lock.before" "$APP/vix.lock"
echo "InstallReconciliationContractTest passed"
