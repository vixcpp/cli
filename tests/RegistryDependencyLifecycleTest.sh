#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
cleanup() { rm -rf "$ROOT"; }
trap cleanup EXIT

export HOME="$ROOT/home"
mkdir -p "$HOME/.vix/registry/index/index"

PACKAGE="$ROOT/package"
mkdir -p "$PACKAGE/include/test"
printf '#pragma once\ninline int package_value() { return 1; }\n' > "$PACKAGE/include/test/package.hpp"
cat > "$PACKAGE/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(test_package LANGUAGES CXX)
add_library(test_package INTERFACE)
add_library(test::package ALIAS test_package)
target_include_directories(test_package INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
EOF
git -C "$PACKAGE" init -q
git -C "$PACKAGE" config user.email contract@example.invalid
git -C "$PACKAGE" config user.name contract
git -C "$PACKAGE" add .
git -C "$PACKAGE" commit -qm initial
COMMIT="$(git -C "$PACKAGE" rev-parse HEAD)"

cat > "$HOME/.vix/registry/index/index/test.package.json" <<EOF
{"repo":{"url":"$PACKAGE"},"versions":{"1.0.0":{"tag":"v1.0.0","commit":"$COMMIT"}}}
EOF

OTHER="$ROOT/other"
mkdir -p "$OTHER/include/test"
printf '#pragma once\ninline int other_value() { return 1; }\n' > "$OTHER/include/test/other.hpp"
git -C "$OTHER" init -q
git -C "$OTHER" config user.email contract@example.invalid
git -C "$OTHER" config user.name contract
git -C "$OTHER" add .
git -C "$OTHER" commit -qm v1
OTHER_V1="$(git -C "$OTHER" rev-parse HEAD)"
printf '#pragma once\ninline int other_value() { return 2; }\n' > "$OTHER/include/test/other.hpp"
git -C "$OTHER" add .
git -C "$OTHER" commit -qm v1.1
OTHER_V11="$(git -C "$OTHER" rev-parse HEAD)"
cat > "$HOME/.vix/registry/index/index/test.other.json" <<EOF
{"repo":{"url":"$OTHER"},"versions":{"1.0.0":{"tag":"v1.0.0","commit":"$OTHER_V1"},"1.1.0":{"tag":"v1.1.0","commit":"$OTHER_V11"}}}
EOF

PRESERVE="$ROOT/preserve"
mkdir -p "$PRESERVE"
cat > "$PRESERVE/vix.json" <<'EOF'
{"deps":[{"id":"test/package","version":"^1.0.0"},{"id":"test/other","version":"^1.0.0"}]}
EOF
cat > "$PRESERVE/vix.lock" <<EOF
{"lockVersion":1,"dependencies":[{"id":"test/package","requested":"^1.0.0","version":"1.0.0","repo":"$PACKAGE","tag":"v1.0.0","commit":"$COMMIT"},{"id":"test/other","requested":"^1.0.0","version":"1.0.0","repo":"$OTHER","tag":"v1.0.0","commit":"$OTHER_V1"}]}
EOF
(cd "$PRESERVE" && "$VIX_BIN" remove test/package >/dev/null)
if grep -Fq 'test/package' "$PRESERVE/vix.json"; then exit 1; fi
grep -Fq 'test/other' "$PRESERVE/vix.json"
if grep -Fq 'test/package' "$PRESERVE/vix.lock"; then exit 1; fi
grep -Fq '"version": "1.0.0"' "$PRESERVE/vix.lock"
grep -Fq "$OTHER_V1" "$PRESERVE/vix.lock"
if grep -Fq "$OTHER_V11" "$PRESERVE/vix.lock"; then exit 1; fi

PROJECT="$ROOT/project"
mkdir -p "$PROJECT"
printf '{"deps": []}\n' > "$PROJECT/vix.json"

(cd "$PROJECT" && "$VIX_BIN" add test/package@1.0.0 >/dev/null)
grep -Fq '"id": "test/package"' "$PROJECT/vix.json"
grep -Fq '"id": "test/package"' "$PROJECT/vix.lock"

(cd "$PROJECT" && "$VIX_BIN" install >/dev/null)
grep -Fq 'test/package' "$PROJECT/.vix/vix_deps.cmake"
test -e "$PROJECT/.vix/deps/test.package"

(cd "$PROJECT" && "$VIX_BIN" remove test/package >/dev/null)
if grep -Fq 'test/package' "$PROJECT/vix.json"; then exit 1; fi
if grep -Fq 'test/package' "$PROJECT/vix.lock"; then exit 1; fi
if grep -Fq 'test/package' "$PROJECT/.vix/vix_deps.cmake"; then exit 1; fi
test -e "$PROJECT/.vix/deps/test.package"

(cd "$PROJECT" && "$VIX_BIN" add test/package@1.0.0 >/dev/null)
(cd "$PROJECT" && "$VIX_BIN" install >/dev/null)
(cd "$PROJECT" && "$VIX_BIN" remove test/package --purge --yes >/dev/null)
test ! -e "$PROJECT/.vix/deps/test.package"

RECONCILE="$ROOT/reconcile"
mkdir -p "$RECONCILE"
printf '{"deps": [{"id": "test/package", "version": "1.0.0"}]}\n' > "$RECONCILE/vix.json"
printf '{"lockVersion": 1, "dependencies": []}\n' > "$RECONCILE/vix.lock"
(cd "$RECONCILE" && "$VIX_BIN" install > "$ROOT/install.log")
if grep -Fq 'No dependencies to install' "$ROOT/install.log"; then exit 1; fi
grep -Fq '"id": "test/package"' "$RECONCILE/vix.lock"
grep -Fq 'test/package' "$RECONCILE/.vix/vix_deps.cmake"
