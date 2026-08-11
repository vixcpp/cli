#!/usr/bin/env bash
# Source this file from a dev contract or benchmark.  It creates forwarding
# PATH wrappers: every event is timestamped, then the real program is exec'd.
# Nothing is simulated; the child vix is always the binary under test.
set -euo pipefail

dev_harness_init() {
  local vix_bin=$1 root=$2
  export DEV_HARNESS_LOG="$root/process-events.tsv"
  export DEV_HARNESS_BIN="$root/process-bin"
  mkdir -p "$DEV_HARNESS_BIN"
  : > "$DEV_HARNESS_LOG"

  dev_harness_wrap vix "$vix_bin"
  dev_harness_wrap cmake "$(command -v cmake)"
  dev_harness_wrap ninja "$(command -v ninja)"
  for tool in c++ g++ clang++ cc ld; do
    local real
    real=$(command -v "$tool" 2>/dev/null || true)
    [[ -n "$real" ]] && dev_harness_wrap "$tool" "$real"
  done
  export PATH="$DEV_HARNESS_BIN:$PATH"
}

dev_harness_wrap() {
  local name=$1
  local real=$2
  local wrapper="$DEV_HARNESS_BIN/$name"
  printf '%s\n' '#!/usr/bin/env bash' 'set -euo pipefail' \
    'printf "%s\\t%s\\t" "$(date +%s%N)" "$(basename "$0")" >> "$DEV_HARNESS_LOG"' \
    'printf "%q " "$@" >> "$DEV_HARNESS_LOG"' \
    'printf "\\n" >> "$DEV_HARNESS_LOG"' \
    "exec $(printf '%q' "$real") \"\$@\"" > "$wrapper"
  chmod +x "$wrapper"
}

dev_harness_count() {
  local tool=$1
  awk -F '\t' -v tool="$tool" '$2 == tool { ++n } END { print n + 0 }' "$DEV_HARNESS_LOG"
}
