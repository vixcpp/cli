#!/usr/bin/env bash
# Authoritative current-CLI contract suite. Registered script contracts are
# explicitly called here; platform-specific native dev coverage follows CTest.
set -euo pipefail

VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

failures=0
failed_scripts=()

run() {
  local script="$1"
  printf '\n==> %s\n' "$script"
  if bash "$ROOT/tests/contracts/$script" "$VIX_BIN"; then
    printf 'PASS: %s\n' "$script"
  else
    local status=$?
    printf 'FAIL: %s (exit %s)\n' "$script" "$status" >&2
    failures=$((failures + 1))
    failed_scripts+=("$script")
  fi
}

run_test() {
  local script="$1"
  printf '\n==> %s\n' "$script"
  if bash "$ROOT/tests/$script" "$VIX_BIN"; then
    printf 'PASS: %s\n' "$script"
  else
    local status=$?
    printf 'FAIL: %s (exit %s)\n' "$script" "$status" >&2
    failures=$((failures + 1))
    failed_scripts+=("$script")
  fi
}

check_command() {
  local label="$1"
  shift
  if "$@" >/dev/null; then
    printf 'PASS: %s\n' "$label"
  else
    local status=$?
    printf 'FAIL: %s (exit %s)\n' "$label" "$status" >&2
    failures=$((failures + 1))
    failed_scripts+=("$label")
  fi
}

run GlobalCliContractTest.sh
run PublicOptionCoverageTest.sh
run CapabilityCoverageGateTest.sh
run CommandCoverageGateTest.sh
run GlobalOptionCoverageGateTest.sh
run run/RunCoreContractTest.sh
run run/RunCacheContractTest.sh
run run/RunVixAppContractTest.sh
run run/RunExecutionPathsContractTest.sh
run run/RunCompiledDependencyContractTest.sh
run ../RunScriptDependencyPathTest.sh
run ../RunSingleCppCacheCliTest.sh
run ../RunSanitizedOutputCliTest.sh
run ../GitDependenciesTest.sh
run ../MutationCommandContractsTest.sh
run ../GlobalInstallOviTest.sh
run build/BuildCoreContractTest.sh
run build/BuildPassthroughContractTest.sh
run build/BuildExecutionPathsContractTest.sh
run build/BuildNinjaIncrementalityContractTest.sh
run ../BuildToolCliCompatTest.sh
run ../BuildPlanningCompatTest.sh
run ../BuildWatchCliTest.sh
run ../BuildProgressCliTest.sh
run ../SdkProfileCompositionTest.sh
run ../BuildNativeCMakeDiscoveryTest.sh
run ../CMakeVixDependencyManifestTest.sh
run ../NativeBuildStateCleanCliTest.sh
run ../NativeRunFastPathCliTest.sh
run ../ExternalCMakeBuildStateCliTest.sh
run install/InstallReconciliationContractTest.sh
run run/RunAdvancedOptionsContractTest.sh
run_test TestsCommandContractCliTest.sh
check_command 'top-level help' "$VIX_BIN" --help
for command in new init build run dev test repl add remove update install publish upgrade; do
  check_command "help $command" "$VIX_BIN" help "$command"
done
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) echo 'Native watched dev integration is POSIX-only and is registered conditionally in CTest.' ;;
  *) run_test NativeDevLifecycleCliTest.sh ;;
esac

if (( failures > 0 )); then
  printf '\nAllCliContractsTest: %d independent check(s) failed:\n' "$failures" >&2
  printf '  %s\n' "${failed_scripts[@]}" >&2
  exit 1
fi

echo "AllCliContractsTest passed"
