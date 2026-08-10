#!/usr/bin/env bash
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
PROJECT="$ROOT/project with spaces"; mkdir -p "$PROJECT"
export HOME="$ROOT/home"; mkdir -p "$HOME"
fail() { echo "BuildCoreContractTest: $*" >&2; exit 1; }
expect_parse_error() { set +e; local o; o="$("$@" 2>&1)"; local rc=$?; set -e; [[ $rc == 2 ]] || { printf '%s\n' "$o" >&2; fail "expected parse failure"; }; }
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(contract LANGUAGES CXX)
option(TEST_FEATURE "contract passthrough" OFF)
add_executable(contract main.cpp)
if(TEST_FEATURE)
  target_compile_definitions(contract PRIVATE TEST_FEATURE=1)
endif()
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
int main() { return 0; }
CPP
"$VIX_BIN" build --dir "$PROJECT" --launcher none --linker default --build-target contract >/dev/null
test -f "$PROJECT/build-ninja/contract" || fail "CMake passthrough/build target"
"$VIX_BIN" build --dir "$PROJECT" --no-cache --fast --clean --warning-check --launcher none --linker default >/dev/null
"$VIX_BIN" build --dir "$PROJECT" --target native --graph-executor auto --heartbeat --launcher none --linker default >/dev/null
"$VIX_BIN" build --dir "$PROJECT" --target=native --graph-executor=off --no-heartbeat --launcher=none --linker=default >/dev/null
targets="$("$VIX_BIN" build --targets)"; grep -Fq native <<<"$targets" || fail "native target missing"
for preset in dev dev-ninja release; do "$VIX_BIN" build --dir "$PROJECT" --preset="$preset" --launcher none --linker default >/dev/null; done
for mode in address undefined address,undefined thread; do "$VIX_BIN" build --dir "$PROJECT" --sanitize="$mode" --launcher none --linker default >/dev/null; done
printf 'build contract log\n' >"$PROJECT/build-ninja/build.log"
"$VIX_BIN" build --dir "$PROJECT" --log "$PROJECT/build-ninja/build.log" | grep -Fq 'build contract log' || fail "--log file"
"$VIX_BIN" build --dir "$PROJECT" --log "$PROJECT/build-ninja" | grep -Fq 'build contract log' || fail "--log directory"
expect_parse_error "$VIX_BIN" build --dir "$PROJECT" --graph-executor=bad
expect_parse_error "$VIX_BIN" build --dir "$PROJECT" --launcher=bad
expect_parse_error "$VIX_BIN" build --dir "$PROJECT" --linker=bad
expect_parse_error "$VIX_BIN" build --dir "$PROJECT" --sanitize=bad
expect_parse_error "$VIX_BIN" build --dir "$PROJECT" --page 0
expect_parse_error "$VIX_BIN" build --dir "$PROJECT" --bin --out "$ROOT/out"
echo "BuildCoreContractTest passed"
