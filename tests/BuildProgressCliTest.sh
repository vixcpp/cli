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
project(progress_app LANGUAGES CXX)
add_executable(progress_app src/main.cpp src/slow.cpp)
CMAKE

cat >"$PROJECT/src/main.cpp" <<'CPP'
int slow();
int main() { return slow(); }
CPP

cat >"$PROJECT/src/slow.cpp" <<'CPP'
int slow() { return 0; }
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
reject_output "^Building progress-app [0-9]" "$OUT"

if command -v script >/dev/null 2>&1; then
  rm -rf "$PROJECT/build-ninja"
  mkdir -p "$ROOT/bin"

  REAL_CXX="$(command -v c++)"
  cat >"$ROOT/bin/c++" <<SH
#!/usr/bin/env bash
sleep 1.0
exec "$REAL_CXX" "\$@"
SH
  chmod +x "$ROOT/bin/c++"

  TTY_OUT="$ROOT/tty-build.out"
  script -q -f "$TTY_OUT" -c "env HOME='$HOME_DIR' CCACHE_DISABLE=1 PATH='$ROOT/bin:$PATH' '$VIX_BIN' build --verbose --launcher none --linker default --dir '$PROJECT'" >/dev/null 2>&1 &
  WATCH_PID=$!

  wait_for_output "Compiling.*progress-app" "$TTY_OUT"
  wait_for_output "jobs:[[:space:]]*[1-9][0-9]*" "$TTY_OUT"

  if ! kill -0 "$WATCH_PID" 2>/dev/null; then
    cat "$TTY_OUT" >&2
    echo "build finished before early header assertions observed" >&2
    exit 1
  fi

  wait "$WATCH_PID"
  WATCH_PID=""

  require_output "build .*\\[" "$TTY_OUT"
  require_output "Finished\\|Done" "$TTY_OUT"
  reject_output "^Building progress-app [0-9]" "$TTY_OUT"

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
