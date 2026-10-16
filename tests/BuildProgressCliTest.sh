#!/usr/bin/env bash
# Build owns user-facing progress; implicit run builds stay silent on success.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project"
PROJECT="$ROOT/project"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(output_contract LANGUAGES CXX)
add_executable(app main.cpp)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "OUTPUT_CONTRACT_MARKER\n"; }
CPP
"$VIX_BIN" build app --dir "$PROJECT" >"$ROOT/build.out" 2>&1
if ! grep -Eiq 'Compiling.*app|Finished.*app' "$ROOT/build.out"; then
  cat "$ROOT/build.out" >&2; echo 'vix build did not present Vix-owned build progress' >&2; exit 1
fi
if grep -Eiq 'ninja:|cmake --build|-- Configuring done|-- Generating done|\[[0-9]+/[0-9]+\] Building|\] Linking CXX' "$ROOT/build.out"; then
  cat "$ROOT/build.out" >&2; echo 'raw backend progress leaked from vix build' >&2; exit 1
fi
run_output="$("$VIX_BIN" run app --dir "$PROJECT" 2>&1)"
run_output="$(normalize_pty_output "$run_output")"
grep -Fxq 'OUTPUT_CONTRACT_MARKER' <<<"$run_output"
if grep -Eiq 'ninja:|no work to do|cmake --build|-- Configuring done|-- Generating done|\[[0-9]+/[0-9]+\] Building|\] Linking CXX' <<<"$run_output"; then
  printf '%s\n' "$run_output" >&2; echo 'successful implicit build chatter leaked from vix run' >&2; exit 1
fi
echo "BuildProgressCliTest passed"
