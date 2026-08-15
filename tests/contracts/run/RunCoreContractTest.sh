#!/usr/bin/env bash
set -euo pipefail
VIX_BIN="${1:-/vixcpp/vix/modules/cli/build-ninja/vix}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
PROJECT="$ROOT/project with spaces"; mkdir -p "$PROJECT/include" "$PROJECT/cwd"
export HOME="$ROOT/home"
mkdir -p "$HOME"
fail() { echo "RunCoreContractTest: $*" >&2; exit 1; }
expect_failure() { local expected="$1"; shift; set +e; local out; out="$("$@" 2>&1)"; local rc=$?; set -e; [[ $rc -ne 0 ]] || fail "unexpected success: $*"; grep -Fq -- "$expected" <<<"$out" || { printf '%s\n' "$out" >&2; fail "missing: $expected"; }; }
cat >"$PROJECT/include/value.hpp" <<'CPP'
#pragma once
inline int value() { return 7; }
CPP
cat >"$PROJECT/main.cpp" <<'CPP'
#include <cstdlib>
#include <iostream>
#include "value.hpp"
int main(int argc, char **argv) {
  std::cout << "hello " << value() << " argc=" << argc << " arg=" << (argc > 1 ? argv[1] : "") << " env=" << (std::getenv("RUN_CONTRACT") ?: "") << "\n";
  return argc > 2 ? 19 : 0;
}
CPP
out="$("$VIX_BIN" run "$PROJECT/main.cpp" --no-san --run alpha -- -I"$PROJECT/include")"
grep -Fq 'hello 7 argc=2 arg=alpha env=' <<<"$out" || fail "hello/--run contract"
out="$("$VIX_BIN" run "$PROJECT/main.cpp" --no-san --cwd "$PROJECT/cwd" --env RUN_CONTRACT=yes --args beta -- -I"$PROJECT/include")"
grep -Fq 'hello 7 argc=2 arg=beta env=yes' <<<"$out" || fail "--cwd/--env/--args contract"
expect_failure 'runtime error: program reported an error' "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --run one two -- -I"$PROJECT/include"
cat >"$PROJECT/broken.cpp" <<'CPP'
int main() { does_not_compile }
CPP
expect_failure 'does_not_compile' "$VIX_BIN" run "$PROJECT/broken.cpp" --no-san
cat >"$PROJECT/link.cpp" <<'CPP'
extern int missing(); int main() { return missing(); }
CPP
expect_failure 'missing' "$VIX_BIN" run "$PROJECT/link.cpp" --no-san
trace1="$ROOT/trace1"; trace2="$ROOT/trace2"; trace3="$ROOT/trace3"
"$VIX_BIN" run "$PROJECT/main.cpp" --no-san --trace-cache -- -I"$PROJECT/include" >"$trace1" 2>&1
touch "$PROJECT/main.cpp"
"$VIX_BIN" run "$PROJECT/main.cpp" --no-san --trace-cache -- -I"$PROJECT/include" >"$trace2" 2>&1
grep -Fq 'rebuild reason: cache hit' "$trace2" || fail "warm/touch cache contract"
printf '\n// changed header\n' >>"$PROJECT/include/value.hpp"
"$VIX_BIN" run "$PROJECT/main.cpp" --no-san --trace-cache -- -I"$PROJECT/include" >"$trace3" 2>&1
for mode in fast strict; do "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --compiler-fingerprint="$mode" -- -I"$PROJECT/include" >/dev/null; done
for mode in auto always never; do "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --clear="$mode" -- -I"$PROJECT/include" >/dev/null; done
for value in 0 1 true false; do "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --docs="$value" -- -I"$PROJECT/include" >/dev/null; done
for value in kv json json-pretty; do "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --log-format="$value" -- -I"$PROJECT/include" >/dev/null; done
for value in auto always never; do "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --log-color="$value" -- -I"$PROJECT/include" >/dev/null; done
for value in trace debug info warn error critical off; do
  "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --log-level="$value" -- -I"$PROJECT/include" >/dev/null
  "$VIX_BIN" run "$PROJECT/main.cpp" --no-san --loglevel="$value" -- -I"$PROJECT/include" >/dev/null
done
expect_failure 'Invalid value for --compiler-fingerprint' "$VIX_BIN" run "$PROJECT/main.cpp" --compiler-fingerprint=bad
expect_failure 'Missing value for --cwd' "$VIX_BIN" run "$PROJECT/main.cpp" --cwd
expect_failure 'Invalid value for --auto-deps' "$VIX_BIN" run "$PROJECT/main.cpp" --auto-deps=bad
echo "RunCoreContractTest passed"
