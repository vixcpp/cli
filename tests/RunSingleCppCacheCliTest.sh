#!/usr/bin/env bash
# Standalone run/build reuses managed outputs without stale-result reuse.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project"
PROJECT="$ROOT/project"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
#ifndef VALUE
#define VALUE 1
#endif
int main() { std::cout << VALUE << '\n'; }
CPP
run() { "$VIX_BIN" run "$PROJECT/main.cpp" --diagnostics=off "$@"; }
out="$(run)"; out="$(normalize_pty_output "$out")"; grep -Fxq '1' <<<"$out"
out="$(run)"; out="$(normalize_pty_output "$out")"; grep -Fxq '1' <<<"$out"
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
#ifndef VALUE
#define VALUE 2
#endif
int main() { std::cout << VALUE << '\n'; }
CPP
out="$(run)"; out="$(normalize_pty_output "$out")"; grep -Fxq '2' <<<"$out" || { echo 'source change reused stale output' >&2; exit 1; }
out="$(run --compiler-arg=-DVALUE=3)"
out="$(normalize_pty_output "$out")"
grep -Fxq '3' <<<"$out" || { echo 'compiler argument was absent from standalone cache identity' >&2; exit 1; }
"$VIX_BIN" build "$PROJECT/main.cpp" --compiler-arg=-DVALUE=4 >/dev/null
test ! -e "$PROJECT/main" || { echo 'managed standalone build exported into source directory' >&2; exit 1; }
cat >"$PROJECT/broken.cpp" <<'CPP'
int main() { BROKEN_FOR_CACHE_TEST }
CPP
for _ in 1 2; do
  if "$VIX_BIN" run "$PROJECT/broken.cpp" --diagnostics=off >"$ROOT/failure.out" 2>&1; then
    echo 'broken source unexpectedly succeeded' >&2; exit 1
  fi
  grep -Fq 'BROKEN_FOR_CACHE_TEST' "$ROOT/failure.out"
done
echo "RunSingleCppCacheCliTest passed"
