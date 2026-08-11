#!/usr/bin/env bash
# One entry point for the CLI contract suite. It deliberately preserves failing
# coverage gates: a new public surface must never be silently skipped.
set -euo pipefail

VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

run() { bash "$ROOT/tests/contracts/$1" "$VIX_BIN"; }

run GlobalCliContractTest.sh
run PublicOptionCoverageTest.sh
run CapabilityCoverageGateTest.sh
run CommandCoverageGateTest.sh
run GlobalOptionCoverageGateTest.sh
run dev/DevOptionCoverageTest.sh
run run/RunCoreContractTest.sh
run run/RunCacheContractTest.sh
run run/RunVixAppContractTest.sh
run run/RunExecutionPathsContractTest.sh
run run/RunCompiledDependencyContractTest.sh
run dev/DevProjectContractTest.sh
run dev/DevSingleCppContractTest.sh
run dev/DevSingleCppSignalContractTest.sh
run build/BuildCoreContractTest.sh
run build/BuildPassthroughContractTest.sh
run build/BuildExecutionPathsContractTest.sh

echo "AllCliContractsTest passed"
