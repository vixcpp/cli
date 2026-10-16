#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?path to vix executable required}"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
PROJECT="$ROOT/project"
mkdir -p "$PROJECT" "$ROOT/home" "$ROOT/cache" "$ROOT/config" "$ROOT/data"

export HOME="$ROOT/home"
export XDG_CACHE_HOME="$ROOT/cache"
export XDG_CONFIG_HOME="$ROOT/config"
export XDG_DATA_HOME="$ROOT/data"

fail() { echo "NativeRunFastPathCliTest: $*" >&2; exit 1; }
run_vix() { (cd "$PROJECT" && "$VIX_BIN" run); }
run_vix_test() { (cd "$PROJECT" && "$VIX_BIN" test); }
assert_clean_execution_output() {
  local stdout_file="$1" stderr_file="$2" label="$3"
  grep -Fq 'PROGRAM_MARKER' "$stdout_file" "$stderr_file" || fail "$label lost program stdout"
  grep -Fq 'PROGRAM_STDERR_MARKER' "$stdout_file" "$stderr_file" || fail "$label lost program stderr"
  if grep -Eiq 'ninja:|no work to do|Building CXX object|Linking CXX|Configuring done|Generating done|cmake --build' \
      "$stdout_file" "$stderr_file"; then
    cat "$stdout_file" "$stderr_file" >&2
    fail "$label exposed successful backend output"
  fi
}

cat >"$PROJECT/vix.toml" <<'TOML'
format = 1
[project]
name = "native-run-fast-path"

[[targets]]
name = "app"
kind = "executable"
sources = ["main.cpp"]

[[tests]]
name = "smoke"
target = "app"
TOML
cat >"$PROJECT/value.hpp" <<'HPP'
#pragma once
inline int value() { return 1; }
HPP
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
#include "value.hpp"
int main() { std::cout << "PROGRAM_MARKER VALUE=" << value() << '\n'; std::cerr << "PROGRAM_STDERR_MARKER\n"; }
CPP

run_vix >"$ROOT/first.out" 2>"$ROOT/first.err" || {
  cat "$ROOT/first.out" "$ROOT/first.err" >&2
  fail "initial run failed"
}
assert_clean_execution_output "$ROOT/first.out" "$ROOT/first.err" "initial run"
grep -Fq 'VALUE=1' "$ROOT/first.out" "$ROOT/first.err" || fail "initial output value was not observed"

