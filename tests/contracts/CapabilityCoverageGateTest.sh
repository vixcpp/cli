#!/usr/bin/env bash
# Current capability inventory gate; registration is not a test-pass claim.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MANIFEST="$ROOT/tests/contracts/CapabilityCoverage.md"
required=(standalone-cpp external-cmake native-project target-artifact runtime-arguments test-filter ctest-escape-hatch offline-install native-dev-lifecycle distribution-upgrade)
for id in "${required[@]}"; do
  grep -Fq -- "\`$id\`" "$MANIFEST" || { echo "Missing current capability coverage row: $id" >&2; exit 1; }
done
while IFS='|' read -r _ capability _ test status _; do
  capability="$(printf '%s' "$capability" | sed 's/`//g; s/[[:space:]]//g')"
  status="$(printf '%s' "$status" | sed 's/[[:space:]]//g')"
  [[ -n "$capability" && "$capability" != "Capability" ]] || continue
  if [[ "$status" == "REGISTERED" ]]; then
    test="$(printf '%s' "$test" | sed 's/`//g; s/[[:space:]]//g')"
    grep -Fq -- "$test" "$ROOT/tests/CMakeLists.txt" || {
      echo "Capability references an unregistered CTest: $capability -> $test" >&2; exit 1; }
  elif [[ "$status" != "HELP_ONLY" && "$status" != "GAP" ]]; then
    echo "Invalid capability coverage status: $capability -> $status" >&2; exit 1
  fi
done < <(grep '^|' "$MANIFEST" | tail -n +3)
echo "CapabilityCoverageGateTest passed"
