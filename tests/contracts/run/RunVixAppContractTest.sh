#!/usr/bin/env bash
# A public Vix package must provide both headers and compiled target usage
# requirements. The script path must therefore take CMake fallback, not guess
# a subset of static libraries.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
VIX_ROOT="$(cd "$(dirname "$VIX_BIN")" && pwd)"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
export VIX_ROOT
cat >"$ROOT/app.cpp" <<'CPP'
#include <vix.hpp>
int main() { vix::App app; return 0; }
CPP
out="$("$VIX_BIN" run "$ROOT/app.cpp" --no-san --trace-cache 2>&1 | tr -d '\r')"
grep -Fq 'script strategy: cmake fallback' <<<"$out"
