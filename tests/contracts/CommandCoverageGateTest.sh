#!/usr/bin/env bash
# Command-registration validation.  The manifest is an audit, so UNCOVERED
# rows remain visible without turning the complete contract suite red; PASS
# rows must always resolve to an executable CTest contract.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DISPATCH="$ROOT/src/commands/Dispatch.cpp"
MANIFEST="$ROOT/tests/contracts/CommandCoverage.md"

mapfile -t commands < <(sed -n 's/^[[:space:]]*add({"\([^"]*\)".*/\1/p' "$DISPATCH" | sort -u)

for command in "${commands[@]}"; do
  row="$(grep -E "^\|[[:space:]]+\`$command\`[[:space:]]+\|" "$MANIFEST" || true)"
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
      grep -Fq -- "$contract" "$ROOT/tests/CMakeLists.txt" || {
        echo "PASS command references unregistered contract: $command" >&2; exit 1; }
      ;;
    UNCOVERED|FAIL|BLOCKED|UNAVAILABLE) ;;
    *) echo "Invalid command status for $command: $status" >&2; exit 1 ;;
  esac
done

for command in new init build run test dev repl add remove update install; do
  grep -Fq "add({\"$command\"" "$DISPATCH" || {
    echo "Required canonical command is not registered: $command" >&2
    exit 1
  }
done

for command in target targets module modules; do
  if grep -Fq "add({\"$command\"" "$DISPATCH"; then
    echo "Removed command remains registered: $command" >&2
    exit 1
  fi
  if grep -Fq "cmd == \"$command\"" "$ROOT/src/CLI.cpp"; then
    echo "Removed command remains in help routing: $command" >&2
    exit 1
  fi
  if grep -Fq "out << \"  $command " "$ROOT/src/CLI.cpp"; then
    echo "Removed command remains in top-level help: $command" >&2
    exit 1
  fi
done

DEV_SOURCE="$ROOT/src/commands/DevCommand.cpp"
grep -Fq 'RunCommand::run' "$DEV_SOURCE" || {
  echo "vix dev does not delegate to the generic run command" >&2
  exit 1
}
if grep -Eiq 'ProjectDiscovery|NativeDevelopmentRuntime|DevSession|vix\.app|app\.vix|Vue|frontend|server application|web application' "$DEV_SOURCE"; then
  echo "vix dev contains a separate or application-specific execution path" >&2
  exit 1
fi

# These commands are intentionally outside the current generic C++ CLI
# constitution. Keep this negative surface contract explicit so their removal
# is not mistaken for a missing coverage row.
for command in agent game cloud note desktop mobile p2p orm db service proxy health deploy logs production ws make task replay env; do
  if grep -Fq "add({\"$command\"" "$DISPATCH"; then
    echo "Non-generic command remains registered: $command" >&2
    exit 1
  fi
  if grep -Fq "out << \"  $command " "$ROOT/src/CLI.cpp"; then
    echo "Non-generic command remains in top-level help: $command" >&2
    exit 1
  fi
  if grep -Fq "cmd == \"$command\"" "$ROOT/src/CLI.cpp"; then
    echo "Non-generic command remains in help routing: $command" >&2
    exit 1
  fi
done

# Guard the implicit command form independently from the comprehensive run
# contracts: adding/removing an extension route changes public dispatch.
grep -Fq 'ext == ".vix" || ext == ".cpp"' "$ROOT/src/CLI.cpp" || {
  echo "Missing implicit .cpp/.vix dispatch contract" >&2; exit 1; }

echo "CommandCoverageGateTest passed"
