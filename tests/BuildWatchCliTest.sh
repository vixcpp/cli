#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"
trap 'if [[ -n "${WATCH_PID:-}" ]]; then kill -INT "$WATCH_PID" 2>/dev/null || true; wait "$WATCH_PID" 2>/dev/null || true; fi; rm -rf "$ROOT"' EXIT

PROJECT="$ROOT/shop"
HOME_DIR="$ROOT/home"
mkdir -p "$PROJECT/src" "$PROJECT/include" "$HOME_DIR"

cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(shop LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(shop
  src/main.cpp
  src/one.cpp
  src/two.cpp
  src/three.cpp)
target_include_directories(shop PRIVATE include)
CMAKE

cat >"$PROJECT/include/shop.hpp" <<'HPP'
#pragma once
int one();
int two();
int three();
HPP

cat >"$PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two() + three(); }
CPP
cat >"$PROJECT/src/one.cpp" <<'CPP'
int one() { return 0; }
CPP
cat >"$PROJECT/src/two.cpp" <<'CPP'
int two() { return 0; }
CPP
cat >"$PROJECT/src/three.cpp" <<'CPP'
int three() { return 0; }
CPP

help_output="$("$VIX_BIN" build --help)"
grep -q -- "--watch" <<<"$help_output"

if HOME="$HOME_DIR" "$VIX_BIN" build --watch --warnings --dir "$PROJECT" >"$ROOT/warnings.out" 2>&1; then
  echo "--watch --warnings unexpectedly succeeded" >&2
  exit 1
fi
grep -q -- "Options --watch and --warnings cannot be used together." "$ROOT/warnings.out"

if HOME="$HOME_DIR" "$VIX_BIN" build --watch --report --dir "$PROJECT" >"$ROOT/report.out" 2>&1; then
  echo "--watch --report unexpectedly succeeded" >&2
  exit 1
fi
grep -q -- "Options --watch and --report cannot be used together." "$ROOT/report.out"

WATCH_OUT="$ROOT/watch.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --launcher none --linker default --dir "$PROJECT" >"$WATCH_OUT" 2>&1 &
WATCH_PID=$!

wait_for_output() {
  local needle="$1"
  local file="${2:-$WATCH_OUT}"
  local deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    if grep -q -- "$needle" "$file"; then
      return 0
    fi
    if [[ "$file" == "$WATCH_OUT" ]] && ! kill -0 "$WATCH_PID" 2>/dev/null; then
      cat "$file" >&2
      echo "watch process exited before '$needle'" >&2
      exit 1
    fi
    sleep 0.05
  done
  cat "$file" >&2
  echo "timed out waiting for '$needle'" >&2
  exit 1
}

reject_output() {
  local needle="$1"
  local file="${2:-$WATCH_OUT}"
  if grep -q -- "$needle" "$file"; then
    cat "$file" >&2
    echo "unexpected output: $needle" >&2
    exit 1
  fi
}

wait_for_output "Watching.*shop"
wait_for_output "Finished.*initial build.* in "
wait_for_output "Waiting.*for changes"

if grep -q '^[[:space:]]' "$WATCH_OUT"; then
  cat "$WATCH_OUT" >&2
  echo "compact watch output has leading whitespace" >&2
  exit 1
fi

reject_output "Compiling shop"
reject_output "build \\["
reject_output "Configured"
reject_output "Built ("
reject_output "Done in"
reject_output "Watching project files"
reject_output "➜"

printf 'ignored\n' >"$PROJECT/build-ninja/ignored.tmp"
sleep 0.2
if [[ "$(grep -c 'Finished.*rebuilt' "$WATCH_OUT" || true)" != "0" ]]; then
  cat "$WATCH_OUT" >&2
  echo "build-directory event produced rebuild output" >&2
  exit 1
fi

cat >"$PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two() + three() + 1; }
CPP

wait_for_output "Change.*src/main.cpp"
wait_for_output "Building.*incremental graph"
wait_for_output "Finished.*rebuilt.*src/main.cpp.* in "
sleep 0.4
if [[ "$(grep -c 'Finished.*rebuilt.*src/main.cpp.* in ' "$WATCH_OUT" || true)" != "1" ]]; then
  cat "$WATCH_OUT" >&2
  echo "expected one compact rebuild line for one source save" >&2
  exit 1
fi

reject_output "^change  "
reject_output "ninja:"
reject_output "Building CXX object"
reject_output "Linking CXX executable"
reject_output "Rebuilt shop"
reject_output "\\[1/"
reject_output "\\[2/"

cat >"$PROJECT/src/one.cpp" <<'CPP'
int one() { return 1; }
CPP
cat >"$PROJECT/src/two.cpp" <<'CPP'
int two() { return 2; }
CPP
cat >"$PROJECT/src/three.cpp" <<'CPP'
int three() { return 3; }
CPP

wait_for_output "Change.*3 files"
wait_for_output "Finished.*rebuilt.*3 files.* in "

cat >>"$PROJECT/CMakeLists.txt" <<'CMAKE'
# watch reconfigure
CMAKE

wait_for_output "Change.*CMakeLists.txt"
wait_for_output "Configuring.*project graph"
wait_for_output "Finished.*reconfigured.*CMakeLists.txt.* in "
if grep -A1 'Finished.*reconfigured.*CMakeLists.txt.* in ' "$WATCH_OUT" | grep -q 'Finished.*rebuilt'; then
  cat "$WATCH_OUT" >&2
  echo "reconfigure iteration printed an extra rebuild line" >&2
  exit 1
fi

cat >"$PROJECT/src/main.cpp" <<'CPP'
int main() { return nope; }
CPP

wait_for_output "Error.*src/main.cpp.*rebuild failed"
wait_for_output "nope"
reject_output "waiting for changes"
reject_output "ninja: Entering directory"

cat >"$PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two() + three(); }
CPP

wait_for_output "Finished.*rebuilt.*src/main.cpp.* in "

kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || code=$?
WATCH_PID=""
test "${code:-130}" = "130"
reject_output "stopped"
reject_output "Stopped build watcher"
reject_output "Watching stopped"

if grep -n '^$' "$WATCH_OUT" >/dev/null; then
  cat "$WATCH_OUT" >&2
  echo "compact watch output contains blank lines" >&2
  exit 1
fi

VERBOSE_OUT="$ROOT/verbose.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --verbose --launcher none --linker default --dir "$PROJECT" >"$VERBOSE_OUT" 2>&1 &
WATCH_PID=$!
wait_for_output "Watching project files" "$VERBOSE_OUT"
wait_for_output "watch backend:" "$VERBOSE_OUT"
kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || true
WATCH_PID=""

QUIET_OUT="$ROOT/quiet.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --quiet --launcher none --linker default --dir "$PROJECT" >"$QUIET_OUT" 2>&1 &
WATCH_PID=$!
sleep 1
kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || true
WATCH_PID=""
if [[ -s "$QUIET_OUT" ]]; then
  cat "$QUIET_OUT" >&2
  echo "quiet watch produced successful-output text" >&2
  exit 1
fi

NATIVE_PROJECT="$ROOT/native-shop"
mkdir -p "$NATIVE_PROJECT/src" "$NATIVE_PROJECT/include"

cat >"$NATIVE_PROJECT/vix.app" <<'APP'
name = "native_shop"
type = "executable"
standard = "c++20"
sources = ["src/main.cpp", "src/one.cpp", "src/two.cpp"]
include_dirs = ["include"]
APP

cat >"$NATIVE_PROJECT/include/shop.hpp" <<'HPP'
#pragma once
int one();
int two();
HPP

cat >"$NATIVE_PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two(); }
CPP
cat >"$NATIVE_PROJECT/src/one.cpp" <<'CPP'
int one() { return 1; }
CPP
cat >"$NATIVE_PROJECT/src/two.cpp" <<'CPP'
int two() { return 2; }
CPP

NATIVE_OUT="$ROOT/native-watch.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --launcher none --linker default --dir "$NATIVE_PROJECT" >"$NATIVE_OUT" 2>&1 &
WATCH_PID=$!
wait_for_output "Watching.*native_shop" "$NATIVE_OUT"
wait_for_output "Waiting.*for changes" "$NATIVE_OUT"

if grep -q '^[[:space:]]' "$NATIVE_OUT"; then
  cat "$NATIVE_OUT" >&2
  echo "native compact watch output has leading whitespace" >&2
  exit 1
fi

cat >"$NATIVE_PROJECT/src/one.cpp" <<'CPP'
int one() { return 3; }
CPP

wait_for_output "Change.*src/one.cpp" "$NATIVE_OUT"
wait_for_output "Building.*incremental graph" "$NATIVE_OUT"
wait_for_output "Finished.*rebuilt.*src/one.cpp.* in " "$NATIVE_OUT"
reject_output "Configuring.*project graph" "$NATIVE_OUT"

kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || true
WATCH_PID=""

echo "BuildWatchCliTest passed"
