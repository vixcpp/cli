#!/usr/bin/env bash
# A public Vix package must provide both headers and compiled target usage
# requirements. The script path must therefore take CMake fallback, not guess
# a subset of static libraries.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
VIX_ROOT="${VIX_ROOT:-$(cd "$(dirname "$VIX_BIN")" && pwd)}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
export VIX_ROOT
cat >"$ROOT/app.cpp" <<'CPP'
#include <vix.hpp>
int main() { vix::App app; return 0; }
CPP
set +e
out="$("$VIX_BIN" run "$ROOT/app.cpp" --no-san --trace-cache 2>&1 | tr -d '\r')"
status=$?
set -e
grep -Fq 'script strategy: cmake fallback' <<<"$out"

# The umbrella test profile deliberately disables installation.  In that
# configuration the generated script cannot resolve a public Vix package, but
# it must still choose the CMake strategy.  When a build-tree package is
# available, keep this as a full compile-and-run regression test.
if [[ "$status" -ne 0 && -f "$VIX_ROOT/VixConfig.cmake" ]]; then
  printf '%s\n' "$out" >&2
  exit "$status"
fi
