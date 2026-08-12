#!/usr/bin/env bash
# Source-to-manifest gate. Every discovered branch/profile is classified, and a
# PASS row must name an executable contract registered in tests/CMakeLists.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MANIFEST="$ROOT/tests/contracts/CapabilityCoverage.md"

require_capability() {
  local id="$1"
  if ! grep -Fq -- "\`$id\`" "$MANIFEST"; then
    echo "Uncovered capability: $id" >&2
    exit 1
  fi
}

while IFS='|' read -r _ command capability variant test status _; do
  [[ "${status// /}" == "PASS" ]] || continue
  test="${test# }"; test="${test% }"
  test="${test%% (*}"
  [[ -n "$test" ]] || { echo "PASS capability has no contract" >&2; exit 1; }
  if ! grep -Fq -- "$test" "$ROOT/tests/CMakeLists.txt"; then
    echo "PASS capability references unregistered contract: $test" >&2
    exit 1
  fi
done < <(grep '^|' "$MANIFEST" | tail -n +3)

for path in target.binary target.script target.project target.container strategy.direct strategy.cmake-fallback manifest.vix; do
  require_capability "$path"
done

for profile in $(sed -n '400,475p' "$ROOT/src/sdk/SdkProfiles.cpp" | sed -n 's/.*profile == "\([a-z0-9-]*\)".*/\1/p'); do
  require_capability "$profile"
done

for path in cmake-ninja target-graph-executor watch-cmake watch-graph-executor log-reader; do
  require_capability "$path"
done

echo "CapabilityCoverageGateTest passed"