mapfile -t CACHES < <(find "$PROJECT/.vix/build" -mindepth 3 -maxdepth 3 -name CMakeCache.txt -print)
[[ ${#CACHES[@]} -eq 1 ]] || fail "expected one native backend context"
BACKEND="${CACHES[0]%/CMakeCache.txt}"
CONTEXT="${BACKEND%/backend}"
SIGNATURE="$CONTEXT/configured-input.sha256"
SNAPSHOT_CACHE="$CONTEXT/target-snapshot.json"
TOOLCHAIN_CACHE="$PROJECT/.vix/build/toolchain-identity.json"
test -s "$SNAPSHOT_CACHE" || fail "configured target/artifact snapshot cache was not written"
test -s "$TOOLCHAIN_CACHE" || fail "toolchain identity cache was not written"
CACHE_BEFORE="$(stat -c '%y' "${CACHES[0]}")"
TOOLCHAIN_BEFORE="$(stat -c '%y' "$TOOLCHAIN_CACHE")"

run_vix >"$ROOT/noop.out" 2>"$ROOT/noop.err" || {
  cat "$ROOT/noop.out" "$ROOT/noop.err" >&2
  fail "unchanged second run failed"
}
assert_clean_execution_output "$ROOT/noop.out" "$ROOT/noop.err" "unchanged second run"
CACHE_AFTER="$(stat -c '%y' "${CACHES[0]}")"
[[ "$CACHE_BEFORE" == "$CACHE_AFTER" ]] || fail "unchanged run reconfigured CMake"
TOOLCHAIN_AFTER="$(stat -c '%y' "$TOOLCHAIN_CACHE")"
[[ "$TOOLCHAIN_BEFORE" == "$TOOLCHAIN_AFTER" ]] || fail "unchanged run rewrote toolchain identity cache"

cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
#include "value.hpp"
int main() { std::cout << "PROGRAM_MARKER VALUE=" << value() + 1 << '\n'; std::cerr << "PROGRAM_STDERR_MARKER\n"; }
CPP
run_vix >"$ROOT/source-edit.out" 2>"$ROOT/source-edit.err" || fail "source-edit run failed"
assert_clean_execution_output "$ROOT/source-edit.out" "$ROOT/source-edit.err" "source-edit run"
grep -Fq 'VALUE=2' "$ROOT/source-edit.out" "$ROOT/source-edit.err" || fail "backend missed a source edit"

cat >"$PROJECT/value.hpp" <<'HPP'
#pragma once
inline int value() { return 3; }
HPP
cat >"$PROJECT/main.cpp" <<'CPP'
#include <iostream>
#include "value.hpp"
int main() { std::cout << "PROGRAM_MARKER VALUE=" << value() << '\n'; std::cerr << "PROGRAM_STDERR_MARKER\n"; }
CPP
run_vix >"$ROOT/header-edit.out" 2>"$ROOT/header-edit.err" || fail "header-edit run failed"
assert_clean_execution_output "$ROOT/header-edit.out" "$ROOT/header-edit.err" "header-edit run"
grep -Fq 'VALUE=3' "$ROOT/header-edit.out" "$ROOT/header-edit.err" || fail "backend missed a header dependency"

cp "$SIGNATURE" "$ROOT/signature.before"
sed 's/native-run-fast-path/native-run-fast-path-updated/' "$PROJECT/vix.toml" >"$ROOT/vix.toml.updated"
mv "$ROOT/vix.toml.updated" "$PROJECT/vix.toml"
run_vix >"$ROOT/config-edit.out" 2>"$ROOT/config-edit.err" || fail "configuration-edit run failed"
assert_clean_execution_output "$ROOT/config-edit.out" "$ROOT/config-edit.err" "configuration-edit run"
cmp -s "$ROOT/signature.before" "$SIGNATURE" && fail "manifest change did not refresh configuration signature"

ARTIFACT="$BACKEND/app"
[[ -f "$ARTIFACT" ]] || fail "expected TargetArtifact is absent"
rm "$ARTIFACT"
run_vix >"$ROOT/missing-artifact.out" 2>"$ROOT/missing-artifact.err" || fail "missing-artifact run did not rebuild"
assert_clean_execution_output "$ROOT/missing-artifact.out" "$ROOT/missing-artifact.err" "missing-artifact run"
[[ -f "$ARTIFACT" ]] || fail "backend did not restore the missing artifact"
grep -Fq 'VALUE=3' "$ROOT/missing-artifact.out" "$ROOT/missing-artifact.err" || fail "restored artifact did not execute"

if run_vix_test >"$ROOT/test.out" 2>"$ROOT/test.err"; then
  :
else
  cat "$ROOT/test.out" "$ROOT/test.err" >&2
  fail "native test command failed"
fi
if grep -Eiq 'ninja:|no work to do|Building CXX object|Linking CXX|Configuring done|Generating done|cmake --build' \
    "$ROOT/test.out" "$ROOT/test.err"; then
  cat "$ROOT/test.out" "$ROOT/test.err" >&2
  fail "native test exposed successful backend build output"
fi
grep -Eiq '100% tests passed|tests passed|Test #[0-9]+: smoke' "$ROOT/test.out" "$ROOT/test.err" ||
  fail "native test results were not visible"

cat >"$PROJECT/main.cpp" <<'CPP'
int main() { this is a compile error; }
CPP
if run_vix >"$ROOT/build-failure.out" 2>"$ROOT/build-failure.err"; then
  fail "compile failure unexpectedly succeeded"
fi
grep -Eiq 'error:|failed|not declared|expected' "$ROOT/build-failure.out" "$ROOT/build-failure.err" || {
  cat "$ROOT/build-failure.out" "$ROOT/build-failure.err" >&2
  fail "useful compiler/build failure diagnostics were hidden"
}

cat >"$PROJECT/main.cpp" <<'CPP'
int main() {
  int *p = new int(42);
  delete p;
  return *p;
}
CPP
if run_vix >"$ROOT/diagnostic.out" 2>"$ROOT/diagnostic.err"; then
  fail "automatic diagnostics did not fail on use-after-free"
fi
grep -Eiq 'use-after-free' "$ROOT/diagnostic.out" "$ROOT/diagnostic.err" || {
  cat "$ROOT/diagnostic.out" "$ROOT/diagnostic.err" >&2
  fail "automatic sanitizer evidence was not preserved"
}

echo "NativeRunFastPathCliTest passed"
