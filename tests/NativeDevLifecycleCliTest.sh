#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?path to vix executable required}"
ROOT="$(mktemp -d)"
PROJECT="$ROOT/project"
mkdir -p "$PROJECT/runtime" "$ROOT/home" "$ROOT/cache" "$ROOT/config" "$ROOT/data"

DEV_PID=""
cleanup() {
  if [[ -n "$DEV_PID" ]] && kill -0 "$DEV_PID" 2>/dev/null; then
    kill -INT "$DEV_PID" 2>/dev/null || true
    for ((i = 0; i < 30; ++i)); do
      kill -0 "$DEV_PID" 2>/dev/null || break
      sleep 0.1
    done
    kill -TERM "$DEV_PID" 2>/dev/null || true
    wait "$DEV_PID" 2>/dev/null || true
  fi
  rm -rf "$ROOT"
}
trap cleanup EXIT

export HOME="$ROOT/home"
export XDG_CACHE_HOME="$ROOT/cache"
export XDG_CONFIG_HOME="$ROOT/config"
export XDG_DATA_HOME="$ROOT/data"

fail() { echo "NativeDevLifecycleCliTest: $*" >&2; exit 1; }
assert_process_stopped() {
  local pid="$1" label="$2"
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    fail "$label left application process $pid running"
  fi
}
wait_for_text() {
  local file="$1" text="$2" label="$3"
  for ((i = 0; i < 160; ++i)); do
    if grep -Fq -- "$text" "$file" 2>/dev/null; then return 0; fi
    if [[ -n "$DEV_PID" ]] && ! kill -0 "$DEV_PID" 2>/dev/null; then
      cat "$file" >&2 || true
      fail "$label: vix dev exited early"
    fi
    sleep 0.1
  done
  cat "$file" >&2 || true
  fail "$label: timed out waiting for '$text'"
}
wait_for_occurrences() {
  local file="$1" text="$2" required="$3" label="$4"
  for ((i = 0; i < 160; ++i)); do
    local count
    count="$(grep -Fc -- "$text" "$file" 2>/dev/null || true)"
    if [[ "${count:-0}" -ge "$required" ]]; then return 0; fi
    if [[ -n "$DEV_PID" ]] && ! kill -0 "$DEV_PID" 2>/dev/null; then
      cat "$file" >&2 || true
      fail "$label: vix dev exited early"
    fi
    sleep 0.1
  done
  cat "$file" >&2 || true
  fail "$label: expected $required occurrences of '$text'"
}

cat >"$PROJECT/vix.toml" <<'TOML'
format = 1
[project]
name = "native-dev-lifecycle"

