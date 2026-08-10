#!/usr/bin/env bash
# Public run targets must execute user-visible behavior, not just classify.
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
fail() { echo "RunExecutionPathsContractTest: $*" >&2; exit 1; }

cat >"$ROOT/binary.cpp" <<'CPP'
#include <cstdlib>
#include <fstream>
int main(int argc, char **argv) {
  std::ofstream(std::getenv("RUN_PATH_RESULT"))
      << (argc > 1 ? argv[1] : "") << ':' << (std::getenv("RUN_PATH_ENV") ?: "");
}
CPP
c++ -std=c++20 "$ROOT/binary.cpp" -o "$ROOT/binary"
RUN_PATH_RESULT="$ROOT/binary-result" "$VIX_BIN" run "$ROOT/binary" --env RUN_PATH_ENV=ok --run argument
grep -Fxq 'argument:ok' "$ROOT/binary-result" || fail "binary target did not receive args/env"

PROJECT="$ROOT/project"; mkdir -p "$PROJECT"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(run_project_contract LANGUAGES CXX)
add_executable(run_project main.cpp)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
int main(int argc, char **argv) { std::cout << "project:" << (argc > 1 ? argv[1] : "") << '\n'; }
CPP
"$VIX_BIN" build --dir "$PROJECT" --launcher none --linker default --build-target run_project >/dev/null
out="$("$VIX_BIN" run "$PROJECT" --run project-arg | tr -d '\r')"
grep -Fxq 'project:project-arg' <<<"$out" || fail "project target did not run built executable"

cat >"$ROOT/manifest.cpp" <<'CPP'
#include <cstdlib>
#include <iostream>
int main() { std::cout << "manifest:" << (std::getenv("MANIFEST_ENV") ?: "") << '\n'; }
CPP
cat >"$ROOT/run.vix" <<'VIX'
[app]
kind = "script"
name = "manifest-contract"
entry = "manifest.cpp"

[run]
env = ["MANIFEST_ENV=ok"]
VIX
out="$(cd "$ROOT" && "$VIX_BIN" run run.vix --no-san | tr -d '\r')"
grep -Fxq 'manifest:ok' <<<"$out" || fail "manifest script did not propagate environment"
printf '[app]\nkind = "script"\n' >"$ROOT/bad.vix"
if "$VIX_BIN" run "$ROOT/bad.vix" >/dev/null 2>&1; then fail "malformed manifest unexpectedly ran"; fi

mkdir -p "$ROOT/bin"
cat >"$ROOT/bin/curl" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >"$RUN_URI_ARGS"
printf 'uri-contract\n'
SH
chmod +x "$ROOT/bin/curl"
out="$(PATH="$ROOT/bin:$PATH" RUN_URI_ARGS="$ROOT/uri-args" "$VIX_BIN" run 'http://fixture.invalid/index.txt' | tr -d '\r')"
grep -Fxq 'uri-contract' <<<"$out" || fail "HTTP URI adapter did not dispatch curl"
grep -Fq -- '-L http://fixture.invalid/index.txt' "$ROOT/uri-args" || fail "HTTP URI adapter passed wrong URL"

echo "RunExecutionPathsContractTest passed"
