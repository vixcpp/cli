#!/usr/bin/env bash
# Deterministic contracts for global parsing and explicit source-file routing.
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
for option in '-h, --help' '-v, --version' '--verbose' '-q, --quiet'; do
  grep -Fq -- "$option" "$TEST_ROOT/out" || { echo "Global help omits $option" >&2; exit 1; }
done
for option in '--log-level' '--color' '--debug'; do
  if grep -Fq -- "$option" "$TEST_ROOT/out"; then
    echo "Global help exposes retired option $option" >&2; exit 1
  fi
done
expect_ok "$VIX_BIN" -h
grep -q 'Usage' "$TEST_ROOT/out"
expect_ok "$VIX_BIN" --version
grep -q 'version' "$TEST_ROOT/out"
expect_ok "$VIX_BIN" -v
grep -q 'version' "$TEST_ROOT/out"

expect_fail "$VIX_BIN" --log-level
grep -q 'unrecognized subcommand' "$TEST_ROOT/err"
if grep -q -- '--log-level' "$TEST_ROOT/out"; then
  echo 'Global help still exposes --log-level' >&2; exit 1
fi

expect_fail "$VIX_BIN" --verbose --quiet
grep -q 'cannot be used together' "$TEST_ROOT/err"
expect_fail "$VIX_BIN" --verbose build --quiet
grep -q 'cannot be used together' "$TEST_ROOT/err"

# Source files require an explicit command; there is no implicit top-level route.
expect_fail "$VIX_BIN" run "$TEST_ROOT/missing.cpp"
explicit="$(cat "$TEST_ROOT/out" "$TEST_ROOT/err")"
expect_fail "$VIX_BIN" "$TEST_ROOT/missing.cpp"
implicit="$(cat "$TEST_ROOT/out" "$TEST_ROOT/err")"
grep -qi 'script\|file' <<<"$explicit"
grep -qi 'explicit command: vix build' <<<"$implicit"

expect_fail "$VIX_BIN" buid
grep -q 'build' "$TEST_ROOT/err"
expect_fail "$VIX_BIN" completely-unknown-command

echo "GlobalCliContractTest passed"
