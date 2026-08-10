#!/usr/bin/env bash
# A compiled dependency is only covered when its implementation links and runs.
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
fail() { echo "RunCompiledDependencyContractTest: $*" >&2; exit 1; }
commit_repo() { git -C "$1" init -q; git -C "$1" config user.email test@example.invalid; git -C "$1" config user.name 'Vix test'; git -C "$1" add .; git -C "$1" commit -qm initial; git -C "$1" rev-parse HEAD; }

LIB_B="$ROOT/lib-b"; mkdir -p "$LIB_B/include/b" "$LIB_B/src"
cat >"$LIB_B/include/b/b.hpp" <<'CPP'
#pragma once
int b_value();
CPP
cat >"$LIB_B/src/b.cpp" <<'CPP'
#include <b/b.hpp>
int b_value() { return 40; }
CPP
cat >"$LIB_B/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(lib_b LANGUAGES CXX)
add_library(b STATIC src/b.cpp)
add_library(b::b ALIAS b)
target_include_directories(b PUBLIC include)
CMAKE
B_REV="$(commit_repo "$LIB_B")"

LIB_A="$ROOT/lib-a"; mkdir -p "$LIB_A/include/a" "$LIB_A/src"
cat >"$LIB_A/include/a/a.hpp" <<'CPP'
#pragma once
int a_value();
CPP
cat >"$LIB_A/src/a.cpp" <<'CPP'
#include <a/a.hpp>
#include <b/b.hpp>
int a_value() { return b_value() + 2; }
CPP
cat >"$LIB_A/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(lib_a LANGUAGES CXX)
add_library(a STATIC src/a.cpp)
add_library(a::a ALIAS a)
target_include_directories(a PUBLIC include)
target_link_libraries(a PUBLIC b::b)
CMAKE
A_REV="$(commit_repo "$LIB_A")"

PROJECT="$ROOT/project"; mkdir -p "$PROJECT"
cat >"$PROJECT/vix.app" <<APP
name = "compiled-dependency-contract"
type = "executable"
standard = "c++20"
sources = ["main.cpp"]

[dependencies.b]
git = "$LIB_B"
rev = "$B_REV"
target = "b::b"

[dependencies.a]
git = "$LIB_A"
rev = "$A_REV"
target = "a::a"
APP
cat >"$PROJECT/main.cpp" <<'CPP'
#include <a/a.hpp>
#include <iostream>
int main() { std::cout << a_value() << '\n'; }
CPP
(cd "$PROJECT" && "$VIX_BIN" install >/dev/null)
out="$(cd "$PROJECT" && "$VIX_BIN" run main.cpp --no-san --trace-cache 2>&1 | tr -d '\r')"
grep -Fq 'script strategy: cmake fallback' <<<"$out" || fail "compiled dependency did not use CMake fallback"
grep -Fxq '42' <<<"$out" || fail "compiled transitive dependency did not link/run"

out="$(cd "$PROJECT" && "$VIX_BIN" run main.cpp --no-san --trace-cache 2>&1 | tr -d '\r')"
grep -Fq 'cmake graph cache: hit' <<<"$out" || fail "unchanged compiled dependency did not use graph cache"
grep -Fxq '42' <<<"$out" || fail "warm compiled dependency did not run cached executable"

# Change compiled implementation in the installed fixture. A reused executable
# would still print 42, so this proves real dependency rebuild propagation.
cat >"$PROJECT/.vix/deps/b/src/b.cpp" <<'CPP'
#include <b/b.hpp>
int b_value() { return 50; }
CPP
out="$(cd "$PROJECT" && "$VIX_BIN" run main.cpp --no-san --trace-cache 2>&1 | tr -d '\r')"
grep -Fq 'cmake graph cache: miss' <<<"$out" || fail "changed compiled dependency did not invalidate graph cache: $out"
grep -Fxq '52' <<<"$out" || fail "changed compiled dependency reused stale executable"

# A requested but absent library is a distinct linkage failure from a missing
# implementation in source. It must fail instead of silently producing an old
# executable.
if out="$(cd "$PROJECT" && "$VIX_BIN" run main.cpp --no-san -- -lcontract_missing_library 2>&1)"; then
  fail "missing library unexpectedly linked"
fi
grep -Eiq 'cannot find|link error|linking failed' <<<"$out" || fail "missing library lacked a linker diagnostic"
echo "RunCompiledDependencyContractTest passed"
