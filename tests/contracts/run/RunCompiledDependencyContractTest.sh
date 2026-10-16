#!/usr/bin/env bash
# CMake target graphs, including transitive compiled dependencies, remain backend-owned.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project/lib-b/include/b" "$ROOT/project/lib-a/include/a"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
PROJECT="$ROOT/project"
cat >"$PROJECT/lib-b/include/b/b.hpp" <<'CPP'
#pragma once
int b_value();
CPP
printf '#include <b/b.hpp>\nint b_value() { return 40; }\n' >"$PROJECT/lib-b/b.cpp"
cat >"$PROJECT/lib-b/CMakeLists.txt" <<'CMAKE'
add_library(b STATIC b.cpp)
target_include_directories(b PUBLIC include)
CMAKE
cat >"$PROJECT/lib-a/include/a/a.hpp" <<'CPP'
#pragma once
int a_value();
CPP
printf '#include <a/a.hpp>\n#include <b/b.hpp>\nint a_value() { return b_value() + 2; }\n' >"$PROJECT/lib-a/a.cpp"
cat >"$PROJECT/lib-a/CMakeLists.txt" <<'CMAKE'
add_library(a STATIC a.cpp)
target_include_directories(a PUBLIC include)
target_link_libraries(a PUBLIC b)
CMAKE
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(transitive_dependency LANGUAGES CXX)
add_subdirectory(lib-b)
add_subdirectory(lib-a)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE a)
CMAKE
cat >"$PROJECT/main.cpp" <<'CPP'
#include <a/a.hpp>
#include <iostream>
int main() { std::cout << a_value() << '\n'; }
CPP
out="$(normalize_pty_output "$("$VIX_BIN" run app --dir "$PROJECT")")"
grep -Fxq '42' <<<"$out"
cat >"$PROJECT/lib-b/b.cpp" <<'CPP'
#include <b/b.hpp>
int b_value() { return 50; }
CPP
out="$(normalize_pty_output "$("$VIX_BIN" run app --dir "$PROJECT")")"
grep -Fxq '52' <<<"$out" || { echo 'transitive dependency change did not rebuild' >&2; exit 1; }
echo "RunCompiledDependencyContractTest passed"
