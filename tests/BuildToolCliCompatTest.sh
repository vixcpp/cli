#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="${TMPDIR:-/tmp}/vix-cli-build-tool-compat-$$"
PROJECT="$ROOT/project"

cleanup() {
  rm -rf "$ROOT"
}
trap cleanup EXIT

mkdir -p "$PROJECT"
cat > "$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(vix_cli_build_tool_compat LANGUAGES CXX)
add_executable(vix_cli_build_tool_compat main.cpp)
CMAKE

cat > "$PROJECT/main.cpp" <<'CPP'
int main() { return 0; }
CPP

set +e
invalid_linker_output="$("$VIX_BIN" build --linker invalid --dir "$PROJECT" 2>&1)"
invalid_linker_status=$?
set -e
test "$invalid_linker_status" -eq 2
printf '%s\n' "$invalid_linker_output" | grep -q "Invalid value for --linker: invalid"
printf '%s\n' "$invalid_linker_output" | grep -q "Valid: auto, default, mold, lld"

set +e
invalid_launcher_output="$("$VIX_BIN" build --launcher invalid --dir "$PROJECT" 2>&1)"
invalid_launcher_status=$?
set -e
test "$invalid_launcher_status" -eq 2
printf '%s\n' "$invalid_launcher_output" | grep -q "Invalid value for --launcher: invalid"
printf '%s\n' "$invalid_launcher_output" | grep -q "Valid: auto, none, sccache, ccache"

set +e
"$VIX_BIN" build --targets >/dev/null
targets_status=$?
set -e
test "$targets_status" -eq 0 -o "$targets_status" -eq 255

CCACHE_DISABLE=1 "$VIX_BIN" build --launcher none --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --launcher auto --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --linker default --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --linker auto --dir "$PROJECT" >/dev/null
