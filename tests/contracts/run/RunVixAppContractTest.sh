#!/usr/bin/env bash
# Historical vix.app files do not establish project authority in current Vix.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
PROJECT="$ROOT/project"
cat >"$PROJECT/vix.app" <<'APP'
name = "retired-authority"
type = "executable"
sources = ["main.cpp"]
APP
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "current-project-authority\n"; }
CPP
if (cd "$PROJECT" && "$VIX_BIN" run) >"$ROOT/no-authority.out" 2>&1; then
  echo 'historical vix.app unexpectedly established project authority' >&2
  cat "$ROOT/no-authority.out" >&2
  exit 1
fi
grep -Fq -- 'No Vix or CMake project was found for this run target.' "$ROOT/no-authority.out" || {
  cat "$ROOT/no-authority.out" >&2
  echo 'legacy-only directory did not fail through project discovery' >&2
  exit 1
}
if grep -Fq -- 'current-project-authority' "$ROOT/no-authority.out"; then
  cat "$ROOT/no-authority.out" >&2
  echo 'legacy source was executed without project authority' >&2
  exit 1
fi
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(current_authority LANGUAGES CXX)
add_executable(current_authority main.cpp)
CMAKE
out="$("$VIX_BIN" run current_authority --dir "$PROJECT")"
out="$(normalize_pty_output "$out")"
grep -Fxq 'current-project-authority' <<<"$out" || {
  printf '%s\n' "$out" >&2
  echo 'recognized CMake authority did not execute its target' >&2
  exit 1
}
echo "RunVixAppContractTest passed"
