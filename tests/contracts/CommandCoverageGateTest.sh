#!/usr/bin/env bash
# Command-registration validation.  The manifest is an audit, so UNCOVERED
# rows remain visible without turning the complete contract suite red; PASS
# rows must always resolve to an executable CTest contract.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DISPATCH="$ROOT/src/commands/Dispatch.cpp"
MANIFEST="$ROOT/tests/contracts/CommandCoverage.md"
CLI="${1:-}"

mapfile -t commands < <(sed -n 's/^[[:space:]]*add({"\([^"]*\)".*/\1/p' "$DISPATCH" | sort -u)
expected_commands=(add build dev init install new publish remove repl run test update upgrade)
expected_sorted="$(printf '%s\n' "${expected_commands[@]}" | sort)"
actual_sorted="$(printf '%s\n' "${commands[@]}")"
[[ "$actual_sorted" == "$expected_sorted" ]] || {
  echo "Dispatcher command set does not exactly match the canonical release surface" >&2
  printf 'Expected:\n%s\nActual:\n%s\n' "$expected_sorted" "$actual_sorted" >&2
  exit 1
}

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

for command in new init build run test dev repl add remove update install publish upgrade; do
  grep -Fq "add({\"$command\"" "$DISPATCH" || {
    echo "Required canonical command is not registered: $command" >&2
    exit 1
  }
done

for completion in bash/vix zsh/_vix fish/vix.fish; do
  file="$ROOT/share/completions/$completion"
  [[ -f "$file" ]] || { echo "Missing shell completion asset: $completion" >&2; exit 1; }
  for command in new init build run test dev repl add remove update install publish upgrade; do
    grep -Eq "(^|[^[:alnum:]_-])$command([^[:alnum:]_-]|$)" "$file" || {
      echo "Shell completion omits canonical command $command: $completion" >&2; exit 1; }
  done
  for command in login registry search modules targets doctor uninstall; do
    if grep -Eq "(^|[^[:alnum:]_-])$command([^[:alnum:]_-]|$)" "$file"; then
      echo "Retired command appears in shell completion $completion: $command" >&2; exit 1
    fi
  done
done

grep -Fq 'UpgradeRuntime runtime' "$ROOT/src/commands/UpgradeCommand.cpp" || {
  echo "UpgradeCommand does not compose UpgradeRuntime" >&2; exit 1; }
grep -Fq 'runtime.run(' "$ROOT/src/commands/UpgradeCommand.cpp" || {
  echo "UpgradeCommand does not delegate execution to UpgradeRuntime" >&2; exit 1; }

ASSEMBLER="$ROOT/../../cmake/AssembleVixDistribution.cmake"
for input in VIX_CLI_BINARY VIX_SDK_PREFIX VIX_SDK_LIBRARIES VIX_RELEASE_TRUST VIX_DISTRIBUTION_SOURCE; do
  grep -Fq "$input" "$ASSEMBLER" || {
    echo "Distribution assembler lacks explicit input $input" >&2; exit 1; }
done
if grep -Eq 'file\(COPY .*modules/|GLOB_RECURSE' "$ASSEMBLER"; then
  echo "Distribution assembler sweeps repository modules instead of admitted inputs" >&2; exit 1
fi

for command in check fmt clean reset login logout registry search list outdated store unpublish pack verify cache doctor info completion uninstall make task target targets module modules tests up i deps; do
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

grep -Fq 'link("https://vixcpp.com/docs")' "$ROOT/src/CLI.cpp" || {
  echo "Canonical documentation URL is missing from top-level help" >&2; exit 1; }
grep -Fq 'out << "  upgrade                    Upgrade Vix' "$ROOT/src/CLI.cpp" || {
  echo "Canonical upgrade help entry is missing" >&2; exit 1; }
if grep -Fq 'docs.vixcpp.com/cli' "$ROOT/src/CLI.cpp"; then
  echo "Historical CLI documentation URL remains in top-level help" >&2
  exit 1
fi

if [[ -n "$CLI" ]]; then
  help_output="$("$CLI" --help)"
  for command in "${expected_commands[@]}"; do
    grep -Eq "^[[:space:]]+$command([[:space:]]|$)" <<<"$help_output" || {
      echo "Canonical command is missing from top-level help: $command" >&2; exit 1; }
  done
  for command in check fmt clean reset login logout registry search list outdated store unpublish pack verify cache doctor info completion uninstall make task target targets module modules tests up i deps; do
    if grep -Eq "^[[:space:]]+$command([[:space:]]|$)" <<<"$help_output"; then
      echo "Removed command remains in top-level help: $command" >&2; exit 1
    fi
  done
  grep -Fq 'https://vixcpp.com/docs' <<<"$help_output" || {
    echo "Canonical documentation URL is missing from CLI help" >&2; exit 1; }
fi

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
grep -Fq 'ext == ".cpp"' "$ROOT/src/CLI.cpp" || {
  echo "Missing implicit .cpp dispatch contract" >&2; exit 1; }

echo "CommandCoverageGateTest passed"
