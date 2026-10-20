#!/usr/bin/env bash
# Command-registration validation. Coverage status records registration, not
# test outcome; every command also receives an executable help-route check.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DISPATCH="$ROOT/src/commands/Dispatch.cpp"
MANIFEST="$ROOT/tests/contracts/CommandCoverage.md"
CLI="${1:-}"

mapfile -t commands < <(sed -n 's/^[[:space:]]*add({"\([^"]*\)".*/\1/p' "$DISPATCH" | sort -u)
expected_commands=(add build dev init install new publish remove run test update upgrade)
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
  status="$(awk -F'|' '{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $7); print $7}' <<<"$row")"
  contract="$(awk -F'|' '{gsub(/^[[:space:]]+|[[:space:]]+$/, "", $8); print $8}' <<<"$row")"
  case "$status" in
    REGISTERED)
      [[ "$contract" != "—" && -n "$contract" ]] || {
        echo "REGISTERED command has no contract: $command" >&2; exit 1; }
      grep -Fq -- "$contract" "$ROOT/tests/CMakeLists.txt" || {
        echo "REGISTERED command references unregistered contract: $command" >&2; exit 1; }
      ;;
    HELP_ONLY|GAP|BLOCKED|UNAVAILABLE) ;;
    *) echo "Invalid command status for $command: $status" >&2; exit 1 ;;
  esac
done

for command in new init build run test dev add remove update install publish upgrade; do
  grep -Fq "add({\"$command\"" "$DISPATCH" || {
    echo "Required canonical command is not registered: $command" >&2
    exit 1
  }
done

for completion in bash/vix zsh/_vix fish/vix.fish; do
  file="$ROOT/share/completions/$completion"
  [[ -f "$file" ]] || { echo "Missing shell completion asset: $completion" >&2; exit 1; }
  for command in new init build run test dev add remove update install publish upgrade; do
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

