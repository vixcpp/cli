#!/usr/bin/env bash
# One SIGINT always terminates a single-file dev watch, whatever its phase.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
export CCACHE_DIR="${CCACHE_DIR:-/tmp/vix/ccache}"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/vix-dev-signal.XXXXXX")"
PID="" CHILD=""
fail() { echo "DevSingleCppSignalContractTest: $*" >&2; exit 1; }
cleanup() { [[ -n "$PID" ]] && kill -TERM "$PID" 2>/dev/null || true; wait "${PID:-}" 2>/dev/null || true; rm -rf "$ROOT"; }
trap cleanup EXIT

write_running_source() {
  printf '%s\n' '#include <iostream>' '#include <unistd.h>' 'int main(){ std::cout << "READY pid=" << getpid() << std::endl; for (;;) pause(); }' > "$ROOT/main.cpp"
}
start() { HOME="$ROOT/home" "$VIX_BIN" dev "$ROOT/main.cpp" --quiet > "$ROOT/dev.log" 2>&1 & PID=$!; }
wait_for() { local text=$1; for _ in $(seq 1 300); do grep -q "$text" "$ROOT/dev.log" && return; sleep .1; done; cat "$ROOT/dev.log" >&2; fail "missing $text"; }
assert_stopped() { for _ in $(seq 1 100); do kill -0 "$PID" 2>/dev/null || break; sleep .1; done; ! kill -0 "$PID" 2>/dev/null || fail 'dev did not stop after one SIGINT'; [[ -z "$CHILD" ]] || ! kill -0 "$CHILD" 2>/dev/null || fail 'runtime child remained after dev exit'; PID=""; CHILD=""; }
interrupt() { kill -INT "$PID"; assert_stopped; ! grep -q 'Fix the errors, save the file' "$ROOT/dev.log" || fail 'SIGINT was reported as a build failure'; }

mkdir -p "$ROOT/home"
# Initial build: interrupt before it can start a child.
write_running_source
start
sleep .15
interrupt

# Running child: the watcher must reap the child it interrupts.
: > "$ROOT/dev.log"
write_running_source
start
wait_for READY
CHILD=$(sed -n 's/.*READY pid=\([0-9][0-9]*\).*/\1/p' "$ROOT/dev.log" | tail -1)
interrupt

# Failure wait: a single signal leaves the watch loop, rather than requiring a
# second Ctrl+C after the compiler error.
: > "$ROOT/dev.log"
printf '%s\n' 'int main( { return 0; }' > "$ROOT/main.cpp"
start
wait_for 'Fix the errors, save the file'
interrupt

# Rebuild: start a fresh session, change a header, then interrupt the build.
: > "$ROOT/dev.log"
printf '%s\n' '#pragma once' '#define VALUE 1' > "$ROOT/value.hpp"
printf '%s\n' '#include "value.hpp"' '#include <iostream>' '#include <unistd.h>' 'int main(){ std::cout << "READY " << VALUE << std::endl; for (;;) pause(); }' > "$ROOT/main.cpp"
start
wait_for 'READY 1'
printf '%s\n' '#pragma once' '#define VALUE 2' > "$ROOT/value.hpp"
sleep .15
interrupt

echo "DevSingleCppSignalContractTest passed"
