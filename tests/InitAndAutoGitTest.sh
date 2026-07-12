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
}

CMAKE_REPO="$ROOT/sample-repo"
mkdir -p "$CMAKE_REPO/include/sample" "$CMAKE_REPO/src"
cat > "$CMAKE_REPO/include/sample/sample.hpp" <<'HPP'
#pragma once
namespace sample { int answer(); }
HPP
cat > "$CMAKE_REPO/src/sample.cpp" <<'CPP'
#include <sample/sample.hpp>
namespace sample { int answer() { return 42; } }
CPP
cat > "$CMAKE_REPO/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(sample LANGUAGES CXX)
add_library(sample src/sample.cpp)
add_library(sample::sample ALIAS sample)
add_executable(sample_tests src/sample.cpp)
target_include_directories(sample PUBLIC include)
CMAKE
git_init_fixture "$CMAKE_REPO"
git -C "$CMAKE_REPO" tag v0.1.0
git -C "$CMAKE_REPO" tag v1.1.0
git -C "$CMAKE_REPO" tag v2.0.0-beta.1

HEADER_REPO="$ROOT/header-repo"
mkdir -p "$HEADER_REPO/include/headers"
cat > "$HEADER_REPO/include/headers/headers.hpp" <<'HPP'
#pragma once
namespace headers { inline int answer() { return 42; } }
HPP
git_init_fixture "$HEADER_REPO"
git -C "$HEADER_REPO" tag 1.0.0

INIT_APP="$ROOT/init app !"
mkdir -p "$INIT_APP"
cat > "$INIT_APP/main.cpp" <<'CPP'
int main() { return 0; }
CPP
cat > "$INIT_APP/extra.cpp" <<'CPP'
int extra() { return 1; }
CPP
(cd "$INIT_APP" && "$VIX_BIN" init --standard c++23 >/dev/null)
grep -q 'name = "init-app"' "$INIT_APP/vix.app"
grep -q 'standard = "c++23"' "$INIT_APP/vix.app"
grep -q 'extra.cpp' "$INIT_APP/vix.app"
grep -q 'main.cpp' "$INIT_APP/vix.app"
if (cd "$INIT_APP" && "$VIX_BIN" init >/dev/null 2>&1); then
  echo "vix init replaced existing vix.app without --force" >&2
  exit 1
fi
(cd "$INIT_APP" && "$VIX_BIN" init --name forced --force >/dev/null)
grep -q 'name = "forced"' "$INIT_APP/vix.app"

APP="$ROOT/auto-app"
mkdir -p "$APP"
cat > "$APP/main.cpp" <<'CPP'
#include <sample/sample.hpp>
int main() { return sample::answer() == 42 ? 0 : 1; }
CPP
(cd "$APP" && "$VIX_BIN" init >/dev/null && "$VIX_BIN" install "$CMAKE_REPO" >/dev/null && "$VIX_BIN" run main.cpp >/dev/null)
grep -q 'tag = "v1.1.0"' "$APP/vix.app"
grep -q 'target = "sample::sample"' "$APP/vix.app"
grep -q '"requested": "v1.1.0"' "$APP/vix.lock"

HEADERS_APP="$ROOT/headers-app"
mkdir -p "$HEADERS_APP"
cat > "$HEADERS_APP/main.cpp" <<'CPP'
#include <headers/headers.hpp>
int main() { return headers::answer() == 42 ? 0 : 1; }
CPP
(cd "$HEADERS_APP" && "$VIX_BIN" init >/dev/null && "$VIX_BIN" install "$HEADER_REPO" >/dev/null && "$VIX_BIN" run main.cpp >/dev/null)
grep -q 'header_only = true' "$HEADERS_APP/vix.app"
grep -q 'include = "include"' "$HEADERS_APP/vix.app"

TEMP_APP="$ROOT/temp-app"
mkdir -p "$TEMP_APP"
cat > "$TEMP_APP/main.cpp" <<'CPP'
#include <sample/sample.hpp>
int main() { return sample::answer() == 42 ? 0 : 1; }
CPP
(cd "$TEMP_APP" && "$VIX_BIN" run main.cpp --dep "$CMAKE_REPO" >/dev/null)
test ! -e "$TEMP_APP/vix.app"
test ! -e "$TEMP_APP/vix.lock"
test ! -e "$TEMP_APP/.vix"

SAVE_APP="$ROOT/save-app"
mkdir -p "$SAVE_APP"
cat > "$SAVE_APP/main.cpp" <<'CPP'
#include <headers/headers.hpp>
int main() { return headers::answer() == 42 ? 0 : 1; }
CPP
(cd "$SAVE_APP" && "$VIX_BIN" run main.cpp --dep "$HEADER_REPO" --save >/dev/null)
test -e "$SAVE_APP/vix.app"
test -e "$SAVE_APP/vix.lock"
grep -q '\[dependencies.header-repo\]' "$SAVE_APP/vix.app"
