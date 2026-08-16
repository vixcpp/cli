#!/usr/bin/env bash
# Offline contract for Git/CMake dependencies consumed through add_subdirectory.
set -euo pipefail
VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
fail() { echo "InstallCMakeCompatibilityContractTest: $*" >&2; exit 1; }
git_init() { git -C "$1" init -q; git -C "$1" config user.email contract@example.invalid; git -C "$1" config user.name contract; git -C "$1" add .; git -C "$1" commit -qm fixture; git -C "$1" tag v1.0.0; git -C "$1" rev-parse HEAD; }

REPO="$ROOT/fixture repository with spaces"; mkdir -p "$REPO/library/include/example" "$REPO/library/src" "$REPO/library/deps/libb/include/example" "$REPO/library/generated"
cat >"$REPO/library/include/example/example.hpp" <<'EOF'
#pragma once
int example_static_value(); int example_extras_value(); int example_shared_value();
EOF
cat >"$REPO/library/include/example/header_only.hpp" <<'EOF'
#pragma once
inline int example_header_only_value() { return 7; }
EOF
cat >"$REPO/library/src/example.cpp" <<'EOF'
#include <example/example.hpp>
int example_static_value() { return 11; }
EOF
cat >"$REPO/library/src/extras.cpp" <<'EOF'
#include <example/example.hpp>
int example_extras_value() { return 13; }
EOF
cat >"$REPO/library/src/shared.cpp" <<'EOF'
#include <example/example.hpp>
int example_shared_value() { return 17; }
EOF
cat >"$REPO/library/deps/libb/include/example/libb.hpp" <<'EOF'
#pragma once
#ifndef EXAMPLE_TRANSITIVE
#error "transitive compile definition missing"
#endif
inline int example_libb_value() { return EXAMPLE_TRANSITIVE; }
EOF
cat >"$REPO/library/deps/libb/CMakeLists.txt" <<'EOF'
add_library(example_libb INTERFACE)
add_library(example::libb ALIAS example_libb)
target_include_directories(example_libb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(example_libb INTERFACE EXAMPLE_TRANSITIVE=19)
target_compile_features(example_libb INTERFACE cxx_std_20)
EOF
cat >"$REPO/library/generated/config.hpp.in" <<'EOF'
#pragma once
#define EXAMPLE_GENERATED_VALUE @EXAMPLE_GENERATED_VALUE@
EOF
cat >"$REPO/library/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(example_fixture LANGUAGES CXX)
option(EXAMPLE_FEATURE "public feature" ON)
set(EXAMPLE_GENERATED_VALUE 23)
configure_file(generated/config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/example/config.hpp @ONLY)
add_subdirectory(deps/libb)
add_library(example_header_only INTERFACE)
add_library(example::header_only ALIAS example_header_only)
target_include_directories(example_header_only INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(example_header_only INTERFACE cxx_std_20)
add_library(example_static STATIC src/example.cpp)
add_library(example::static_lib ALIAS example_static)
target_include_directories(example_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(example_static PRIVATE EXAMPLE_PRIVATE_BUILD=1)
add_library(example_shared SHARED src/shared.cpp)
add_library(example::shared_lib ALIAS example_shared)
target_include_directories(example_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_library(example_extras STATIC src/extras.cpp)
add_library(example::extras ALIAS example_extras)
target_include_directories(example_extras PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(example_extras PUBLIC example::static_lib example::libb)
if(EXAMPLE_FEATURE)
  target_compile_definitions(example_extras INTERFACE EXAMPLE_FEATURE_ENABLED=1)
else()
  target_compile_definitions(example_extras INTERFACE EXAMPLE_FEATURE_ENABLED=0)
endif()
EOF
COMMIT="$(git_init "$REPO")"

APP="$ROOT/application"; mkdir -p "$APP"
cat >"$APP/vix.app" <<EOF
name = "install_contract"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]

[dependencies.example]
git = "$REPO"
tag = "v1.0.0"
subdirectory = "library"
target = "example::extras"

[dependencies.example.cmake]
EXAMPLE_FEATURE = false
EOF
cat >"$APP/main.cpp" <<'EOF'
#include <example/example.hpp>
#include <example/header_only.hpp>
#include <example/libb.hpp>
#include <example/config.hpp>
#ifdef EXAMPLE_PRIVATE_BUILD
#error "private dependency definition leaked to consumer"
#endif
#if EXAMPLE_FEATURE_ENABLED != 0
#error "CMake option did not reach consumer"
#endif
int main() { return example_static_value() + example_extras_value() + example_header_only_value() + example_libb_value() + EXAMPLE_GENERATED_VALUE == 73 ? 0 : 1; }
EOF
(cd "$APP" && "$VIX_BIN" install >/dev/null)
grep -Fq "\"commit\": \"$COMMIT\"" "$APP/vix.lock" || fail "exact tagged commit not locked"
grep -Fq 'EXAMPLE_FEATURE' "$APP/.vix/vix_deps.cmake" || fail "CMake option missing from integration"
test -e "$APP/.vix/deps/example" || fail "dependency was not materialized"
(cd "$APP" && "$VIX_BIN" build >/dev/null)
"$APP/build-ninja/install_contract"

# The same fixture verifies target aliases, an INTERFACE include-only target,
# and a shared target through real application builds.
for pair in 'header example::header_only 7' 'shared example::shared_lib 17'; do
  set -- $pair; name="$1"; target="$2"; value="$3"; dir="$ROOT/$name-app"; mkdir -p "$dir"
  cat >"$dir/vix.app" <<EOF
name = "$name"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]
[dependencies.example]
git = "$REPO"
rev = "$COMMIT"
subdirectory = "library"
target = "$target"
EOF
  cat >"$dir/main.cpp" <<EOF
#include <example/$(test "$name" = header && echo header_only.hpp || echo example.hpp)>
int main() { return $(test "$name" = header && echo example_header_only_value || echo example_shared_value)() == $value ? 0 : 1; }
EOF
  (cd "$dir" && "$VIX_BIN" install >/dev/null && "$VIX_BIN" build >/dev/null)
  "$dir/build-ninja/$name"
done
echo "InstallCMakeCompatibilityContractTest passed"
