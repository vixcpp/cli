#!/usr/bin/env bash
# External CMake discovery, configure arguments and Vix-owned backend state.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/prefix/lib/cmake/TestDependency" "$ROOT/project/build"
PROJECT="$ROOT/project"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
cat >"$ROOT/prefix/lib/cmake/TestDependency/TestDependencyConfig.cmake" <<'CMAKE'
if(NOT TARGET test::dependency)
  add_library(test::dependency INTERFACE IMPORTED)
endif()
CMAKE
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(cmake_discovery_current LANGUAGES CXX)
find_package(TestDependency CONFIG REQUIRED)
option(FEATURE_VALUE "context test" OFF)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE test::dependency)
target_compile_definitions(app PRIVATE FEATURE_VALUE=$<BOOL:${FEATURE_VALUE}>)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << FEATURE_VALUE << '\n'; }
CPP
printf 'sentinel\n' >"$PROJECT/build/sentinel"
CMAKE_PREFIX_PATH="$ROOT/prefix" "$VIX_BIN" build app --dir "$PROJECT" --release --cmake=-DFEATURE_VALUE=ON >/dev/null
contexts=("$PROJECT"/.vix/build/*)
test "${#contexts[@]}" -eq 1
test -f "${contexts[0]}/context.toml"
test -f "${contexts[0]}/backend/CMakeCache.txt"
out="$(CMAKE_PREFIX_PATH="$ROOT/prefix" "$VIX_BIN" run app --dir "$PROJECT" --release --cmake=-DFEATURE_VALUE=ON)"
out="$(normalize_pty_output "$out")"
grep -Fxq '1' <<<"$out"
test -f "$PROJECT/build/sentinel"
echo "native CMake discovery tests passed"
