#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"
WATCH_PID=""
trap 'if [[ -n "$WATCH_PID" ]]; then kill -INT "$WATCH_PID" 2>/dev/null || true; wait "$WATCH_PID" 2>/dev/null || true; fi; rm -rf "$ROOT"' EXIT

PROJECT="$ROOT/progress-app"
HOME_DIR="$ROOT/home"
mkdir -p "$PROJECT/src" "$HOME_DIR"

cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(progress_app LANGUAGES C CXX)
add_library(core STATIC src/foo.cpp)
add_library(progress_module MODULE src/module.cpp)
add_subdirectory(ui)
add_subdirectory(nested/module)
add_executable(progress_app src/main.cpp src/slow.cpp src/slow.c)
target_link_libraries(progress_app PRIVATE core ui x)
add_custom_command(OUTPUT generated.txt COMMAND ${CMAKE_COMMAND} -E touch generated.txt)
add_custom_target(generate ALL DEPENDS generated.txt)
CMAKE

mkdir -p "$PROJECT/ui/src" "$PROJECT/nested/module/lib"
cat >"$PROJECT/ui/CMakeLists.txt" <<'CMAKE'
add_library(ui STATIC src/View.cpp)
CMAKE

cat >"$PROJECT/nested/module/CMakeLists.txt" <<'CMAKE'
add_library(x STATIC lib/foo.cc)
CMAKE

cat >"$PROJECT/src/main.cpp" <<'CPP'
int slow();
int main() { return slow(); }
CPP

cat >"$PROJECT/src/slow.cpp" <<'CPP'
int slow() { return 0; }
CPP

cat >"$PROJECT/src/slow.c" <<'C'
int slow_c(void) { return 0; }
C

cat >"$PROJECT/src/foo.cpp" <<'CPP'
int foo() { return 0; }
CPP

cat >"$PROJECT/src/module.cpp" <<'CPP'
int module() { return 0; }
CPP

cat >"$PROJECT/ui/src/View.cpp" <<'CPP'
int view() { return 0; }
CPP

cat >"$PROJECT/nested/module/lib/foo.cc" <<'CPP'
int nested_foo() { return 0; }
CPP

require_output() {
  local needle="$1"
  local file="$2"
  if ! grep -q -- "$needle" "$file"; then
    cat "$file" >&2
    echo "expected output: $needle" >&2
    exit 1
  fi
}

reject_output() {
  local needle="$1"
  local file="$2"
  if [[ ! -e "$file" ]]; then
    return 0
  fi
  if grep -q -- "$needle" "$file"; then
    cat "$file" >&2
    echo "unexpected output: $needle" >&2
    exit 1
  fi
}

wait_for_output() {
  local needle="$1"
  local file="$2"
  local deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    if grep -q -- "$needle" "$file"; then
      return 0
    fi
    sleep 0.05
  done
  cat "$file" >&2
  echo "timed out waiting for '$needle'" >&2
  exit 1
}

OUT="$ROOT/redirected.out"
HOME="$HOME_DIR" CCACHE_DISABLE=1 "$VIX_BIN" build --launcher none --linker default --dir "$PROJECT" >"$OUT" 2>&1
if grep -q $'\r' "$OUT"; then
  cat "$OUT" >&2
  echo "redirected build output contains carriage returns" >&2
  exit 1
fi
require_output "Compiling.*progress-app" "$OUT"
require_output "Finished.*dev \[unoptimized + debuginfo\].* in " "$OUT"
reject_output "Configuring.*progress-app\|Configured in" "$OUT"
reject_output "Project ready\|Compilation finished\|Linked\|Build completed" "$OUT"
reject_output "^Building progress-app [0-9]" "$OUT"

if command -v script >/dev/null 2>&1; then
  rm -rf "$PROJECT/build-ninja"
  mkdir -p "$ROOT/bin"

  REAL_CXX="$(command -v c++)"
  REAL_CC="$(command -v cc)"
  cat >"$ROOT/bin/c++" <<SH
#!/usr/bin/env bash
sleep 1.0
exec "$REAL_CXX" "\$@"
SH
  cat >"$ROOT/bin/cc" <<SH
