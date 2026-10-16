#!/usr/bin/env bash
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
PROJECT="$ROOT/project with spaces"; mkdir -p "$PROJECT"
export HOME="$ROOT/home"; mkdir -p "$HOME"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
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
#include <iostream>
int main() { std::cout << "BUILD_CORE_TARGET\n"; }
CPP
"$VIX_BIN" build --dir "$PROJECT" contract >/dev/null
out="$("$VIX_BIN" run contract --dir "$PROJECT")" || fail "File API target did not execute"
out="$(normalize_pty_output "$out")"
grep -Fxq 'BUILD_CORE_TARGET' <<<"$out" || fail "program output was not preserved"
for retired in --sanitize --sanitize=address --no-san; do
  expect_parse_error "$VIX_BIN" build "$retired"
done
for conflict in '--san --asan' '--asan --ubsan' '--asan --tsan' '--ubsan --tsan' '--san --san'; do
  # shellcheck disable=SC2086 # Deliberate option-list expansion for parser coverage.
  expect_parse_error "$VIX_BIN" build $conflict
done
for mode in --san --asan --ubsan --tsan; do
  set +e
  output="$("$VIX_BIN" build --dir "$PROJECT" "$mode" 2>&1)"
  code=$?
  set -e
  [[ $code != 0 ]] || fail "external CMake accepted $mode without Vix instrumentation"
  grep -Fq 'Vix diagnostic modes are not available for arbitrary external CMake projects.' <<<"$output" ||
    fail "external CMake did not fail closed for $mode"
done
echo "BuildCoreContractTest passed"
