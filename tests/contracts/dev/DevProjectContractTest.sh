#!/usr/bin/env bash
# Uses real CMake/Ninja and a PATH wrapper that forwards to the exact CLI under test.
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
VIX_BIN="$(cd "$(dirname "$VIX_BIN")" && pwd)/$(basename "$VIX_BIN")"
fail() { echo "DevProjectContractTest: $*" >&2; exit 1; }
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/vix-dev-contract.XXXXXX")"
PROJECT="$ROOT/dev_project"
BIN="$ROOT/bin"
LOG="$ROOT/dev.log"
COUNT="$ROOT/vix-build.count"
mkdir -p "$PROJECT/runtime" "$BIN" "$ROOT/home"
cleanup() { [[ -n "${DEV_PID:-}" ]] && kill -TERM "$DEV_PID" 2>/dev/null || true; wait "${DEV_PID:-}" 2>/dev/null || true; rm -rf "$ROOT"; }
trap cleanup EXIT

printf '#!/usr/bin/env bash\necho build >> "%s"\nexec "%s" "$@"\n' "$COUNT" "$VIX_BIN" > "$BIN/vix"
chmod +x "$BIN/vix"
printf '%s\n' 'cmake_minimum_required(VERSION 3.20)' 'project(dev_project LANGUAGES CXX)' 'add_executable(dev_project main.cpp)' > "$PROJECT/CMakeLists.txt"
printf '%s\n' '#pragma once' '#define DEV_MESSAGE "one"' > "$PROJECT/message.inl"
printf '%s\n' '#include "message.inl"' '#include <cstdlib>' '#include <iostream>' '#include <unistd.h>' 'int main(int argc, char **argv) { char cwd[4096]; getcwd(cwd, sizeof cwd); std::cout << "READY " << DEV_MESSAGE << " pid=" << getpid() << " arg=" << (argc > 1 ? argv[1] : "") << " env=" << (std::getenv("DEV_ENV") ?: "") << " cwd=" << cwd << std::endl; for (;;) pause(); }' > "$PROJECT/main.cpp"

PATH="$BIN:$PATH" HOME="$ROOT/home" bash -c 'trap - INT; exec "$@"' bash "$VIX_BIN" dev "$PROJECT" --quiet --cwd "$PROJECT/runtime" --env DEV_ENV=kept --args persisted >"$LOG" 2>&1 &
DEV_PID=$!
for i in $(seq 1 80); do grep -q 'READY one pid=[0-9]* arg=persisted env=kept cwd=.*/runtime' "$LOG" && break; sleep 0.1; done
grep -q 'READY one pid=[0-9]* arg=persisted env=kept cwd=.*/runtime' "$LOG" || { sed -n '1,200p' "$LOG" >&2; fail 'initial child did not receive args/env/cwd'; }
[[ $(wc -l < "$COUNT") -eq 1 ]] || fail 'initial build did not use exactly one controlled vix subprocess'

# Keep the fixture portable to filesystems with one-second mtimes: Ninja must
# also observe the header as newer than its dependent object file.
sleep 1.1
printf '%s\n' '#pragma once' '#define DEV_MESSAGE "two"' > "$PROJECT/message.inl"
for i in $(seq 1 100); do grep -q 'READY two pid=[0-9]* arg=persisted env=kept cwd=.*/runtime' "$LOG" && break; sleep 0.1; done
grep -q 'READY two pid=[0-9]* arg=persisted env=kept cwd=.*/runtime' "$LOG" || { sed -n '1,240p' "$LOG" >&2; fail '.inl change did not rebuild and restart'; }
[[ $(wc -l < "$COUNT") -eq 2 ]] || fail '.inl change caused a non-incremental number of vix builds'

printf 'ignored\n' > "$PROJECT/README.md"
sleep 1.1
[[ $(wc -l < "$COUNT") -eq 2 ]] || fail 'ignored README change triggered a build'

kill -TERM "$DEV_PID"
wait "$DEV_PID" || true
DEV_PID=""
echo "DevProjectContractTest passed"
