#!/usr/bin/env bash
# Context isolation, ordered configure identity and scoped clean contract.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project/build" "$ROOT/project/.vix/packages/sentinel"
PROJECT="$ROOT/project"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(build_context_contract LANGUAGES CXX)
add_executable(app main.cpp)
CMAKE
printf 'int main() { return 0; }\n' >"$PROJECT/main.cpp"
printf 'user build data\n' >"$PROJECT/build/sentinel"
printf 'package data\n' >"$PROJECT/.vix/packages/sentinel/content"
build() { "$VIX_BIN" build app --dir "$PROJECT" --cmake "-DCONTEXT_VALUE=$1" >/dev/null; }
build A
context_a=("$PROJECT"/.vix/build/*)
test "${#context_a[@]}" -eq 1
id_a="${context_a[0]}"
test -f "${context_a[0]}/context.toml"
test -f "${context_a[0]}/backend/CMakeCache.txt"
build B
context_b=("$PROJECT"/.vix/build/*)
test "${#context_b[@]}" -eq 2
id_b="${context_b[0]}"
[[ "$id_b" == "$id_a" ]] && id_b="${context_b[1]}"
[[ "$id_b" != "$id_a" ]]
build A --clean
context_after=("$PROJECT"/.vix/build/*)
test "${#context_after[@]}" -eq 2
test -f "$id_a/context.toml"
test -f "$id_b/context.toml"
test -f "$PROJECT/build/sentinel"
test -f "$PROJECT/.vix/packages/sentinel/content"
echo "BuildPlanningCompatTest passed"
