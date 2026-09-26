#!/usr/bin/env bash
# --frontend is a public experimental selector.  This contract intentionally
# exercises parsing only, so it remains valid for Vix binaries built with or
# without the optional VixC frontend linked in.
set -euo pipefail

VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"

expect_frontend_error() {
  local command="$1"
  local output status

  set +e
  output="$("$VIX_BIN" "$command" --frontend unsupported 2>&1)"
  status=$?
  set -e

  test "$status" -eq 2
  printf '%s\n' "$output" | grep -Fq -- "--frontend currently supports only 'vixc'."
}

build_output="$("$VIX_BIN" build --frontend vixc --targets)"
printf '%s\n' "$build_output" | grep -Fq 'Available targets'

run_output="$("$VIX_BIN" run --frontend vixc)"
printf '%s\n' "$run_output" | grep -Fq 'No run target provided.'

expect_frontend_error build
expect_frontend_error run

set +e
equals_output="$("$VIX_BIN" build --frontend=unsupported --targets 2>&1)"
equals_status=$?
set -e
test "$equals_status" -eq 2
printf '%s\n' "$equals_output" | grep -Fq -- "--frontend currently supports only 'vixc'."

echo "FrontendOptionContractTest passed"
