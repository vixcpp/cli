#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"
trap 'if [[ -n "${WATCH_PID:-}" ]]; then kill -INT "$WATCH_PID" 2>/dev/null || true; wait "$WATCH_PID" 2>/dev/null || true; fi; rm -rf "$ROOT"' EXIT

PROJECT="$ROOT/project"
HOME_DIR="$ROOT/home"
mkdir -p "$PROJECT/src" "$HOME_DIR"

cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(watch_project LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(watch_project src/main.cpp)
CMAKE

cat >"$PROJECT/src/main.cpp" <<'CPP'
int main() { return 0; }
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
  local deadline=$((SECONDS + 15))
  while (( SECONDS < deadline )); do
    if grep -q -- "$needle" "$WATCH_OUT"; then
      return 0
    fi
    if ! kill -0 "$WATCH_PID" 2>/dev/null; then
      cat "$WATCH_OUT" >&2
      echo "watch process exited before '$needle'" >&2
      exit 1
    fi
    sleep 0.05
  done
  cat "$WATCH_OUT" >&2
  echo "timed out waiting for '$needle'" >&2
  exit 1
}

wait_for_output "Watching project files"

before_changes="$(grep -c '^change  ' "$WATCH_OUT" || true)"
printf 'ignored\n' >"$PROJECT/build-ninja/ignored.tmp"
sleep 0.2
after_ignored="$(grep -c '^change  ' "$WATCH_OUT" || true)"
test "$before_changes" = "$after_ignored"

cat >"$PROJECT/src/main.cpp" <<'CPP'
int main() { return 1; }
CPP

wait_for_output "change  src/main.cpp"
wait_for_output "Rebuilt project"

kill -INT "$WATCH_PID" 2>/dev/null || true
wait "$WATCH_PID" || code=$?
WATCH_PID=""
test "${code:-130}" = "130"
grep -q -- "Stopped build watcher." "$WATCH_OUT"

echo "BuildWatchCliTest passed"
