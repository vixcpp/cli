#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?path to vix executable required}"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"
export XDG_CACHE_HOME="$ROOT/cache"
export XDG_CONFIG_HOME="$ROOT/config"
export XDG_DATA_HOME="$ROOT/data"
mkdir -p "$HOME" "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"

fail() { echo "RunAdvancedOptionsContractTest: $*" >&2; exit 1; }
expect_failure() {
  local label="$1"; shift
  if "$@" >"$ROOT/failure.out" 2>"$ROOT/failure.err"; then
    fail "$label unexpectedly succeeded"
  fi
}
assert_no_backend_output() {
  if grep -Eiq 'ninja:|no work to do|Building CXX object|Linking CXX|Configuring done|Generating done|cmake --build' "$@"; then
    cat "$@" >&2
    fail "successful run exposed backend chatter"
  fi
}

# Standalone compiler/linker escape hatches are exact, repeatable channels.
cat >"$ROOT/single.cpp" <<'CPP'
#include <iostream>
int main(int argc, char **argv) {
  std::cout << "SINGLE=" << I2_ORDER << " ARG=" << (argc > 1 ? argv[1] : "") << '\n';
}
CPP
"$VIX_BIN" build "$ROOT/single.cpp" --compiler-arg=-DI2_ORDER=0 >"$ROOT/managed-build.out" 2>"$ROOT/managed-build.err" || fail "single-source managed build failed"
test ! -e "$ROOT/single" || fail "single-source build exported an executable without --out"
MAP_FILE="$ROOT/link-map.txt"
"$VIX_BIN" build "$ROOT/single.cpp" --out "$ROOT/single-built" \
  --compiler-arg=-UI2_ORDER --compiler-arg=-DI2_ORDER=1 \
  --compiler-arg=-UI2_ORDER --compiler-arg=-DI2_ORDER=2 \
  --linker-arg="-Wl,-Map,$MAP_FILE" >"$ROOT/build.out" 2>"$ROOT/build.err" || fail "single-source build arguments failed"
test -s "$MAP_FILE" || fail "linker argument did not reach linker"
BUILT_OUT="$("$ROOT/single-built")" || fail "exported single-source artifact failed to execute"
grep -Fq 'SINGLE=2 ARG=' <<<"$BUILT_OUT" || fail "repeated compiler arguments did not preserve order"
RUN_OUT="$("$VIX_BIN" run "$ROOT/single.cpp" \
  --compiler-arg=-DI2_ORDER=2 \
  --linker-arg="-Wl,-Map,$ROOT/run-link-map.txt" \
  -- --release 2>"$ROOT/single-run.err")" || fail "single-source run arguments failed"
grep -Fq 'SINGLE=2 ARG=--release' <<<"$RUN_OUT" || fail "compiler args or runtime separator were misrouted"
test -s "$ROOT/run-link-map.txt" || fail "run linker argument did not reach linker"

# Native project scope checks happen before build-state creation or execution.
NATIVE="$ROOT/native"
mkdir -p "$NATIVE/runtime"
cat >"$NATIVE/vix.toml" <<'TOML'
format = 1
[project]
name = "advanced-options-native"
[[targets]]
name = "app"
kind = "executable"
sources = ["main.cpp"]
TOML
cat >"$NATIVE/main.cpp" <<'CPP'
#include <filesystem>
#include <iostream>
int main(int argc, char **argv) {
  std::cout << "CWD=" << std::filesystem::current_path().string()
            << " ARG=" << (argc > 1 ? argv[1] : "") << '\n';
}
CPP
expect_failure "invalid cwd" bash -c 'cd "$1" && "$2" run app --cwd missing' _ "$NATIVE" "$VIX_BIN"
test ! -e "$NATIVE/.vix/build" || fail "invalid cwd reached build preparation"
expect_failure "native --cmake" "$VIX_BIN" build --dir "$NATIVE" --cmake -DVALUE=1
test ! -e "$NATIVE/.vix/build" || fail "native --cmake reached backend"
expect_failure "native compiler args" "$VIX_BIN" run --dir "$NATIVE" --compiler-arg=-DVALUE=1
test ! -e "$NATIVE/.vix/build" || fail "project compiler args reached backend"

NATIVE_OUT="$(cd "$NATIVE" && "$VIX_BIN" run app --cwd runtime -- --release 2>"$ROOT/native.err")" ||
  fail "native cwd/runtime arguments failed"
grep -Fq "CWD=$NATIVE/runtime ARG=--release" <<<"$NATIVE_OUT" || fail "project-relative cwd or runtime argv is incorrect"

# External CMake configure arguments preserve sequence; run remains quiet on success.
CMAKE="$ROOT/cmake"
mkdir -p "$CMAKE"
cat >"$CMAKE/CMakeLists.txt" <<'CMAKEFILE'
cmake_minimum_required(VERSION 3.20)
project(advanced_options LANGUAGES CXX)
if(NOT DEFINED VALUE)
  set(VALUE 0)
endif()
add_executable(app main.cpp)
target_compile_definitions(app PRIVATE VALUE=${VALUE})
CMAKEFILE
cat >"$CMAKE/main.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "CMAKE_VALUE=" << VALUE << '\n'; }
CPP
"$VIX_BIN" build --dir "$CMAKE" --cmake -DVALUE=1 --cmake=-DVALUE=2 \
  >"$ROOT/cmake-build.out" 2>"$ROOT/cmake-build.err" || fail "external CMake build arguments failed"
"$VIX_BIN" run app --dir "$CMAKE" --cmake -DVALUE=1 --cmake=-DVALUE=2 \
  >"$ROOT/cmake-run.out" 2>"$ROOT/cmake-run.err" || fail "external CMake run failed"
grep -Fq 'CMAKE_VALUE=2' "$ROOT/cmake-run.out" "$ROOT/cmake-run.err" || fail "ordered CMake arguments were not applied"
assert_no_backend_output "$ROOT/cmake-run.out" "$ROOT/cmake-run.err"
expect_failure "external CMake compiler args" "$VIX_BIN" run app --dir "$CMAKE" --compiler-arg=-DVALUE=3
expect_failure "single-source --cmake" "$VIX_BIN" build "$ROOT/single.cpp" --cmake -DVALUE=4

echo "RunAdvancedOptionsContractTest passed"
