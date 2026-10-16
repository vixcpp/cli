#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?path to vix executable required}"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
PROJECT="$ROOT/project"
mkdir -p "$PROJECT" "$ROOT/home"
export HOME="$ROOT/home"

fail() { echo "NativeBuildStateCleanCliTest: $*" >&2; exit 1; }

cat >"$PROJECT/vix.toml" <<'TOML'
format = 1
[project]
name = "clean-state-regression"

[[targets]]
name = "app"
kind = "executable"
sources = ["main.cpp"]
TOML
cat >"$PROJECT/main.cpp" <<'CPP'
int main() { return 0; }
CPP

mkdir -p "$PROJECT/build" "$PROJECT/.vix/packages"
printf 'user-build-sentinel\n' >"$PROJECT/build/keep.txt"
printf 'package-state-sentinel\n' >"$PROJECT/.vix/packages/keep.txt"

(cd "$PROJECT" && "$VIX_BIN" build app) >"$ROOT/initial.log" 2>&1 || {
  cat "$ROOT/initial.log" >&2
  fail "initial native build failed"
}
mapfile -t CONTEXTS < <(find "$PROJECT/.vix/build" -mindepth 3 -maxdepth 3 -name CMakeCache.txt -print)
[[ ${#CONTEXTS[@]} -eq 1 ]] || fail "expected one context-local CMake cache"
CONTEXT_ROOT="${CONTEXTS[0]%/backend/CMakeCache.txt}"
test -f "$CONTEXT_ROOT/context.toml" || fail "context metadata was not written"

(cd "$PROJECT" && "$VIX_BIN" build app --clean) >"$ROOT/clean.log" 2>&1 || {
  cat "$ROOT/clean.log" >&2
  fail "native clean build failed"
}
grep -Eq 'Building CXX object|Building CXX' "$ROOT/clean.log" || {
  cat "$ROOT/clean.log" >&2
  fail "clean build did not compile sources again"
}
test -f "$PROJECT/build/keep.txt" || fail "user-owned build directory was removed"
test -f "$PROJECT/.vix/packages/keep.txt" || fail "package state was removed"
test -f "${CONTEXTS[0]}" || fail "selected context was not recreated"

echo "NativeBuildStateCleanCliTest passed"
