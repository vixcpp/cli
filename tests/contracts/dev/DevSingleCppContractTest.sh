#!/usr/bin/env bash
# Real direct-compile watch contract; no Vix runtime or project fixture.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
export CCACHE_DIR="${CCACHE_DIR:-/tmp/vix/ccache}"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/vix-dev-single.XXXXXX")"
LOG="$ROOT/dev.log"
PID=""
fail() { echo "DevSingleCppContractTest: $*" >&2; sed -n '1,260p' "$LOG" >&2 || true; exit 1; }
cleanup() { [[ -n "$PID" ]] && kill -TERM "$PID" 2>/dev/null || true; wait "${PID:-}" 2>/dev/null || true; rm -rf "$ROOT"; }
trap cleanup EXIT

wait_for() {
  local text=$1
  for _ in $(seq 1 300); do grep -q "$text" "$LOG" && return; sleep 0.1; done
  sed -n '1,240p' "$LOG" >&2; fail "missing output: $text"
}
rebuild_count() { grep -Ec '^Rebuilt main\.cpp in ' "$LOG" || true; }
start() {
  HOME="$ROOT/home" "$VIX_BIN" dev "$ROOT/main.cpp" --quiet >"$LOG" 2>&1 & PID=$!
}
stop() { kill -INT "$PID"; wait "$PID" || true; PID=""; }

mkdir -p "$ROOT/home"
printf '%s\n' '#pragma once' '#include "b.hpp"' '#define VALUE B_VALUE' > "$ROOT/a.hpp"
printf '%s\n' '#pragma once' '#define B_VALUE 1' > "$ROOT/b.hpp"
printf '%s\n' '#include "a.hpp"' '#include <chrono>' '#include <iostream>' '#include <thread>' 'int main(){ std::cout << "READY value=" << VALUE << std::endl; for (;;) std::this_thread::sleep_for(std::chrono::seconds(1)); }' > "$ROOT/main.cpp"
start
wait_for 'READY value=1'

# A transitive header is a real compiler input, not a directory heuristic.
printf '%s\n' '#pragma once' '#define B_VALUE 2' > "$ROOT/b.hpp"
wait_for 'READY value=2'
sleep 1.5
[[ $(grep -c '^READY value=2$' "$LOG") -eq 1 ]] || fail 'one b.hpp write produced multiple READY value=2 children'
[[ $(rebuild_count) -eq 1 ]] || fail 'one b.hpp write was accepted more than once'

# No input change remains idle.  A direct header is a dependency too.
sleep 1
[[ $(rebuild_count) -eq 1 ]] || fail 'idle watcher rebuilt unexpectedly'
# Direct-script cache fingerprints include mtime; a touch is consequently a
# single coherent watch event, never an idle-loop trigger.
touch_before=$(rebuild_count)
touch "$ROOT/b.hpp"
for _ in $(seq 1 100); do [[ $(rebuild_count) -eq $((touch_before + 1)) ]] && break; sleep .1; done
[[ $(rebuild_count) -eq $((touch_before + 1)) ]] || fail 'touch-only dependency change was not observed'
sleep 1
[[ $(rebuild_count) -eq $((touch_before + 1)) ]] || fail 'touch-only dependency change looped'
printf '%s\n' '#pragma once' '#include "b.hpp"' '#define VALUE (B_VALUE + 10)' > "$ROOT/a.hpp"
wait_for 'READY value=12'
sleep 1
[[ $(grep -c '^READY value=12$' "$LOG") -eq 1 ]] || fail 'direct header write produced multiple children'

# Change the include graph; a successful rebuild must replace the watched set.
printf '%s\n' '#pragma once' '#define VALUE 3' > "$ROOT/c.hpp"
printf '%s\n' '#include "c.hpp"' '#include <chrono>' '#include <iostream>' '#include <thread>' 'int main(){ std::cout << "READY value=" << VALUE << std::endl; for (;;) std::this_thread::sleep_for(std::chrono::seconds(1)); }' > "$ROOT/main.cpp"
wait_for 'READY value=3'
sleep 1
before_stale=$(rebuild_count)
printf '%s\n' '#pragma once' '#include "b.hpp"' '#define VALUE (B_VALUE + 11)' > "$ROOT/a.hpp"
sleep 1.5
[[ $(rebuild_count) -eq "$before_stale" ]] || fail 'stale a.hpp remained watched after include graph change'
printf '%s\n' '#pragma once' '#define VALUE 4' > "$ROOT/c.hpp"
wait_for 'READY value=4'
sleep 1
[[ $(grep -c '^READY value=4$' "$LOG") -eq 1 ]] || fail 'new dependency write produced multiple children'

# A failed rebuild stays alive; its last successful dependency set observes a
# deleted/recreated generated header and recovers without another main change.
rm "$ROOT/c.hpp"
sleep 1
kill -0 "$PID" || fail 'watch session exited after failed rebuild'
printf '%s\n' '#pragma once' '#define VALUE 5' > "$ROOT/c.hpp"
wait_for 'READY value=5'
stop
echo "DevSingleCppContractTest passed"
