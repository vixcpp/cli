#!/usr/bin/env bash
# Watch/restart is a dev intention, never a build/run option.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
expect_rejected() {
  local output status
  set +e; output="$("$VIX_BIN" "$@" 2>&1)"; status=$?; set -e
  [[ $status -eq 2 ]] || { printf '%s\n' "$output" >&2; echo "expected rejection: $*" >&2; exit 1; }
}
expect_rejected build --watch
expect_rejected run --watch
dev_help="$($VIX_BIN dev --help)"
grep -Fq 'For native Vix projects, build and restart a target as project files change.' <<<"$dev_help"
grep -Fq 'External CMake projects currently run once; automatic watching is not available.' <<<"$dev_help"
if grep -Eq -- '(^|[[:space:]])--watch([[:space:]]|$)' <<<"$dev_help"; then
  echo 'dev exposes an implementation switch instead of command intent' >&2; exit 1
fi
echo "BuildWatchCliTest passed; native lifecycle is covered by vix_cli_native_dev_lifecycle"
