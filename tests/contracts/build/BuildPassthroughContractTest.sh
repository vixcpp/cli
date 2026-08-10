#!/usr/bin/env bash
# CMake arguments after -- are a public, observable configuration contract.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
cat >"$ROOT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(passthrough LANGUAGES CXX)
option(TEST_FEATURE "contract" OFF)
add_executable(passthrough main.cpp)
if(TEST_FEATURE)
  target_compile_definitions(passthrough PRIVATE TEST_FEATURE=1)
endif()
CMAKE
cat >"$ROOT/main.cpp" <<'CPP'
#ifndef TEST_FEATURE
#error TEST_FEATURE was not propagated
#endif
int main() { return 0; }
CPP
"$VIX_BIN" build --dir "$ROOT" --launcher none --linker default -- -DTEST_FEATURE=ON
