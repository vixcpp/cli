#!/usr/bin/env bash
# Regression: CMake owns the project while vix.app supplies dependencies.
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/vix-cmake-vix-deps.XXXXXX")"
trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"
mkdir -p "$HOME"

DEP="$ROOT/dep"
mkdir -p "$DEP/include/fixture" "$DEP/src"
cat >"$DEP/include/fixture/fixture.hpp" <<'CPP'
#pragma once
int fixture_value();
CPP
cat >"$DEP/src/fixture.cpp" <<'CPP'
#include <fixture/fixture.hpp>
int fixture_value() { return 42; }
CPP
cat >"$DEP/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
include(CheckCXXCompilerFlag)
project(fixture_dependency LANGUAGES CXX)
check_cxx_compiler_flag("-Wall" FIXTURE_SUPPORTS_WALL)
if(NOT FIXTURE_SUPPORTS_WALL)
  message(FATAL_ERROR "fixture compiler flag probe did not run")
endif()
add_library(fixture src/fixture.cpp)
add_library(fixture::fixture ALIAS fixture)
target_include_directories(fixture PUBLIC include)
CMAKE
git -C "$DEP" init -q
git -C "$DEP" config user.email contract@example.invalid
git -C "$DEP" config user.name contract
git -C "$DEP" add .
git -C "$DEP" commit -qm fixture
git -C "$DEP" tag v1.0.0

PROJECT="$ROOT/project"
mkdir -p "$PROJECT"
cat >"$PROJECT/main.cpp" <<'CPP'
#include <fixture/fixture.hpp>
int main() { return fixture_value() == 42 ? 0 : 1; }
CPP
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(cmake_owned LANGUAGES CXX)
if(NOT TARGET fixture::fixture)
  find_package(fixture CONFIG REQUIRED)
endif()
add_executable(cmake_owned main.cpp)
target_link_libraries(cmake_owned PRIVATE fixture::fixture)
CMAKE

# Installing into a CMake-owned project creates a dependency-only vix.app;
# it must not invent Vix-generated-project metadata.
(cd "$PROJECT" && "$VIX_BIN" install "$DEP" --name fixture --tag v1.0.0 --target fixture::fixture >/dev/null)
test -f "$PROJECT/vix.app"
grep -Fq '[dependencies.fixture]' "$PROJECT/vix.app"
! grep -Eq '^(name|type|standard|sources)[[:space:]]*=' "$PROJECT/vix.app"
test -f "$PROJECT/.vix/vix_deps.cmake"

(cd "$PROJECT" && "$VIX_BIN" build --clean >/dev/null)
"$PROJECT/build-ninja/cmake_owned"
! grep -Fq 'CMAKE_PROJECT_TOP_LEVEL_INCLUDES' "$PROJECT/build-ninja/CMakeCache.txt"
grep -Fq 'CMAKE_PROJECT_INCLUDE' "$PROJECT/build-ninja/CMakeCache.txt"
grep -Fq "$PROJECT/build-ninja/vix-project-deps.cmake" "$PROJECT/build-ninja/CMakeCache.txt"
grep -Fq "$PROJECT/.vix/vix_deps.cmake" "$PROJECT/build-ninja/vix-project-deps.cmake"
echo "CMake Vix dependency-manifest regression passed"
