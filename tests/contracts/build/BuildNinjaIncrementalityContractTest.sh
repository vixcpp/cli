#!/usr/bin/env bash
# CMake/Ninja owns C++ dependency and CONFIGURE_DEPENDS invalidation.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
PROJECT="$ROOT/generic-cmake"; mkdir -p "$PROJECT/src" "$PROJECT/include"
fail() { echo "BuildNinjaIncrementalityContractTest: $*" >&2; exit 1; }
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(generic_incrementality LANGUAGES CXX)
file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(generic_app ${SOURCES})
target_include_directories(generic_app PRIVATE include)
target_compile_definitions(generic_app PRIVATE CONFIG_VALUE=${CONFIG_VALUE})
CMAKE
printf '#pragma once\nint value();\nconstexpr int header_version = 1;\n' >"$PROJECT/include/value.hpp"
printf '#include "value.hpp"\nint value() { return 1; }\n' >"$PROJECT/src/value.cpp"
printf '#include "value.hpp"\nint main() { return value() == CONFIG_VALUE && header_version >= 1 ? 0 : 1; }\n' >"$PROJECT/src/main.cpp"
build() { "$VIX_BIN" build generic_app --dir "$PROJECT" --cmake="-DCONFIG_VALUE=$1" >/dev/null; }
run() { "$VIX_BIN" run generic_app --dir "$PROJECT" --cmake="-DCONFIG_VALUE=${1:?configuration value required}" >/dev/null || fail "unexpected executable result"; }
build 1; run 1                  # initial + configured no-op baseline
backend_dirs=("$PROJECT"/.vix/build/*/backend)
test -f "${backend_dirs[0]}/CMakeCache.txt" || fail "CMake state is not in a Vix-owned context"
build 1; run 1                  # no-op
printf '#include "value.hpp"\nint value() { return 2; }\n' >"$PROJECT/src/value.cpp"
build 2; run 2                  # .cpp dependency is Ninja-owned
printf '#pragma once\nint value();\nconstexpr int header_version = 3;\n' >"$PROJECT/include/value.hpp"
printf '#include "value.hpp"\nint main() { return value() == CONFIG_VALUE && header_version == 3 ? 0 : 1; }\n' >"$PROJECT/src/main.cpp"
build 2; run 2                  # .hpp dependency is Ninja-owned
printf 'int extra() { return 0; }\n' >"$PROJECT/src/extra.cpp"
build 2; run 2                  # CONFIGURE_DEPENDS adds a source
rm "$PROJECT/src/extra.cpp"
build 2; run 2                  # CONFIGURE_DEPENDS removes it
printf '\ntarget_compile_definitions(generic_app PRIVATE CMAKE_LISTS_CHANGED=1)\n' >>"$PROJECT/CMakeLists.txt"
build 2; run 2                  # CMakeLists invalidates configure
build 2; run 2                  # effective CMake variable remains valid
echo "BuildNinjaIncrementalityContractTest passed"
