#!/usr/bin/env bash
# Command-registration gate. This intentionally fails on an unclassified or
# untested public dispatcher entry; UNAVAILABLE is reserved for real external
# integration and does not satisfy local coverage.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DISPATCH="$ROOT/src/commands/Dispatch.cpp"
MANIFEST="$ROOT/tests/contracts/CommandCoverage.md"

mapfile -t commands < <(sed -n 's/^[[:space:]]*add({"\([^"]*\)".*/\1/p' "$DISPATCH" | sort -u)

for command in "${commands[@]}"; do
  row="$(grep -E "^\| \`$command\` \|" "$MANIFEST" || true)"
  if [[ -z "$row" ]]; then
    echo "Untested public command: $command" >&2
    exit 1
  fi
  status="$(awk -F'|' '{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $8); print $8}' <<<"$row")"
  contract="$(awk -F'|' '{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $9); print $9}' <<<"$row")"
  case "$status" in
    PASS)
      [[ "$contract" != "—" && -n "$contract" ]] || {
        echo "PASS command has no contract: $command" >&2; exit 1; }
      rg -Fq -- "$contract" "$ROOT/tests/CMakeLists.txt" || {
        echo "PASS command references unregistered contract: $command" >&2; exit 1; }
      ;;
    UNCOVERED)
      echo "Untested public command: $command" >&2
      exit 1
      ;;
    FAIL|BLOCKED|UNAVAILABLE) ;;
    *) echo "Invalid command status for $command: $status" >&2; exit 1 ;;
  esac
done

# Guard the implicit command form independently from the comprehensive run
# contracts: adding/removing an extension route changes public dispatch.
rg -Fq 'ext == ".vix" || ext == ".cpp"' "$ROOT/src/CLI.cpp" || {
  echo "Missing implicit .cpp/.vix dispatch contract" >&2; exit 1; }

echo "CommandCoverageGateTest passed"
