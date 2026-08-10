#!/usr/bin/env bash
# A changed local header must invalidate direct-script cache. This is a real
# success contract and currently fails, preserving the regression visibly.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/include"
cat >"$ROOT/include/nested.hpp" <<'CPP'
#pragma once
inline int nested_value() { return 1; }
CPP
cat >"$ROOT/include/value.hpp" <<'CPP'
#pragma once
#include "nested.hpp"
inline int value() { return nested_value(); }
CPP
cat >"$ROOT/main.cpp" <<'CPP'
#include "value.hpp"
#include <iostream>
int main() { std::cout << value() << '\n'; }
CPP
out="$("$VIX_BIN" run "$ROOT/main.cpp" --no-san --trace-cache -- -I"$ROOT/include" 2>&1 | tr -d '\r')"
grep -Fxq '1' <<<"$out"
out="$("$VIX_BIN" run "$ROOT/main.cpp" --no-san --trace-cache -- -I"$ROOT/include" 2>&1 | tr -d '\r')"
grep -Fq 'rebuild reason: cache hit' <<<"$out"
touch "$ROOT/include/value.hpp"
out="$("$VIX_BIN" run "$ROOT/main.cpp" --no-san --trace-cache -- -I"$ROOT/include" 2>&1 | tr -d '\r')"
grep -Fq 'rebuild reason: cache hit' <<<"$out"
cat >"$ROOT/include/value.hpp" <<'CPP'
#pragma once
#include "nested.hpp"
inline int value() { return nested_value() + 10; }
CPP
out="$("$VIX_BIN" run "$ROOT/main.cpp" --no-san --trace-cache -- -I"$ROOT/include" 2>&1 | tr -d '\r')"
if grep -Fq 'rebuild reason: cache hit' <<<"$out"; then
  printf '%s\n' "$out" >&2
  echo "changed local header incorrectly reused direct-script cache" >&2
  exit 1
fi
grep -Fxq '11' <<<"$out"

cat >"$ROOT/include/nested.hpp" <<'CPP'
#pragma once
inline int nested_value() { return 2; }
CPP
out="$("$VIX_BIN" run "$ROOT/main.cpp" --no-san --trace-cache -- -I"$ROOT/include" 2>&1 | tr -d '\r')"
if grep -Fq 'rebuild reason: cache hit' <<<"$out"; then
  printf '%s\n' "$out" >&2
  echo "changed transitive local header incorrectly reused direct-script cache" >&2
  exit 1
fi
grep -Fxq '12' <<<"$out"
