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

help_output="$("$VIX_BIN" build --help)"
printf '%s\n' "$help_output" | grep -q -- '--debug'
printf '%s\n' "$help_output" | grep -q -- '--debug-log <scope>'
printf '%s\n' "$help_output" | grep -q -- '--log <scope>'
printf '%s\n' "$help_output" | grep -q -- '--graph-executor <mode>'
printf '%s\n' "$help_output" | grep -q -- '--heartbeat'
printf '%s\n' "$help_output" | grep -q -- '--no-heartbeat'
printf '%s\n' "$help_output" | grep -q 'Platform:'
if printf '%s\n' "$help_output" | grep -q 'Environment variables:'; then
  echo "build help still exposes environment variables" >&2
  exit 1
fi

for args in \
  '--debug-log invalid' \
  '--log invalid' \
  '--graph-executor invalid'; do
  set +e
  "$VIX_BIN" build $args --dir "$PROJECT" >/dev/null 2>&1
  status=$?
  set -e
  test "$status" -eq 2
done

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

targets_output="$("$VIX_BIN" build --targets)"
printf '%s\n' "$targets_output" | grep -q 'Available targets'
printf '%s\n' "$targets_output" | grep -q 'native'
printf '%s\n' "$targets_output" | grep -q 'unavailable'

CCACHE_DISABLE=1 "$VIX_BIN" build --launcher none --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --launcher auto --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --linker default --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --linker auto --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --target native --heartbeat --graph-executor auto --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --debug --debug-log cache --dir "$PROJECT" >/dev/null
CCACHE_DISABLE=1 "$VIX_BIN" build --debug-log all --dir "$PROJECT" >/dev/null

printf 'build-log-content\n' > "$PROJECT/build-ninja/build.log"
printf 'configure-log-content\n' > "$PROJECT/build-ninja/configure.log"
"$VIX_BIN" build --log build --dir "$PROJECT" | grep -q 'build-log-content'
"$VIX_BIN" build --log configure --dir "$PROJECT" | grep -q 'configure-log-content'
"$VIX_BIN" build --log all --dir "$PROJECT" | grep -q 'Configure log'
