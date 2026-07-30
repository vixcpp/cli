#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/vix-native-discovery-test.XXXXXX")"
trap 'rm -rf "$ROOT"' EXIT

make_main() {
  local dir="$1"
  cat > "$dir/main.cpp" <<'CPP'
int main() { return 0; }
CPP
}

assert_no_sdk_error() {
  local log="$1"
  if grep -q "Missing SDK modules" "$log"; then
    cat "$log" >&2
    exit 1
  fi
  if grep -q "vix upgrade --sdk" "$log"; then
    cat "$log" >&2
    exit 1
  fi
}

make_test_dependency_prefix() {
  local prefix="$1"
  mkdir -p "$prefix/lib/cmake/TestDependency"
  cat > "$prefix/lib/cmake/TestDependency/TestDependencyConfig.cmake" <<'CMAKE'
if(NOT TARGET test::dependency)
  add_library(test::dependency INTERFACE IMPORTED)
endif()
CMAKE
}

make_vix_prefix() {
  local prefix="$1"
  mkdir -p "$prefix/lib/cmake/Vix"
  cat > "$prefix/lib/cmake/Vix/VixConfig.cmake" <<'CMAKE'
foreach(name IN ITEMS core json db websocket async error sync time utils)
  if(NOT TARGET vix::${name})
    add_library(vix::${name} INTERFACE IMPORTED)
  endif()
endforeach()
CMAKE
}

run_build_log() {
  local log="$1"
  shift
  "$@" >"$log" 2>&1
  assert_no_sdk_error "$log"
}

HOME_NATIVE="$ROOT/home-native"
mkdir -p "$HOME_NATIVE"

GENERIC="$ROOT/generic"
mkdir -p "$GENERIC"
make_main "$GENERIC"
cat > "$GENERIC/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(generic_native LANGUAGES CXX)
add_executable(app main.cpp)
CMAKE
run_build_log "$ROOT/generic.log" env HOME="$HOME_NATIVE" "$VIX_BIN" build --clean --preset release --dir "$GENERIC"

TEST_PREFIX="$ROOT/test-prefix"
make_test_dependency_prefix "$TEST_PREFIX"
GENERIC_DEP="$ROOT/generic-dependency"
mkdir -p "$GENERIC_DEP"
make_main "$GENERIC_DEP"
cat > "$GENERIC_DEP/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(generic_dependency LANGUAGES CXX)
find_package(TestDependency CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE test::dependency)
CMAKE
run_build_log "$ROOT/generic-dependency.log" env HOME="$HOME_NATIVE" CMAKE_PREFIX_PATH="$TEST_PREFIX" "$VIX_BIN" build --clean --preset release --dir "$GENERIC_DEP"

VIX_PREFIX="$ROOT/system-like-vix"
make_vix_prefix "$VIX_PREFIX"
VIX_PROJECT="$ROOT/vix-prefix-project"
mkdir -p "$VIX_PROJECT"
make_main "$VIX_PROJECT"
cat > "$VIX_PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(vix_prefix_project LANGUAGES CXX)
find_package(Vix CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE vix::core vix::json vix::db vix::websocket)
CMAKE
run_build_log "$ROOT/vix-prefix.log" env HOME="$HOME_NATIVE" CMAKE_PREFIX_PATH="$VIX_PREFIX" "$VIX_BIN" build --clean --preset release --dir "$VIX_PROJECT"

VIX_DIR_PROJECT="$ROOT/vix-dir-project"
mkdir -p "$VIX_DIR_PROJECT"
make_main "$VIX_DIR_PROJECT"
cat > "$VIX_DIR_PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(vix_dir_project LANGUAGES CXX)
find_package(Vix CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE vix::core)
CMAKE
run_build_log "$ROOT/vix-dir.log" env HOME="$HOME_NATIVE" Vix_DIR="$VIX_PREFIX/lib/cmake/Vix" "$VIX_BIN" build --clean --preset release --dir "$VIX_DIR_PROJECT"

VENDORED="$ROOT/vendored"
mkdir -p "$VENDORED/vendor/dep"
make_main "$VENDORED"
cat > "$VENDORED/vendor/dep/CMakeLists.txt" <<'CMAKE'
add_library(vendor_dependency INTERFACE)
add_library(vendor::dependency ALIAS vendor_dependency)
CMAKE
cat > "$VENDORED/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(vendored_dependency LANGUAGES CXX)
add_subdirectory(vendor/dep)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE vendor::dependency)
CMAKE
run_build_log "$ROOT/vendored.log" env HOME="$HOME_NATIVE" "$VIX_BIN" build --clean --preset release --dir "$VENDORED"

VIX_LOCAL_TARGETS="$ROOT/vix-local-target-spelling"
mkdir -p "$VIX_LOCAL_TARGETS"
make_main "$VIX_LOCAL_TARGETS"
cat > "$VIX_LOCAL_TARGETS/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(vix_local_target_spelling LANGUAGES CXX)
foreach(name IN ITEMS core json db websocket async error sync time utils)
  add_library(vix_${name}_local INTERFACE)
  add_library(vix::${name} ALIAS vix_${name}_local)
endforeach()
add_executable(app main.cpp)
target_link_libraries(app PRIVATE vix::core vix::json vix::db vix::websocket)
CMAKE
run_build_log "$ROOT/vix-local-target-spelling.log" env HOME="$HOME_NATIVE" "$VIX_BIN" build --clean --preset release --dir "$VIX_LOCAL_TARGETS"

WATCH_PROJECT="$ROOT/watch-project"
mkdir -p "$WATCH_PROJECT"
make_main "$WATCH_PROJECT"
cat > "$WATCH_PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(watch_native_dependency LANGUAGES CXX)
find_package(TestDependency CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE test::dependency)
CMAKE
set +e
timeout 6s env HOME="$HOME_NATIVE" CMAKE_PREFIX_PATH="$TEST_PREFIX" "$VIX_BIN" build --watch --clean --preset release --dir "$WATCH_PROJECT" >"$ROOT/watch.log" 2>&1
watch_status=$?
set -e
if [[ "$watch_status" -ne 0 && "$watch_status" -ne 124 && "$watch_status" -ne 130 && "$watch_status" -ne 143 ]]; then
  cat "$ROOT/watch.log" >&2
  exit 1
fi
assert_no_sdk_error "$ROOT/watch.log"
if ! grep -Eq "initial build|Waiting|Finished" "$ROOT/watch.log"; then
  cat "$ROOT/watch.log" >&2
  exit 1
fi

echo "native CMake discovery tests passed"
