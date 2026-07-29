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
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --build-target all --launcher none --linker default --dir "$PROJECT" >"$WATCH_OUT" 2>&1 &
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

wait_for_count() {
  local needle="$1"
  local expected="$2"
  local file="${3:-$WATCH_OUT}"
  local deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    local count
    count="$(grep -c -- "$needle" "$file" || true)"
    if (( count >= expected )); then
      return 0
    fi
    sleep 0.05
  done
  cat "$file" >&2
  echo "timed out waiting for $expected matches of '$needle'" >&2
  exit 1
}

wait_for_output "Watching.*all"
wait_for_output "Finished.*initial build.* in "
wait_for_output "Waiting.*for changes"

wait_for_output "Compiling.*all"
reject_output "Watching project files"
reject_output "➜"
reject_output "^Change "
reject_output "^Building incremental graph"

printf 'ignored\n' >"$PROJECT/build-ninja/ignored.tmp"
sleep 0.2
if [[ "$(grep -c 'Finished.*src/' "$WATCH_OUT" || true)" != "0" ]]; then
  cat "$WATCH_OUT" >&2
  echo "build-directory event produced rebuild output" >&2
  exit 1
fi

cat >"$PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two() + three() + 1; }
CPP

wait_for_output "Finished.*src/main.cpp.* in "
sleep 0.4
if [[ "$(grep -c 'Finished.*src/main.cpp.* in ' "$WATCH_OUT" || true)" != "1" ]]; then
  cat "$WATCH_OUT" >&2
  echo "expected one compact rebuild line for one source save" >&2
  exit 1
fi

cat >"$PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two() + three() + 1; }
CPP
sleep 0.5
if [[ "$(grep -c 'Finished.*src/main.cpp.* in ' "$WATCH_OUT" || true)" != "1" ]]; then
  cat "$WATCH_OUT" >&2
  echo "same-content source save produced a rebuild" >&2
  exit 1
fi

cat >"$PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two() + three() + 2; }
CPP
wait_for_count "Finished.*src/main.cpp.* in " 2

reject_output "^change  "
reject_output "Change "
reject_output "Classification"
reject_output "incremental"
reject_output "Rebuilt shop"

cat >"$PROJECT/src/one.cpp" <<'CPP'
int one() { return 1; }
CPP
cat >"$PROJECT/src/two.cpp" <<'CPP'
int two() { return 2; }
CPP
cat >"$PROJECT/src/three.cpp" <<'CPP'
int three() { return 3; }
CPP

wait_for_output "Finished.*3 files.* in "

cat >>"$PROJECT/CMakeLists.txt" <<'CMAKE'
# watch reconfigure
CMAKE

wait_for_output "Finished.*reconfigured.*CMakeLists.txt.* in .*full refresh"
if grep -A1 'Finished.*reconfigured.*CMakeLists.txt.* in ' "$WATCH_OUT" | grep -q 'Finished.*src/'; then
  cat "$WATCH_OUT" >&2
  echo "reconfigure iteration printed an extra rebuild line" >&2
  exit 1
fi

cat >"$PROJECT/src/main.cpp" <<'CPP'
int main() { return nope; }
CPP

wait_for_output "Error.*rebuilding.*src/main.cpp"
wait_for_output "nope"
reject_output "waiting for changes"
reject_output "ninja: Entering directory"

cat >"$PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return one() + two() + three(); }
CPP

wait_for_output "Finished.*src/main.cpp.* in "

kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || code=$?
WATCH_PID=""
test "${code:-130}" = "130"
reject_output "stopped"
reject_output "Stopped build watcher"
reject_output "Watching stopped"

if grep -q $'\r' "$WATCH_OUT"; then
  cat "$WATCH_OUT" >&2
  echo "redirected compact watch output contains carriage returns" >&2
  exit 1
fi

VERBOSE_OUT="$ROOT/verbose.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --build-target all --verbose --launcher none --linker default --dir "$PROJECT" >"$VERBOSE_OUT" 2>&1 &
WATCH_PID=$!
wait_for_output "Watching.*all" "$VERBOSE_OUT"
wait_for_output "^  .*backend.*inotify.*·.*target.*all.*·.*jobs" "$VERBOSE_OUT"
wait_for_output "^  .*build.*build-ninja" "$VERBOSE_OUT"
wait_for_output "Finished.*initial build.* in " "$VERBOSE_OUT"
wait_for_output "Waiting.*for changes" "$VERBOSE_OUT"

cat >"$PROJECT/src/two.cpp" <<'CPP'
int two() { return 20; }
CPP

wait_for_output "Finished.*src/two.cpp.* in " "$VERBOSE_OUT"
reject_output "ninja: Entering directory" "$VERBOSE_OUT"
reject_output "\\[[0-9]*/" "$VERBOSE_OUT"
reject_output "^change  " "$VERBOSE_OUT"
reject_output "Change " "$VERBOSE_OUT"
reject_output "$PROJECT/build-ninja" "$VERBOSE_OUT"
reject_output "Classification" "$VERBOSE_OUT"
reject_output "Affected tasks" "$VERBOSE_OUT"
reject_output "incremental" "$VERBOSE_OUT"
reject_output "watch classification" "$VERBOSE_OUT"
reject_output "Rebuilt all" "$VERBOSE_OUT"
reject_output "Finished.*rebuilt" "$VERBOSE_OUT"
reject_output "Stopped build watcher" "$VERBOSE_OUT"
reject_output "Watching project files" "$VERBOSE_OUT"
reject_output "➜" "$VERBOSE_OUT"
kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || true
WATCH_PID=""

