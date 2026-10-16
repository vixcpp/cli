#!/usr/bin/env bash
# Core run behavior: source input, explicit compiler settings and runtime state.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/include" "$ROOT/runtime"
fail() { echo "RunCoreContractTest: $*" >&2; exit 1; }
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
expect_rejected() {
  local output status
  set +e; output="$("$VIX_BIN" "$@" 2>&1)"; status=$?; set -e
  [[ $status -eq 2 ]] || { printf '%s\n' "$output" >&2; fail "expected parser rejection: $*"; }
}
cat >"$ROOT/include/value.hpp" <<'CPP'
#pragma once
inline int value() { return 7; }
CPP
cat >"$ROOT/main.cpp" <<'CPP'
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include "value.hpp"
int main(int argc, char **argv) {
  std::cout << "value=" << value() << " argc=" << argc
            << " arg=" << (argc > 1 ? argv[1] : "")
            << " env=" << (std::getenv("RUN_CONTRACT") ? std::getenv("RUN_CONTRACT") : "")
            << " cwd=" << std::filesystem::current_path().string() << '\n';
}
CPP
set +e
out="$("$VIX_BIN" run "$ROOT/main.cpp" --diagnostics=off \
  --compiler-arg="-I$ROOT/include" --cwd "$ROOT/runtime" \
  --env RUN_CONTRACT=yes -- alpha 2>&1)"
status=$?
set -e
[[ $status -eq 0 ]] || { printf '%s\n' "$out" >&2; fail "standalone run failed (exit $status)"; }
out="$(normalize_pty_output "$out")"
expected="value=7 argc=2 arg=alpha env=yes cwd=$ROOT/runtime"
grep -Fxq "$expected" <<<"$out" || {
  printf 'Exit status: %d\nExpected bytes: %q\nActual bytes:   %q\n' "$status" "$expected" "$out" >&2
  fail "runtime argv/env/cwd or compiler argument contract"
}

cat >"$ROOT/nonzero.cpp" <<'CPP'
int main() { return 19; }
CPP
set +e; out="$("$VIX_BIN" run "$ROOT/nonzero.cpp" --diagnostics=off 2>&1)"; status=$?; set -e
[[ $status -ne 0 ]] || fail "nonzero program exit was lost"

cat >"$ROOT/broken.cpp" <<'CPP'
int main() { does_not_compile }
CPP
set +e; out="$("$VIX_BIN" run "$ROOT/broken.cpp" --diagnostics=off 2>&1)"; status=$?; set -e
[[ $status -ne 0 ]] && grep -Fq 'does_not_compile' <<<"$out" || fail "compiler diagnostic was hidden"

for option in --trace-cache --compiler-fingerprint --clear --docs --log-format --log-color --log-level --loglevel --auto-deps --watch; do
  expect_rejected run "$ROOT/main.cpp" --diagnostics=off "$option"
done
expect_rejected run "$ROOT/main.cpp" --diagnostics=off --cwd
expect_rejected run "$ROOT/main.cpp" --diagnostics=off --env INVALID
set +e
out="$("$VIX_BIN" run "$ROOT/main.cpp" --diagnostics=off \
  --compiler-arg="-I$ROOT/include" -- --release 2>&1)"
status=$?
set -e
[[ $status -eq 0 ]] || { printf '%s\n' "$out" >&2; fail "runtime separator invocation failed (exit $status)"; }
out="$(normalize_pty_output "$out")"
grep -Fq 'arg=--release' <<<"$out" || fail "runtime -- did not pass option-like program arguments"
echo "RunCoreContractTest passed"
