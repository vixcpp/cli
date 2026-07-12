#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
cleanup() { rm -rf "$ROOT"; }
trap cleanup EXIT

export HOME="$ROOT/home"
mkdir -p "$HOME"

git_init_fixture() {
  local dir="$1"
  git -C "$dir" init -q
  git -C "$dir" config user.email test@example.invalid
  git -C "$dir" config user.name "Vix Test"
  git -C "$dir" add .
  git -C "$dir" commit -q -m init
  git -C "$dir" rev-parse HEAD
}

make_fake_cmake() {
  local dir="$1"
  mkdir -p "$dir"
  cat > "$dir/cmake" <<'SH'
#!/usr/bin/env bash
echo "cmake must not be invoked for this test" >&2
exit 87
SH
  chmod +x "$dir/cmake"
}

CMAKE_REPO="$ROOT/sample-cmake"
mkdir -p "$CMAKE_REPO/include/sample" "$CMAKE_REPO/src"
cat > "$CMAKE_REPO/include/sample/sample.hpp" <<'HPP'
#pragma once
namespace sample { int answer(); }
HPP
cat > "$CMAKE_REPO/src/sample.cpp" <<'CPP'
#include <sample/sample.hpp>
namespace sample { int answer() { return 42; } }
CPP
cat > "$CMAKE_REPO/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(sample_cmake LANGUAGES CXX)
add_library(sample_lib src/sample.cpp)
add_library(sample::sample ALIAS sample_lib)
target_include_directories(sample_lib PUBLIC include)
CMAKE
CMAKE_COMMIT="$(git_init_fixture "$CMAKE_REPO")"

HEADER_REPO="$ROOT/sample-headers"
mkdir -p "$HEADER_REPO/include/headers"
cat > "$HEADER_REPO/include/headers/headers.hpp" <<'HPP'
#pragma once
namespace headers { inline int answer() { return 7; } }
HPP
HEADER_COMMIT="$(git_init_fixture "$HEADER_REPO")"

PROJECT="$ROOT/project"
mkdir -p "$PROJECT/tests"
cat > "$PROJECT/vix.app" <<APP
name = "run_perf"
type = "executable"
standard = "c++20"
sources = ["tests/test.cpp"]

[dependencies.sample]
git = "$CMAKE_REPO"
rev = "$CMAKE_COMMIT"
target = "sample::sample"

[dependencies.headers]
git = "$HEADER_REPO"
rev = "$HEADER_COMMIT"
header_only = true
include = "include"
APP

cat > "$PROJECT/tests/test.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "Hello\\n"; }
CPP
cat > "$PROJECT/tests/use_sample.cpp" <<'CPP'
#include <sample/sample.hpp>
int main() { return sample::answer() == 42 ? 0 : 1; }
CPP
cat > "$PROJECT/tests/use_headers.cpp" <<'CPP'
#include <headers/headers.hpp>
int main() { return headers::answer() == 7 ? 0 : 1; }
CPP

(cd "$PROJECT" && "$VIX_BIN" install >/dev/null)
test -e "$PROJECT/.vix/vix_deps.cmake"

FAKE_BIN="$ROOT/fake-bin"
make_fake_cmake "$FAKE_BIN"

(cd "$PROJECT/tests" && PATH="$FAKE_BIN:$PATH" "$VIX_BIN" run test.cpp >/dev/null)

(cd "$PROJECT/tests" && "$VIX_BIN" run use_sample.cpp >/dev/null)

(cd "$PROJECT/tests" && PATH="$FAKE_BIN:$PATH" "$VIX_BIN" run use_headers.cpp >/dev/null)

(cd "$PROJECT/tests" && PATH="$FAKE_BIN:$PATH" "$VIX_BIN" run test.cpp >/dev/null)

TEMP_RUN="$ROOT/temp-run"
mkdir -p "$TEMP_RUN"
cat > "$TEMP_RUN/main.cpp" <<'CPP'
#include <sample/sample.hpp>
int main() { return sample::answer() == 42 ? 0 : 1; }
CPP
(cd "$TEMP_RUN" && "$VIX_BIN" run main.cpp --dep "git=$CMAKE_REPO;rev=$CMAKE_COMMIT;target=sample::sample" >/dev/null)
test ! -e "$TEMP_RUN/vix.app"
test ! -e "$TEMP_RUN/vix.lock"
test ! -e "$TEMP_RUN/.vix"
