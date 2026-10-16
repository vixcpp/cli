#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?path to vix executable required}"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"
export XDG_CACHE_HOME="$ROOT/cache"
export XDG_CONFIG_HOME="$ROOT/config"
export XDG_DATA_HOME="$ROOT/data"
mkdir -p "$HOME" "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"

fail() { echo "ExternalCMakeBuildStateCliTest: $*" >&2; exit 1; }

PROJECT="$ROOT/project"
mkdir -p "$PROJECT" "$PROJECT/build" "$PROJECT/build-dev" "$PROJECT/build-release"
printf 'user-build\n' >"$PROJECT/build/sentinel"
printf 'user-build-dev\n' >"$PROJECT/build-dev/sentinel"
printf 'user-build-release\n' >"$PROJECT/build-release/sentinel"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(external_context LANGUAGES CXX)
if(NOT DEFINED VALUE)
  set(VALUE 0)
endif()
add_executable(app main.cpp)
target_compile_definitions(app PRIVATE VALUE=${VALUE})
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "VALUE=" << VALUE << '\n'; }
CPP

"$VIX_BIN" build --dir "$PROJECT" --cmake=-DVALUE=1 --cmake=-DVALUE=2 \
  >"$ROOT/build-a.out" 2>"$ROOT/build-a.err" || fail "ordered CMake context A build failed"

BACKENDS=("$PROJECT"/.vix/build/*/backend)
[[ ${#BACKENDS[@]} -eq 1 && -d "${BACKENDS[0]}" ]] || fail "build A was not placed in one Vix-owned backend context"
BACKEND_A="${BACKENDS[0]}"
CONTEXT_A="${BACKEND_A%/backend}"
[[ -f "$CONTEXT_A/context.toml" && -f "$BACKEND_A/CMakeCache.txt" ]] || fail "context A metadata/backend state missing"

"$VIX_BIN" build --dir "$PROJECT" --cmake=-DVALUE=2 --cmake=-DVALUE=1 \
  >"$ROOT/build-b.out" 2>"$ROOT/build-b.err" || fail "ordered CMake context B build failed"
BACKENDS=("$PROJECT"/.vix/build/*/backend)
[[ ${#BACKENDS[@]} -eq 2 ]] || fail "different ordered CMake arguments reused one context"
BACKEND_B=""
for backend in "${BACKENDS[@]}"; do
  [[ "$backend" == "$BACKEND_A" ]] || BACKEND_B="$backend"
done
[[ -n "$BACKEND_B" ]] || fail "second CMake context not found"
CONTEXT_B="${BACKEND_B%/backend}"
[[ -f "$CONTEXT_B/context.toml" && -f "$BACKEND_B/CMakeCache.txt" ]] || fail "context B metadata/backend state missing"

printf 'selected-context-only\n' >"$BACKEND_A/clean-sentinel"
printf 'preserve-other-context\n' >"$BACKEND_B/clean-sentinel"

RUN_OUT="$("$VIX_BIN" run app --dir "$PROJECT" --cmake=-DVALUE=1 --cmake=-DVALUE=2 \
  2>"$ROOT/run.err")" || fail "File API target execution failed"
grep -Fq 'VALUE=2' <<<"$RUN_OUT" || fail "run did not use the artifact from the selected File API context"
backend_output_pattern='ninja:|no work to do|Building CXX object|Linking CXX|Configuring done|Generating done|cmake --build'
if grep -Eiq "$backend_output_pattern" "$ROOT/run.err" ||
   grep -Eiq "$backend_output_pattern" <<<"$RUN_OUT"; then
  fail "successful implicit run exposed external CMake backend output"
fi

"$VIX_BIN" build --dir "$PROJECT" --cmake=-DVALUE=1 --cmake=-DVALUE=2 --clean \
  >"$ROOT/clean.out" 2>"$ROOT/clean.err" || fail "selected-context clean/rebuild failed"
[[ -f "$CONTEXT_A/context.toml" && -f "$BACKEND_A/CMakeCache.txt" ]] || fail "selected context was not recreated"
[[ -f "$CONTEXT_B/context.toml" && -f "$BACKEND_B/CMakeCache.txt" ]] || fail "clean removed the other context"
[[ ! -e "$BACKEND_A/clean-sentinel" ]] || fail "clean did not remove the selected context"
[[ -f "$BACKEND_B/clean-sentinel" ]] || fail "clean removed state from the other context"
[[ -f "$PROJECT/build/sentinel" && -f "$PROJECT/build-dev/sentinel" && -f "$PROJECT/build-release/sentinel" ]] ||
  fail "Vix clean touched a user-owned build directory"

echo "ExternalCMakeBuildStateCliTest passed"
