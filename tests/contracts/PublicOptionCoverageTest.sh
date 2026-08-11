#!/usr/bin/env bash
# The help text is a public API. Adding a visible long option without a matrix
# entry fails CI.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MATRIX="$ROOT/tests/contracts/OptionCoverage.md"
require_matrix_entry() {
  local command="$1" option="$2"
  if ! awk -F'|' -v command="$command" -v option="$option" '$2 ~ command && $3 ~ "`" option "`" { found=1 } END { exit found ? 0 : 1 }' "$MATRIX"; then
    echo "Untested public option: $command $option" >&2
    exit 1
  fi
}
check_help() {
  local command="$1" help option
  help="$("$VIX_BIN" "$command" --help)"
  while IFS= read -r option; do
    [[ -z "$option" ]] || require_matrix_entry "$command" "$option"
  # Examples intentionally contain user/project flags; only the declared help
  # synopsis is part of this command's option surface.
  done < <(printf '%s\n' "$help" | sed '/^Examples:/,$d' | grep -oE -- '--[a-z][a-z0-9-]*' | sort -u)
}
check_help run
check_help build
check_help dev
echo "PublicOptionCoverageTest passed"