#!/usr/bin/env bash
sleep 1.0
exec "$REAL_CC" "\$@"
SH
  chmod +x "$ROOT/bin/c++" "$ROOT/bin/cc"

  NORMAL_TTY_OUT="$ROOT/tty-normal-build.out"
  script -q -f "$NORMAL_TTY_OUT" -c "env HOME='$HOME_DIR' CCACHE_DISABLE=1 PATH='$ROOT/bin:$PATH' '$VIX_BIN' build --jobs 1 --launcher none --linker default --dir '$PROJECT'" >/dev/null 2>&1 &
  WATCH_PID=$!

  wait_for_output "Compiling.*progress-app" "$NORMAL_TTY_OUT"
  wait "$WATCH_PID"
  WATCH_PID=""

  require_output "build .*\\[============================\\].*done" "$NORMAL_TTY_OUT"
  require_output "build .*1/" "$NORMAL_TTY_OUT"
  require_output "› src/slow.cpp" "$NORMAL_TTY_OUT"
  require_output "› src/slow.c" "$NORMAL_TTY_OUT"
  require_output "› src/foo.cpp" "$NORMAL_TTY_OUT"
  require_output "› src/View.cpp" "$NORMAL_TTY_OUT"
  require_output "› lib/foo.cc" "$NORMAL_TTY_OUT"
  require_output "› progress_app" "$NORMAL_TTY_OUT"
  require_output "› libprogress_module" "$NORMAL_TTY_OUT"
  require_output "› Generating generated.txt" "$NORMAL_TTY_OUT"
  require_output "Finished.*dev \[unoptimized + debuginfo\].* in " "$NORMAL_TTY_OUT"
  reject_output "launcher:\|linker:\|jobs:" "$NORMAL_TTY_OUT"
  reject_output "› Building CXX object\|› Building C object\|› .*CMakeFiles/" "$NORMAL_TTY_OUT"
  reject_output "Project ready\|Compilation finished\|Linked\|Build completed" "$NORMAL_TTY_OUT"

  rm -rf "$PROJECT/build-ninja"

  TTY_OUT="$ROOT/tty-build.out"
  script -q -f "$TTY_OUT" -c "env HOME='$HOME_DIR' CCACHE_DISABLE=1 PATH='$ROOT/bin:$PATH' '$VIX_BIN' build --jobs 1 --verbose --dir '$PROJECT'" >/dev/null 2>&1 &
  WATCH_PID=$!

  wait_for_output "Compiling.*progress-app" "$TTY_OUT"
  wait_for_output "launcher:" "$TTY_OUT"
  wait_for_output "linker:" "$TTY_OUT"
  wait_for_output "jobs:" "$TTY_OUT"
  wait_for_output "\\* configured in" "$TTY_OUT"

  if ! kill -0 "$WATCH_PID" 2>/dev/null; then
    cat "$TTY_OUT" >&2
    echo "build finished before early header assertions observed" >&2
    exit 1
  fi

  wait "$WATCH_PID"
  WATCH_PID=""

  require_output "build .*\\[" "$TTY_OUT"
  require_output "build .*1/" "$TTY_OUT"
  require_output "› src/slow.cpp" "$TTY_OUT"
  require_output "› src/slow.c" "$TTY_OUT"
  require_output "› src/foo.cpp" "$TTY_OUT"
  require_output "› src/View.cpp" "$TTY_OUT"
  require_output "› lib/foo.cc" "$TTY_OUT"
  require_output "› progress_app" "$TTY_OUT"
  require_output "› libprogress_module" "$TTY_OUT"
  require_output "› Generating generated.txt" "$TTY_OUT"
  require_output "build .*\[============================\].*done" "$TTY_OUT"
  require_output "Finished.*dev \[unoptimized + debuginfo\].* in " "$TTY_OUT"
  reject_output "Configuring.*progress-app\|Configured in" "$TTY_OUT"
  reject_output "› Building CXX object\|› Building C object\|› .*CMakeFiles/" "$TTY_OUT"
  reject_output "Project ready\|Compilation finished\|Linked\|Build completed" "$TTY_OUT"
  reject_output "^Building progress-app [0-9]" "$TTY_OUT"

  rm -rf "$PROJECT/build-ninja"

  CMAKE_VERBOSE_OUT="$ROOT/cmake-verbose.out"
  HOME="$HOME_DIR" CCACHE_DISABLE=1 PATH="$ROOT/bin:$PATH" \
    "$VIX_BIN" build --cmake-verbose --launcher none --linker default --dir "$PROJECT" \
    >"$CMAKE_VERBOSE_OUT" 2>&1

  require_output "Building CXX object\|CMakeFiles/" "$CMAKE_VERBOSE_OUT"
  reject_output "build .*\[============================\].*done" "$CMAKE_VERBOSE_OUT"

  WATCH_OUT="$ROOT/tty-watch.out"
  script -q -f "$WATCH_OUT" -c "env HOME='$HOME_DIR' CCACHE_DISABLE=1 PATH='$ROOT/bin:$PATH' '$VIX_BIN' build --watch --build-target progress_app --verbose --launcher none --linker default --dir '$PROJECT'" >/dev/null 2>&1 &
  WATCH_PID=$!
  wait_for_output "Compiling.*progress_app" "$WATCH_OUT"
  wait_for_output "Waiting.*for changes" "$WATCH_OUT"
  reject_output "^Building progress-app [0-9]" "$WATCH_OUT"

  kill -INT "$WATCH_PID" 2>/dev/null || true
  wait "$WATCH_PID" || true
  WATCH_PID=""
  reject_output "Stopped build watcher" "$WATCH_OUT"
fi

echo "BuildProgressCliTest passed"
