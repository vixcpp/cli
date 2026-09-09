#!/usr/bin/env bash
# Build paths are validated by produced, runnable artifacts.
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
fail() { echo "BuildExecutionPathsContractTest: $*" >&2; exit 1; }

cat >"$ROOT/single.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "single-build\n"; }
CPP
OUT="$ROOT/out/single-artifact"
"$VIX_BIN" build "$ROOT/single.cpp" --out "$OUT" --launcher none --linker default >/dev/null
test -x "$OUT" || fail "single-file build did not export --out executable"
grep -Fxq 'single-build' < <("$OUT") || fail "single-file artifact is not runnable"
if "$VIX_BIN" build "$ROOT/missing.cpp" --out "$ROOT/missing" >/dev/null 2>&1; then
  fail "missing single source unexpectedly built"
fi

# This is deliberately a named CMake target (rather than an `all` shortcut):
# with the graph executor disabled, CMake/Ninja is the only consumer and no
# Vix graph may be imported or persisted before that backend runs.
BACKEND_PROJECT="$ROOT/backend-fixture"; mkdir -p "$BACKEND_PROJECT"
cat >"$BACKEND_PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(generic_backend_fixture LANGUAGES CXX)
add_executable(backend_alpha main.cpp)
CMAKE
cat >"$BACKEND_PROJECT/main.cpp" <<'CPP'
int main() { return 0; }
CPP
BACKEND_LOG="$ROOT/backend.log"
"$VIX_BIN" build --dir "$BACKEND_PROJECT" --build-target backend_alpha --graph-executor off --no-cache --debug --launcher none --linker default >"$BACKEND_LOG" 2>&1
test -x "$BACKEND_PROJECT/build-ninja/backend_alpha" || fail "CMake/Ninja backend did not build target"
if grep -Fq 'build graph:' "$BACKEND_LOG"; then
  cat "$BACKEND_LOG" >&2
  fail "CMake/Ninja backend imported a Vix build graph without a consumer"
fi
test ! -e "$BACKEND_PROJECT/build-ninja/.vix/build-graph.vix" || fail "CMake/Ninja backend persisted an unused Vix build graph"

PROJECT="$ROOT/graph"; mkdir -p "$PROJECT"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(graph_executor_contract LANGUAGES CXX)
add_executable(graph_app main.cpp value.cpp)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
int value();
int main() { return value() == 9 ? 0 : 1; }
CPP
cat >"$PROJECT/value.cpp" <<'CPP'
int value() { return 9; }
CPP
"$VIX_BIN" build --dir "$PROJECT" --graph-executor on --launcher none --linker default --build-target graph_app >/dev/null
test -x "$PROJECT/build-ninja/graph_app" || fail "forced graph executor did not produce target"
"$PROJECT/build-ninja/graph_app" || fail "graph executor artifact failed"

sleep 1
cat >"$PROJECT/value.cpp" <<'CPP'
#error graph executor failure propagation
CPP
if "$VIX_BIN" build --dir "$PROJECT" --graph-executor on --launcher none --linker default --build-target graph_app >"$ROOT/graph-failure.log" 2>&1; then
  cat "$ROOT/graph-failure.log" >&2
  fail "graph executor did not propagate compile failure"
fi
echo "BuildExecutionPathsContractTest passed"