for command in check fmt clean reset login logout registry search list outdated store unpublish pack verify cache doctor info completion uninstall make task target targets module modules tests up i deps repl; do
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
    topic_help="$("$CLI" help "$command")" || {
      echo "Per-command help route failed for canonical command: $command" >&2; exit 1; }
    grep -Eq '^Usage:|^usage:' <<<"$topic_help" || {
      echo "Per-command help has no usage heading: $command" >&2; exit 1; }
  done
  for command in check fmt clean reset login logout registry search list outdated store unpublish pack verify cache doctor info completion uninstall make task target targets module modules tests up i deps; do
    if grep -Eq "^[[:space:]]+$command([[:space:]]|$)" <<<"$help_output"; then
      echo "Removed command remains in top-level help: $command" >&2; exit 1
    fi
  done
  if output="$("$CLI" help repl 2>&1)"; then
    echo 'Removed REPL help topic was accepted' >&2; exit 1
  fi
  grep -Fq 'unknown help topic' <<<"$output" || {
    echo 'Removed REPL help topic did not fail as unknown' >&2; exit 1; }
  grep -Fq 'https://vixcpp.com/docs' <<<"$help_output" || {
    echo "Canonical documentation URL is missing from CLI help" >&2; exit 1; }

  build_help="$($CLI build --help)"
  run_help="$($CLI run --help)"
  dev_help="$($CLI dev --help)"
  test_help="$($CLI test --help)"
  new_help="$($CLI new --help)"
  grep -Fq 'vix new <path>' <<<"$new_help" || { echo 'New help omits the canonical path syntax' >&2; exit 1; }
  if grep -Eq -- '--force|--dir|template|application|CMakeLists' <<<"$new_help"; then
    echo 'New help exposes retired project-creation concepts' >&2; exit 1
  fi
  for option in '--dir' '--jobs' '--clean' '--platform' '--out' '--release' '--verbose' '--quiet' '--san' '--asan' '--ubsan' '--tsan' '--cmake' '--compiler-arg' '--linker-arg'; do
    grep -Fq -- "$option" <<<"$build_help" || { echo "Build help omits $option" >&2; exit 1; }
  done
  for option in '--dir' '--release' '--cwd' '--env' '--verbose' '--quiet' '--' '--san' '--asan' '--ubsan' '--tsan' '--diagnostics=off' '--cmake' '--compiler-arg' '--linker-arg'; do
    grep -Fq -- "$option" <<<"$run_help" || { echo "Run help omits $option" >&2; exit 1; }
  done
  for option in '--dir' '--release' '--cwd' '--env' '--verbose' '--quiet' '--' '--cmake' '--san' '--asan' '--ubsan' '--tsan' '--diagnostics=off'; do
    grep -Fq -- "$option" <<<"$dev_help" || { echo "Dev help omits $option" >&2; exit 1; }
  done
  for option in '--dir' '--list' '--watch' '--fail-fast' '--verbose' '--quiet' '--san' '--asan' '--ubsan' '--tsan' '--ctest'; do
    grep -Fq -- "$option" <<<"$test_help" || { echo "Test help omits $option" >&2; exit 1; }
  done
  grep -Fq -- '--diagnostics=off' <<<"$test_help" || { echo 'Test help omits the automatic-diagnostics opt-out' >&2; exit 1; }
  for term in sqlite mysql cloud heartbeat graph-executor managed-sdk cmake-verbose; do
    if grep -Eiq -- "$term" <<<"$build_help"; then echo "Build help exposes internal term: $term" >&2; exit 1; fi
  done
  for option in --launcher --linker --sysroot --targets --warning-check; do
    if grep -Eiq -- "(^|[[:space:]])${option}([[:space:]]|$|=)" <<<"$build_help"; then
      echo "Build help exposes retired option: $option" >&2; exit 1
    fi
  done
  for term in docker container ssh:// http:// openapi force-server force-script compiler-fingerprint with-sqlite with-mysql; do
    if grep -Eiq -- "$term" <<<"$run_help"; then echo "Run help exposes internal term: $term" >&2; exit 1; fi
  done
  for option in --exact-test --test -R --raw --preset --build-target; do
    if grep -Eiq -- "(^|[[:space:]])${option}([[:space:]]|$|=)" <<<"$test_help"; then
      echo "Test help exposes retired option: $option" >&2; exit 1
    fi
  done

  if output="$("$CLI" build --graph-executor off 2>&1)"; then echo 'Removed build option was accepted' >&2; exit 1; fi
  grep -Fq 'Unknown build option: --graph-executor' <<<"$output" || { echo 'Build did not reject the removed option at the public parser' >&2; exit 1; }
  if output="$("$CLI" run --watch 2>&1)"; then echo 'run --watch was accepted' >&2; exit 1; fi
  grep -Fq 'Unknown run option: --watch' <<<"$output" || { echo 'Run did not reject --watch at the public parser' >&2; exit 1; }
  if output="$("$CLI" test --preset release 2>&1)"; then echo 'Removed test option was accepted' >&2; exit 1; fi
  grep -Fq 'Unknown test option: --preset' <<<"$output" || { echo 'Test did not reject the removed option at the public parser' >&2; exit 1; }
  for args in \
    'build --build-target app' \
    'build --preset release' \
    'build --with-sqlite' \
    'build --with-mysql' \
    'build --report' \
    'build --heartbeat' \
    'build --graph-executor off' \
    'build --target aarch64-linux-gnu' \
    'build --sanitize' \
    'build --sanitize=address' \
    'build --no-san' \
    'build --san --asan' \
    'run --sanitize' \
    'run --sanitize=address' \
    'run --no-san' \
    'run --asan --tsan' \
    'run --diagnostics' \
    'run --diagnostics=on' \
    'run --diagnostics=san' \
    'test --sanitize' \
    'test --sanitize=address' \
    'test --no-san' \
    'test --san --ubsan' \
    'test --diagnostics=asan' \
    'build -- -DFEATURE=ON' \
    'run docker://example/image' \
    'run ssh://host/app' \
    'run http://example/app' \
    'run --force-server' \
    'run --docs' \
    'run --with-sqlite' \
    'run --platform aarch64-linux-gnu' \
    'run --env INVALID' \
    'test --exact-test unit' \
    'test -R unit' \
    'test --test unit' \
    'test --raw' \
    'test --build-target unit' \
    'test filter --ctest -L integration' \
    'test --list --ctest -L integration' \
    'test --watch --ctest -L integration' \
    'test --fail-fast --ctest -L integration' \
    'test -- filter'; do
    if output="$($CLI $args 2>&1)"; then echo "Retired syntax was accepted: $args" >&2; exit 1; fi
  done

  for args in \
    'run --san --san' \
    'run --san --diagnostics=off' \
    'run --diagnostics=off --asan' \
    'run --diagnostics=off --diagnostics=off' \
    'dev --san --diagnostics=off' \
    'test --diagnostics=off --ubsan'; do
    if output="$("$CLI" $args 2>&1)"; then
      echo "Conflicting diagnostic request was accepted: $args" >&2
      exit 1
    else
      status=$?
      [[ "$status" -eq 2 ]] || { echo "Conflict did not return parser status 2: $args ($status)" >&2; exit 1; }
    fi
  done

  diagnostic_fixture="$(mktemp -d)"
  trap 'rm -rf "$diagnostic_fixture"' EXIT
  printf 'cmake_minimum_required(VERSION 3.20)\nproject(diagnostic_gate LANGUAGES CXX)\n' >"$diagnostic_fixture/CMakeLists.txt"
  for command in build run dev test; do
    for mode in --san --asan --ubsan --tsan; do
      if output="$("$CLI" "$command" --dir "$diagnostic_fixture" "$mode" 2>&1)"; then
        echo "Vix diagnostic mode unexpectedly succeeded for external CMake $command" >&2
        exit 1
      fi
      grep -Fq 'Vix diagnostic modes are not available for arbitrary external CMake projects.' <<<"$output" || {
        echo "External CMake $command did not fail closed for $mode" >&2
        exit 1
      }
    done
  done
  for command in run test; do
    output="$($CLI "$command" --dir "$diagnostic_fixture" 2>&1 || true)"
    if grep -Fq 'Vix diagnostic modes are not available for arbitrary external CMake projects.' <<<"$output"; then
      echo "Automatic Vix diagnostics incorrectly rejected external CMake $command" >&2
      exit 1
    fi
  done

  if output="$("$CLI" dev example.cpp 2>&1)"; then echo 'Single-source dev was accepted' >&2; exit 1; fi
  grep -Fq 'not a single source file' <<<"$output" || { echo 'Dev did not explain its project-target-only input' >&2; exit 1; }
  if output="$("$CLI" repl 2>&1)"; then echo 'Removed REPL command was accepted' >&2; exit 1; fi
  grep -Eiq 'unknown|unrecognized' <<<"$output" || {
    echo 'Removed REPL command did not fail as an unknown command' >&2; exit 1; }
  for args in 'install package-name' 'publish --dry-run' 'upgrade --check' 'update acme/json'; do
    if output="$($CLI $args 2>&1)"; then echo "Unsupported command option/argument was accepted: $args" >&2; exit 1; fi
  done
fi

DEV_SOURCE="$ROOT/src/commands/DevCommand.cpp"
grep -Fq 'RunCommand::run_watched' "$DEV_SOURCE" || {
  echo "vix dev does not delegate to the generic run command" >&2
  exit 1
}
if grep -Eiq 'ProjectDiscovery|NativeDevelopmentRuntime|DevSession|vix\.app|app\.vix|Vue|frontend|server application|web application|--watch' "$DEV_SOURCE"; then
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

# Guard the explicit command form independently from comprehensive run
# contracts: top-level source input must explain the supported alternatives.
grep -Fq 'a source file must be used with an explicit command' "$ROOT/src/CLI.cpp" || {
  echo "Top-level .cpp rejection guidance is missing" >&2; exit 1; }
if grep -Fq 'cmd = "run"' "$ROOT/src/CLI.cpp"; then
  echo "Top-level .cpp shortcut still dispatches to run" >&2; exit 1
fi

echo "CommandCoverageGateTest passed"
