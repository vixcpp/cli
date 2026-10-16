#!/usr/bin/env bash
# Standalone compilation reuses correct dependency freshness, not public cache knobs.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/include"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
cat >"$ROOT/include/value.hpp" <<'CPP'
#pragma once
inline int value() { return 1; }
CPP
cat >"$ROOT/main.cpp" <<'CPP'
#include <iostream>
#include "value.hpp"
int main() { std::cout << value() << '\n'; }
CPP
run() { "$VIX_BIN" run "$ROOT/main.cpp" --diagnostics=off --compiler-arg="-I$ROOT/include"; }
out="$(normalize_pty_output "$(run)")"
grep -Fxq '1' <<<"$out" || { printf 'First run output: %q\n' "$out" >&2; echo 'initial standalone build output mismatch' >&2; exit 1; }
out="$(normalize_pty_output "$(run)")"
grep -Fxq '1' <<<"$out" || { printf 'No-op run output: %q\n' "$out" >&2; echo 'unchanged standalone run output mismatch' >&2; exit 1; }
cat >"$ROOT/include/value.hpp" <<'CPP'
#pragma once
inline int value() { return 7; }
CPP
out="$(normalize_pty_output "$(run)")"
grep -Fxq '7' <<<"$out" || {
  printf 'Header-change run output: %q\n' "$out" >&2
  echo 'changed header yielded stale executable' >&2
  exit 1
}
for option in --trace-cache --no-cache --compiler-fingerprint=fast; do
  if "$VIX_BIN" run "$ROOT/main.cpp" "$option" >/dev/null 2>&1; then
    echo "retired cache control accepted: $option" >&2; exit 1
  fi
done
echo "RunCacheContractTest passed"