[[targets]]
name = "app"
kind = "executable"
sources = ["main.cpp"]
TOML
cat >"$PROJECT/value.hpp" <<'HPP'
#pragma once
inline int value() { return 1; }
HPP
cat >"$PROJECT/main.cpp" <<'CPP'
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include "value.hpp"
int main(int argc, char** argv) {
  std::ofstream("app.pid") << static_cast<long long>(::getpid());
  std::cout << "VIX_DEV_VALUE=" << value() << " ARG=" << (argc > 1 ? argv[1] : "missing") << '\n' << std::flush;
  std::cerr << "VIX_DEV_STDERR_MARKER\n" << std::flush;
  for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
CPP

(cd "$PROJECT" && "$VIX_BIN" dev app --cwd runtime -- spawn) >"$ROOT/dev.out" 2>"$ROOT/dev.err" &
DEV_PID=$!
wait_for_text "$ROOT/dev.out" "VIX_DEV_VALUE=1 ARG=spawn" "initial build/run and argument forwarding"
wait_for_text "$ROOT/dev.out" "VIX_DEV_STDERR_MARKER" "application stderr visibility"
APP_PID_1="$(cat "$PROJECT/runtime/app.pid")"

cat >"$PROJECT/value.hpp" <<'HPP'
#pragma once
inline int value() { return 2; }
HPP
wait_for_text "$ROOT/dev.out" "VIX_DEV_VALUE=2 ARG=spawn" "header edit restart"
assert_process_stopped "$APP_PID_1" "header restart"
APP_PID_2="$(cat "$PROJECT/runtime/app.pid")"
cp "$ROOT/dev.out" "$ROOT/success.out"
cp "$ROOT/dev.err" "$ROOT/success.err"
if grep -Eiq 'ninja:|no work to do|Building CXX object|Linking CXX|Configuring done|Generating done' "$ROOT/success.out" "$ROOT/success.err"; then
  cat "$ROOT/success.out" "$ROOT/success.err" >&2
  fail "successful rebuild exposed backend output"
fi

cat >"$PROJECT/main.cpp" <<'CPP'
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
#include "value.hpp"
int main(int argc, char** argv) {
  std::ofstream("app.pid") << static_cast<long long>(::getpid());
  std::cout << "VIX_DEV_VALUE=" << value() + 10 << " ARG=" << (argc > 1 ? argv[1] : "missing") << '\n' << std::flush;
  for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
CPP
wait_for_text "$ROOT/dev.out" "VIX_DEV_VALUE=12 ARG=spawn" "source edit restart"
assert_process_stopped "$APP_PID_2" "source restart"
APP_PID_3="$(cat "$PROJECT/runtime/app.pid")"

cat >"$PROJECT/vix.toml" <<'TOML'
format = 1
[project]
name = "native-dev-lifecycle-config-change"

[[targets]]
name = "app"
kind = "executable"
sources = ["main.cpp"]
TOML
wait_for_occurrences "$ROOT/dev.out" "VIX_DEV_VALUE=12 ARG=spawn" 2 "configuration edit restart"
assert_process_stopped "$APP_PID_3" "configuration restart"
APP_PID_3="$(cat "$PROJECT/runtime/app.pid")"

cat >"$PROJECT/main.cpp" <<'CPP'
#include "this-header-does-not-exist.hpp"
int main() { return 0; }
CPP
wait_for_text "$ROOT/dev.err" "error:" "compile failure diagnostics"
assert_process_stopped "$APP_PID_3" "compile failure"

cat >"$PROJECT/main.cpp" <<'CPP'
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
#include "value.hpp"
int main(int argc, char** argv) {
#ifndef _WIN32
  std::ofstream("app.pid") << static_cast<long long>(::getpid());
#endif
  std::cout << "VIX_DEV_VALUE=" << value() + 1 << " ARG=" << (argc > 1 ? argv[1] : "missing") << '\n' << std::flush;
  for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
CPP
wait_for_text "$ROOT/dev.out" "VIX_DEV_VALUE=3 ARG=spawn" "recovery after compile failure"
if grep -Eiq 'ninja: no work to do|Building CXX object|Linking CXX|Configuring done|Generating done' "$ROOT/dev.out"; then
  cat "$ROOT/dev.out" "$ROOT/dev.err" >&2
  fail "successful implicit build chatter was visible"
fi

kill -INT "$DEV_PID" 2>/dev/null || true
set +e
wait "$DEV_PID"
DEV_STATUS=$?
set -e
DEV_PID=""
[[ "$DEV_STATUS" -eq 0 || "$DEV_STATUS" -eq 130 ]] || fail "dev shutdown returned $DEV_STATUS"
APP_PID="$(cat "$PROJECT/runtime/app.pid" 2>/dev/null || true)"
assert_process_stopped "$APP_PID" "Ctrl+C shutdown"

cat >"$PROJECT/main.cpp" <<'CPP'
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "fault") {
    int* value = new int(42);
    delete value;
    return *value;
  }
  std::cout << "VIX_DEV_HEALTHY\n" << std::flush;
  for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
CPP
(cd "$PROJECT" && "$VIX_BIN" dev app -- fault) >"$ROOT/diag.out" 2>"$ROOT/diag.err" &
DEV_PID=$!
for ((i = 0; i < 160; ++i)); do
  if grep -Eiq 'use-after-free|heap-use-after-free' "$ROOT/diag.out" "$ROOT/diag.err" 2>/dev/null; then break; fi
  if ! kill -0 "$DEV_PID" 2>/dev/null; then cat "$ROOT/diag.out" "$ROOT/diag.err" >&2; fail "diagnostic dev exited early"; fi
  sleep 0.1
done
grep -Eiq 'use-after-free|heap-use-after-free' "$ROOT/diag.out" "$ROOT/diag.err" || {
  cat "$ROOT/diag.out" "$ROOT/diag.err" >&2
  fail "automatic sanitizer diagnostic was not preserved"
}
kill -INT "$DEV_PID" 2>/dev/null || true
set +e
wait "$DEV_PID"
set -e
DEV_PID=""
