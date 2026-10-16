#!/usr/bin/env bash
# Public build inputs resolve to Vix-managed artifacts without path guessing.
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
fail() { echo "BuildExecutionPathsContractTest: $*" >&2; exit 1; }

cat >"$ROOT/single.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "single-build\n"; }
CPP
"$VIX_BIN" build "$ROOT/single.cpp" >"$ROOT/managed.out" 2>"$ROOT/managed.err" || fail "managed single-source build failed"
test ! -e "$ROOT/single" || fail "single-source build exported an artifact without --out"
OUT="$ROOT/out/single-artifact"
"$VIX_BIN" build "$ROOT/single.cpp" --out "$OUT" >/dev/null || fail "explicit single-source export failed"
test -x "$OUT" || fail "single-source build did not export --out executable"
grep -Fxq 'single-build' < <("$OUT") || fail "single-source artifact is not runnable"
if "$VIX_BIN" build "$ROOT/missing.cpp" --out "$ROOT/missing" >/dev/null 2>&1; then
  fail "missing single source unexpectedly built"
fi

# External CMake target selection and execution consume CMake File API artifacts.
PROJECT="$ROOT/external"; mkdir -p "$PROJECT" "$PROJECT/build"
printf 'user-owned\\n' >"$PROJECT/build/sentinel"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(external_execution_paths LANGUAGES CXX)
add_executable(app main.cpp)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "external-target\n"; }
CPP
"$VIX_BIN" build app --dir "$PROJECT" >/dev/null || fail "positional external target build failed"
BACKENDS=("$PROJECT"/.vix/build/*/backend)
test -f "${BACKENDS[0]}/CMakeCache.txt" || fail "external CMake binary directory is not Vix-owned"
RUN_OUTPUT="$("$VIX_BIN" run app --dir "$PROJECT")" || fail "File API target artifact did not execute"
RUN_OUTPUT="$(normalize_pty_output "$RUN_OUTPUT")"
grep -Fxq 'external-target' <<<"$RUN_OUTPUT" || fail "external target output was not preserved"
test -f "$PROJECT/build/sentinel" || fail "Vix changed a user-owned build directory"

# Compiler diagnostics from an implicit target build remain visible.
cat >"$PROJECT/main.cpp" <<'CPP'
#include "missing-header-for-build-contract.hpp"
int main() { return 0; }
CPP
if "$VIX_BIN" build app --dir "$PROJECT" >"$ROOT/failure.out" 2>&1; then
  cat "$ROOT/failure.out" >&2
  fail "invalid external target source unexpectedly built"
fi
grep -Eiq 'error:|failed|unable to build' "$ROOT/failure.out" || {
  cat "$ROOT/failure.out" >&2
  fail "useful build failure diagnostics were not preserved"
}

echo "BuildExecutionPathsContractTest passed"