EXPLAIN_OUT="$ROOT/explain.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --build-target all --verbose --explain --launcher none --linker default --dir "$PROJECT" >"$EXPLAIN_OUT" 2>&1 &
WATCH_PID=$!
wait_for_output "Watching.*all" "$EXPLAIN_OUT"
wait_for_output "Waiting.*for changes" "$EXPLAIN_OUT"

cat >"$PROJECT/src/one.cpp" <<'CPP'
int one() { return 10; }
CPP

wait_for_output "affected tasks" "$EXPLAIN_OUT"
wait_for_output "Finished.*src/one.cpp.* in " "$EXPLAIN_OUT"
reject_output "Change " "$EXPLAIN_OUT"
reject_output "Classification" "$EXPLAIN_OUT"
reject_output "Affected tasks" "$EXPLAIN_OUT"
kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || true
WATCH_PID=""

CMAKE_VERBOSE_OUT="$ROOT/cmake-verbose.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 stdbuf -oL -eL "$VIX_BIN" build --watch --build-target all --cmake-verbose --launcher none --linker default --dir "$PROJECT" >"$CMAKE_VERBOSE_OUT" 2>&1 &
WATCH_PID=$!
wait_for_output "Watching.*all" "$CMAKE_VERBOSE_OUT"
wait_for_output "Waiting.*for changes" "$CMAKE_VERBOSE_OUT"

cat >"$PROJECT/src/three.cpp" <<'CPP'
int three() { return 30; }
CPP

wait_for_output "ninja: Entering directory" "$CMAKE_VERBOSE_OUT"
wait_for_output "Finished.*src/three.cpp.* in " "$CMAKE_VERBOSE_OUT"
reject_output "Rebuilt all" "$CMAKE_VERBOSE_OUT"
reject_output "Stopped build watcher" "$CMAKE_VERBOSE_OUT"
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

cat >"$NATIVE_PROJECT/src/one.cpp" <<'CPP'
int one() { return 3; }
CPP

wait_for_output "Finished.*src/one.cpp.* in " "$NATIVE_OUT"
reject_output "^Change " "$NATIVE_OUT"
reject_output "^Building incremental graph" "$NATIVE_OUT"
reject_output "Configuring.*project graph" "$NATIVE_OUT"

kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || true
WATCH_PID=""

if command -v script >/dev/null 2>&1; then
  TTY_PROJECT="$ROOT/tty-native-shop"
  mkdir -p "$TTY_PROJECT/src/shop/presentation/controllers" "$TTY_PROJECT/include" "$ROOT/bin"

  REAL_CXX="$(command -v c++)"
  cat >"$ROOT/bin/c++" <<SH
#!/usr/bin/env bash
sleep 0.6
exec "$REAL_CXX" "\$@"
SH
  chmod +x "$ROOT/bin/c++"

  cat >"$TTY_PROJECT/vix.app" <<'APP'
name = "tty_shop"
type = "executable"
standard = "c++20"
sources = [
  "src/main.cpp",
  "src/shop/presentation/controllers/ExtremelyLongHealthControllerNameForTerminalTruncation.cpp"
]
include_dirs = ["include"]
APP

  cat >"$TTY_PROJECT/include/shop.hpp" <<'HPP'
#pragma once
int health();
HPP

  cat >"$TTY_PROJECT/src/main.cpp" <<'CPP'
#include "shop.hpp"
int main() { return health(); }
CPP
  cat >"$TTY_PROJECT/src/shop/presentation/controllers/ExtremelyLongHealthControllerNameForTerminalTruncation.cpp" <<'CPP'
int health() { return 0; }
CPP

  TTY_OUT="$ROOT/tty-watch.out"
  : >"$TTY_OUT"
  script -q -f "$TTY_OUT" -c "env HOME='$HOME_DIR' CCACHE_DISABLE=1 PATH='$ROOT/bin:$PATH' '$VIX_BIN' build --watch --launcher none --linker default --dir '$TTY_PROJECT'" >/dev/null 2>&1 &
  WATCH_PID=$!
  wait_for_output "Waiting.*for changes" "$TTY_OUT"

  cat >"$TTY_PROJECT/src/shop/presentation/controllers/ExtremelyLongHealthControllerNameForTerminalTruncation.cpp" <<'CPP'
int health() { return 1; }
CPP

  wait_for_output "Building .*[.][.][.].*ExtremelyLongHealthControllerNameForTerminalTruncation.cpp" "$TTY_OUT"
  wait_for_output "Linking tty_shop" "$TTY_OUT"
  wait_for_output "Finished.*[.][.][.].*ExtremelyLongHealthControllerNameForTerminalTruncation.cpp.* in " "$TTY_OUT"

  if grep -q 'Finished.*Building ' "$TTY_OUT"; then
    cat "$TTY_OUT" >&2
    echo "final watch line contains leftover dynamic progress text" >&2
    exit 1
  fi

  kill -INT "$WATCH_PID" 2>/dev/null || true
  wait "$WATCH_PID" || true
  WATCH_PID=""

  reject_output "Stopped build watcher" "$TTY_OUT"
  reject_output "➜ Stopped" "$TTY_OUT"
fi

echo "BuildWatchCliTest passed"
