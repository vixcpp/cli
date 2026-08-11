#!/usr/bin/env bash
# Deterministic contracts for global parsing and the implicit source-file route.
set -euo pipefail

VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/vix-cli-global.XXXXXX)"
trap 'rm -rf "$TEST_ROOT"' EXIT
export HOME="$TEST_ROOT/home"
export XDG_CACHE_HOME="$TEST_ROOT/cache"
export XDG_CONFIG_HOME="$TEST_ROOT/config"
mkdir -p "$HOME" "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME"

expect_ok() { "$@" >"$TEST_ROOT/out" 2>"$TEST_ROOT/err"; }
expect_fail() {
  if "$@" >"$TEST_ROOT/out" 2>"$TEST_ROOT/err"; then
    echo "expected failure: $*" >&2; exit 1
  fi
}

expect_ok "$VIX_BIN" --help
grep -q 'Global options' "$TEST_ROOT/out"
expect_ok "$VIX_BIN" -h
grep -q 'Usage' "$TEST_ROOT/out"
expect_ok "$VIX_BIN" --version
grep -q 'version' "$TEST_ROOT/out"
expect_ok "$VIX_BIN" -v
grep -q 'version' "$TEST_ROOT/out"

for level in trace debug info warn error critical; do
  expect_ok "$VIX_BIN" --log-level "$level" --version
  expect_ok "$VIX_BIN" "--log-level=$level" --version
done
expect_fail "$VIX_BIN" --log-level
grep -q 'requires a value' "$TEST_ROOT/err"
expect_fail "$VIX_BIN" --log-level= --version
grep -q 'cannot be empty' "$TEST_ROOT/err"
expect_fail "$VIX_BIN" --log-level invalid --version
grep -q 'Invalid log level' "$TEST_ROOT/err"

# The explicit and implicit paths must both enter `run`, not command lookup.
expect_fail "$VIX_BIN" run "$TEST_ROOT/missing.cpp"
explicit="$(cat "$TEST_ROOT/out" "$TEST_ROOT/err")"
expect_fail "$VIX_BIN" "$TEST_ROOT/missing.cpp"
implicit="$(cat "$TEST_ROOT/out" "$TEST_ROOT/err")"
grep -qi 'script\|file' <<<"$explicit"
grep -qi 'script\|file' <<<"$implicit"

expect_fail "$VIX_BIN" buid
grep -q 'build' "$TEST_ROOT/err"
expect_fail "$VIX_BIN" completely-unknown-command

echo "GlobalCliContractTest passed"
