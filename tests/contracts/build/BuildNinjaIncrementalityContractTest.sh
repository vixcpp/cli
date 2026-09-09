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
build() { "$VIX_BIN" build --dir "$PROJECT" --build-target generic_app --graph-executor off --no-cache --launcher none --linker default -- "$@" >/dev/null; }
run() { "$PROJECT/build-ninja/generic_app" || fail "unexpected executable result"; }
build -DCONFIG_VALUE=1; run                    # initial + configured no-op baseline
test ! -e "$PROJECT/build-ninja/.vix-build-state" || fail "normal Ninja build maintained Vix input state"
build -DCONFIG_VALUE=1; run                    # no-op
printf '#include "value.hpp"\nint value() { return 2; }\n' >"$PROJECT/src/value.cpp"
build -DCONFIG_VALUE=2; run                    # .cpp dependency is Ninja-owned
printf '#pragma once\nint value();\nconstexpr int header_version = 3;\n' >"$PROJECT/include/value.hpp"
printf '#include "value.hpp"\nint main() { return value() == CONFIG_VALUE && header_version == 3 ? 0 : 1; }\n' >"$PROJECT/src/main.cpp"
build -DCONFIG_VALUE=3; run                    # .hpp dependency is Ninja-owned
printf 'int extra() { return 0; }\n' >"$PROJECT/src/extra.cpp"
build -DCONFIG_VALUE=3; run                    # CONFIGURE_DEPENDS adds a source
rm "$PROJECT/src/extra.cpp"
build -DCONFIG_VALUE=3; run                    # CONFIGURE_DEPENDS removes it
printf '\ntarget_compile_definitions(generic_app PRIVATE CMAKE_LISTS_CHANGED=1)\n' >>"$PROJECT/CMakeLists.txt"
build -DCONFIG_VALUE=3; run                    # CMakeLists invalidates configure
build -DCONFIG_VALUE=3; run                    # effective CMake variable remains valid
echo "BuildNinjaIncrementalityContractTest passed"
