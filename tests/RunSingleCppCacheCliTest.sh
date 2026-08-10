#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"

cleanup() {
  rm -rf "$ROOT"
}
trap cleanup EXIT

HOME_DIR="$ROOT/home"
PROJECT="$ROOT/project"
FAKE_BIN="$ROOT/bin"
CALLS_FILE="$ROOT/compiler-calls"

mkdir -p "$HOME_DIR/.vix" "$PROJECT" "$FAKE_BIN"
printf '0\n' >"$CALLS_FILE"

cat >"$PROJECT/local.hpp" <<'HPP'
#pragma once
HPP

cat >"$FAKE_BIN/c++" <<'SH'
#!/usr/bin/env bash
set -euo pipefail

calls_file="${VIX_FAKE_CXX_CALLS:?missing VIX_FAKE_CXX_CALLS}"

out=""
src=""
dep_query=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -MM)
      dep_query=1
      ;;
    -o)
      shift
      out="${1:-}"
      ;;
    *.cpp|*.cc|*.cxx)
      src="$1"
      ;;
  esac
  shift || true
done

if [[ "$dep_query" = "1" ]]; then
  printf 'main.o: %s\n' "$src"
  exit 0
fi

count="$(cat "$calls_file")"
printf '%s\n' "$((count + 1))" >"$calls_file"

if [[ -n "$src" ]] && grep -q 'BROKEN_FOR_CACHE_TEST' "$src"; then
  echo "fake compiler: requested failure" >&2
  exit 44
fi

if [[ -z "$out" ]]; then
  echo "fake compiler: missing -o" >&2
  exit 45
fi

mkdir -p "$(dirname "$out")"
cat >"$out" <<'BIN'
#!/usr/bin/env bash
exit 0
BIN
chmod +x "$out"
SH
chmod +x "$FAKE_BIN/c++"

cat >"$FAKE_BIN/cmake" <<'SH'
#!/usr/bin/env bash
echo "cmake fallback must not be used by this test" >&2
exit 87
SH
chmod +x "$FAKE_BIN/cmake"

cat >"$PROJECT/main.cpp" <<'CPP'
#include "local.hpp"
int main() { return 0; }
CPP

compiler_calls() {
  cat "$CALLS_FILE"
}

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
  if grep -q -- "$needle" "$file"; then
    cat "$file" >&2
    echo "unexpected output: $needle" >&2
    exit 1
  fi
}

cache_key_from() {
  awk '/^cache key:/{print $3; exit}' "$1"
}

cache_dir_from() {
  awk '/^cache dir:/{print $3; exit}' "$1"
}

run_vix() {
  local out="$1"
  shift
  (
    cd "$PROJECT"
    env \
      HOME="$HOME_DIR" \
      PATH="$FAKE_BIN:$PATH" \
      CXX="$FAKE_BIN/c++" \
      VIX_FAKE_CXX_CALLS="$CALLS_FILE" \
      "$VIX_BIN" "$1" $([[ "$1" == "run" ]] && printf '%s' '--trace-cache') "${@:2}"
  ) >"$out" 2>&1
}

run_vix_fail() {
  local out="$1"
  shift
  set +e
  (
    cd "$PROJECT"
    env \
      HOME="$HOME_DIR" \
      PATH="$FAKE_BIN:$PATH" \
      CXX="$FAKE_BIN/c++" \
      VIX_FAKE_CXX_CALLS="$CALLS_FILE" \
      "$VIX_BIN" "$@" --trace-cache
  ) >"$out" 2>&1
  local status=$?
  set -e
  return "$status"
}

OUT1="$ROOT/run-1.out"
run_vix "$OUT1" run main.cpp --no-san
require_output "script strategy: direct" "$OUT1"
reject_output "script strategy: cmake fallback" "$OUT1"
test "$(compiler_calls)" = "1"
KEY1="$(cache_key_from "$OUT1")"
test -n "$KEY1"

OUT2="$ROOT/run-2.out"
run_vix "$OUT2" run main.cpp --no-san
require_output "script strategy: direct" "$OUT2"
require_output "rebuild reason: cache hit" "$OUT2"
test "$(compiler_calls)" = "1"
test "$(cache_key_from "$OUT2")" = "$KEY1"

sleep 1
touch "$PROJECT/main.cpp"
OUT3="$ROOT/run-touch.out"
run_vix "$OUT3" run main.cpp --no-san
require_output "script strategy: direct" "$OUT3"
require_output "source content hash match: yes" "$OUT3"
require_output "fingerprint match: yes" "$OUT3"
require_output "rebuild reason: cache hit" "$OUT3"
test "$(compiler_calls)" = "1"
test "$(cache_key_from "$OUT3")" = "$KEY1"

printf '\n// changed content\n' >>"$PROJECT/main.cpp"
OUT4="$ROOT/run-changed.out"
run_vix "$OUT4" run main.cpp --no-san
require_output "script strategy: direct" "$OUT4"
test "$(compiler_calls)" = "2"
KEY2="$(cache_key_from "$OUT4")"
test -n "$KEY2"
test "$KEY2" != "$KEY1"

OUT5="$ROOT/build-shared.out"
run_vix "$OUT5" build main.cpp
test "$(compiler_calls)" = "2"

OUT6="$ROOT/run-option.out"
run_vix "$OUT6" run main.cpp --no-san -- -DALT_CACHE_OPTION=1
require_output "script strategy: direct" "$OUT6"
test "$(compiler_calls)" = "3"
KEY3="$(cache_key_from "$OUT6")"
test -n "$KEY3"
test "$KEY3" != "$KEY2"

cat >"$PROJECT/broken.cpp" <<'CPP'
#include "local.hpp"
BROKEN_FOR_CACHE_TEST
int main() { return 0; }
CPP

OUT7="$ROOT/run-broken.out"
if run_vix_fail "$OUT7" run broken.cpp --no-san; then
  cat "$OUT7" >&2
  echo "broken source unexpectedly compiled" >&2
  exit 1
fi
test "$(compiler_calls)" = "4"
BROKEN_CACHE_DIR="$(cache_dir_from "$OUT7")"
test -n "$BROKEN_CACHE_DIR"
if [[ -e "$BROKEN_CACHE_DIR/meta.txt" ]]; then
  cat "$OUT7" >&2
  echo "failed compilation wrote positive direct cache metadata" >&2
  exit 1
fi

OUT8="$ROOT/run-broken-again.out"
if run_vix_fail "$OUT8" run broken.cpp --no-san; then
  cat "$OUT8" >&2
  echo "broken source unexpectedly compiled on second run" >&2
  exit 1
fi
test "$(compiler_calls)" = "5"

echo "RunSingleCppCacheCliTest passed"
