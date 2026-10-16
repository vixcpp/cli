#!/usr/bin/env bash
# Current project runs use discovery, positional targets and File API artifacts.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project/runtime" "$ROOT/project/build"
fail() { echo "RunExecutionPathsContractTest: $*" >&2; exit 1; }
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
PROJECT="$ROOT/project"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(run_project_contract LANGUAGES CXX)
add_executable(run_project main.cpp)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
#include <cstdlib>
#include <filesystem>
#include <iostream>
int main(int argc, char **argv) {
  std::cout << "project:" << (argc > 1 ? argv[1] : "") << ':'
            << (std::getenv("RUN_PATH_ENV") ?: "") << ':'
            << std::filesystem::current_path().filename().string() << '\n';
}
CPP
printf 'user-owned\n' >"$PROJECT/build/sentinel"
out="$("$VIX_BIN" run run_project --dir "$PROJECT" --cwd runtime \
  --env RUN_PATH_ENV=ok -- project-arg)" || fail "discovered CMake target did not run"
out="$(normalize_pty_output "$out")"
grep -Fxq 'project:project-arg:ok:runtime' <<<"$out" || fail "target argv/env/cwd were not forwarded"
test -f "$PROJECT/build/sentinel" || fail "user-owned build directory was changed"
if "$VIX_BIN" run "$ROOT/not-a-project-binary" >/dev/null 2>&1; then
  fail "arbitrary binary-path execution was accepted"
fi
for retired in 'http://fixture.invalid/app' 'ssh://fixture.invalid/app' 'docker://fixture/app'; do
  if "$VIX_BIN" run "$retired" >/dev/null 2>&1; then fail "retired execution URI was accepted: $retired"; fi
done
echo "RunExecutionPathsContractTest passed"
