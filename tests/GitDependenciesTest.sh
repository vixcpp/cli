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
