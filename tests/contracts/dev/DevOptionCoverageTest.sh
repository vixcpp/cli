#!/usr/bin/env bash
# The dev help surface is public. It must have a dev-specific contract row.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
MATRIX="$ROOT/tests/contracts/OptionCoverage.md"

while IFS= read -r option; do
  if ! awk -F'|' -v option="$option" '$2 ~ /dev/ && $3 ~ "`" option "`" { found=1 } END { exit found ? 0 : 1 }' "$MATRIX"; then
    echo "Untested public option: vix dev $option" >&2
    exit 1
  fi
done < <("$VIX_BIN" dev --help | sed '/^Examples:/,$d' | grep -oE -- '--[a-z][a-z0-9-]*' | sort -u)

echo "DevOptionCoverageTest passed"
