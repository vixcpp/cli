#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
FIXTURE_DIR="${2:?missing fixture directory}"
ROOT="$(mktemp -d)"
cleanup() {
  rm -rf "$ROOT"
}
trap cleanup EXIT

export HOME="$ROOT/home"
export VIX_GLOBAL_PREFIX="$ROOT/vix-global"
mkdir -p "$HOME/.vix/registry/index/index" "$VIX_GLOBAL_PREFIX" "$HOME/.local/bin"
export PATH="$HOME/.local/bin:$PATH"

PKG_REPO="$ROOT/ovi-repo"
cp -R "$FIXTURE_DIR" "$PKG_REPO"
git -C "$PKG_REPO" init -q
git -C "$PKG_REPO" config user.email test@example.invalid
git -C "$PKG_REPO" config user.name "Vix Test"
git -C "$PKG_REPO" add .
git -C "$PKG_REPO" commit -q -m "ovi fixture"
COMMIT="$(git -C "$PKG_REPO" rev-parse HEAD)"

cat > "$HOME/.vix/registry/index/index/vixcpp.ovi.json" <<JSON
{
  "name": "ovi",
  "namespace": "vixcpp",
  "repo": { "url": "$PKG_REPO" },
  "versions": {
    "0.1.0": {
      "tag": "v0.1.0",
      "commit": "$COMMIT"
    }
  }
}
JSON

"$VIX_BIN" install -g vixcpp/ovi

test -x "$VIX_GLOBAL_PREFIX/bin/ovi"
test -x "$VIX_GLOBAL_PREFIX/bin/ovi-doctor"
test -f "$VIX_GLOBAL_PREFIX/include/ovi/ovi.hpp"
test -f "$VIX_GLOBAL_PREFIX/lib/cmake/ovi/oviTargets.cmake"

command -v ovi | grep -q "$HOME/.local/bin/ovi"
ovi --version | grep -q "ovi 0.1.0"
ovi --help | grep -q "usage: ovi"
ovi greet Gaspard | grep -q "Hello, Gaspard!"
ovi-doctor | grep -q "ovi doctor ok"

grep -q '"executables"' "$VIX_GLOBAL_PREFIX/installed.json"
grep -q '"shims"' "$VIX_GLOBAL_PREFIX/installed.json"
grep -q '"ovi-doctor"' "$VIX_GLOBAL_PREFIX/installed.json"
grep -q "$VIX_GLOBAL_PREFIX/bin" "$HOME/.bashrc"
"$VIX_BIN" list -g --json | grep -q '"ovi"'

"$VIX_BIN" uninstall -g vixcpp/ovi
test ! -e "$VIX_GLOBAL_PREFIX/bin/ovi"
test ! -e "$VIX_GLOBAL_PREFIX/bin/ovi-doctor"
test ! -e "$HOME/.local/bin/ovi"
test ! -e "$HOME/.local/bin/ovi-doctor"
test ! -e "$VIX_GLOBAL_PREFIX/include/ovi/ovi.hpp"


FALLBACK_REPO="$ROOT/fallback-repo"
mkdir -p "$FALLBACK_REPO/include/fallback_cli" "$FALLBACK_REPO/src"
cat > "$FALLBACK_REPO/include/fallback_cli/fallback.hpp" <<'EOF'
#pragma once
namespace fallback_cli { inline int answer() { return 42; } }
EOF
cat > "$FALLBACK_REPO/src/main.cpp" <<'EOF'
#include <fallback_cli/fallback.hpp>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "answer") {
    std::cout << fallback_cli::answer() << "\n";
    return 0;
  }
  std::cout << "fallback_cli ok\n";
  return 0;
}
EOF
cat > "$FALLBACK_REPO/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(fallback_cli VERSION 0.1.0 LANGUAGES CXX)
add_library(fallback_cli_lib INTERFACE)
target_include_directories(fallback_cli_lib INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
add_executable(fallback_cli src/main.cpp)
target_link_libraries(fallback_cli PRIVATE fallback_cli_lib)
install(DIRECTORY include/ DESTINATION include)
EOF
cat > "$FALLBACK_REPO/vix.json" <<'EOF'
{
  "name": "fallback_cli",
  "namespace": "vixcpp",
  "version": "0.1.0",
  "type": "header-only",
  "include": "include",
  "deps": [],
  "description": "Global install fallback fixture."
}
EOF

git -C "$FALLBACK_REPO" init -q
git -C "$FALLBACK_REPO" config user.email test@example.invalid
git -C "$FALLBACK_REPO" config user.name "Vix Test"
git -C "$FALLBACK_REPO" add .
git -C "$FALLBACK_REPO" commit -q -m "fallback fixture"
FALLBACK_COMMIT="$(git -C "$FALLBACK_REPO" rev-parse HEAD)"

cat > "$HOME/.vix/registry/index/index/vixcpp.fallback_cli.json" <<JSON
{
  "name": "fallback_cli",
  "namespace": "vixcpp",
  "repo": { "url": "$FALLBACK_REPO" },
  "versions": {
    "0.1.0": {
      "tag": "v0.1.0",
      "commit": "$FALLBACK_COMMIT"
    }
  }
}
JSON

"$VIX_BIN" install -g vixcpp/fallback_cli

test -x "$VIX_GLOBAL_PREFIX/bin/fallback_cli"
test -f "$VIX_GLOBAL_PREFIX/include/fallback_cli/fallback.hpp"
command -v fallback_cli | grep -q "$HOME/.local/bin/fallback_cli"
fallback_cli answer | grep -q "42"
grep -q '"fallback_cli"' "$VIX_GLOBAL_PREFIX/installed.json"

"$VIX_BIN" uninstall -g vixcpp/fallback_cli
test ! -e "$VIX_GLOBAL_PREFIX/bin/fallback_cli"
test ! -e "$HOME/.local/bin/fallback_cli"
