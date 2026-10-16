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

fail() { echo "TestsCommandContractCliTest: $*" >&2; exit 1; }

PROJECT="$ROOT/cmake-project"
mkdir -p "$PROJECT"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(cli_i3_test_contract LANGUAGES CXX)
include(CTest)
add_executable(test_fixture main.cpp)
add_test(NAME network.alpha COMMAND test_fixture)
set_tests_properties(network.alpha PROPERTIES LABELS "integration")
add_test(NAME networkXalpha COMMAND test_fixture)
add_test(NAME Network.alpha COMMAND test_fixture)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
int main() { return 0; }
CPP

FILTER_OUT="$("$VIX_BIN" test --dir "$PROJECT" 'network.')" || fail "literal test filter failed"
grep -Fq 'network.alpha' <<<"$FILTER_OUT" || fail "literal filter omitted matching registration"
if grep -Fq 'networkXalpha' <<<"$FILTER_OUT"; then fail "filter treated dot as a regex wildcard"; fi
if grep -Fq 'Network.alpha' <<<"$FILTER_OUT"; then fail "filter was not case-sensitive"; fi

CTEST_OUT="$("$VIX_BIN" test --dir "$PROJECT" --ctest -L integration -N)" || fail "terminal CTest forwarding failed"
grep -Fq 'network.alpha' <<<"$CTEST_OUT" || fail "CTest label argument was not forwarded"
if grep -Fq 'networkXalpha' <<<"$CTEST_OUT"; then fail "CTest label filter was not applied"; fi

assert_parse_rejected() {
  local expected="$1"
  shift
  local output status
  if output="$("$VIX_BIN" test "$@" 2>&1)"; then
    fail "invalid test syntax was accepted: $*"
  else
    status=$?
  fi
  [[ "$status" -eq 2 ]] || fail "invalid syntax returned $status instead of 2: $*"
  grep -Fq -- "$expected" <<<"$output" || fail "missing parse diagnostic for: $*"
}

assert_parse_rejected '--ctest cannot be combined' filter --ctest -N
assert_parse_rejected '--ctest cannot be combined' --list --ctest -N
assert_parse_rejected '--ctest cannot be combined' --watch --ctest -N
assert_parse_rejected '--ctest cannot be combined' --fail-fast --ctest -N
assert_parse_rejected 'Diagnostic options cannot be combined' --san --ctest -N

NATIVE="$ROOT/native-project"
mkdir -p "$NATIVE"
: >"$NATIVE/vix.toml"
if output="$("$VIX_BIN" test --dir "$NATIVE" --ctest -N 2>&1)"; then
  fail "terminal CTest mode was accepted for a native Vix project"
fi
grep -Fq -- '--ctest is available only for external CMake projects.' <<<"$output" ||
  fail "native CTest rejection was not clear"

echo "TestsCommandContractCliTest passed"
