#!/usr/bin/env bash
# Extends the existing run/build option gate to every dispatcher command.
set -euo pipefail

VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DISPATCH="$ROOT/src/commands/Dispatch.cpp"
MATRIX="$ROOT/tests/contracts/CommandOptionCoverage.md"

require_entry() {
  local command="$1" option="$2" row status contract
  row="$(awk -F'|' -v command="$command" -v option="$option" '$2 ~ "`" command "`" && $3 ~ "`" option "`" { print; exit }' "$MATRIX")"
  # The matrix is an audit record.  A missing row is explicitly uncovered,
  # rather than a broken CLI contract; only declared PASS rows must point to
  # an executable CTest contract.
  [[ -n "$row" ]] || return 0
  status="$(awk -F'|' '{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $6); print $6}' <<<"$row")"
  contract="$(awk -F'|' '{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $4); print $4}' <<<"$row")"
  if [[ "$status" == PASS ]]; then
    [[ "$contract" != "—" && -n "$contract" ]] || { echo "PASS option has no contract: vix $command $option" >&2; exit 1; }
    grep -Fq -- "$contract" "$ROOT/tests/CMakeLists.txt" || { echo "PASS option references unregistered contract: vix $command $option" >&2; exit 1; }
  fi
}

while IFS= read -r command; do
  case "$command" in run|build|i|deps|up|test) continue ;; esac
  help="$($VIX_BIN "$command" --help 2>&1)" || {
    echo "Public command help failed: vix $command --help" >&2; exit 1; }
  while IFS= read -r option; do
    [[ -z "$option" ]] || require_entry "$command" "$option"
  done < <(printf '%s\n' "$help" | sed '/^Examples:/,$d' | grep -oE -- '--[a-z][a-z0-9-]*' | sort -u || true)
done < <(sed -n 's/^[[:space:]]*add({"\([^"]*\)".*/\1/p' "$DISPATCH" | sort -u)

echo "GlobalOptionCoverageGateTest passed"
