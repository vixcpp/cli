#!/usr/bin/env bash
# Retired per-invocation dependency injection cannot alter a project graph.
set -euo pipefail
VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project"
normalize_pty_output() {
  local normalized="${1//$'\r\n'/$'\n'}"
  printf '%s' "${normalized%$'\r'}"
}
cat >"$ROOT/project/main.cpp" <<'CPP'
#include <iostream>
int main(int argc, char **argv) { std::cout << (argc > 1 ? argv[1] : "none") << '\n'; }
CPP
out="$("$VIX_BIN" run "$ROOT/project/main.cpp" -- --dep)"
out="$(normalize_pty_output "$out")"
grep -Fxq -- '--dep' <<<"$out" || { echo 'runtime argument tail was not opaque' >&2; exit 1; }
if "$VIX_BIN" run "$ROOT/project/main.cpp" --dep 'git=ignored;rev=ignored' >/dev/null 2>&1; then
  echo 'historical --dep run option was accepted' >&2; exit 1
fi
test ! -e "$ROOT/project/vix.toml"
test ! -e "$ROOT/project/vix.lock"
echo "RunScriptDependencyPathTest passed"
