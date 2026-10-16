#!/usr/bin/env bash
# Public build controls are generic; historical backend knobs stay rejected.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project"
PROJECT="$ROOT/project"
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(build_tool_contract LANGUAGES CXX)
add_executable(app main.cpp)
CMAKE
printf 'int main() { return 0; }\n' >"$PROJECT/main.cpp"
help="$($VIX_BIN build --help)"
for option in --dir --jobs --clean --platform --release --verbose --quiet --cmake --compiler-arg --linker-arg --san --asan --ubsan --tsan; do
  grep -Fq -- "$option" <<<"$help" || { echo "build help omits $option" >&2; exit 1; }
done
for option in --debug --debug-log --graph-executor --heartbeat --no-heartbeat --linker --launcher --targets --managed-sdk --preset --build-target --sysroot; do
  if "$VIX_BIN" build "$option" >/dev/null 2>&1; then echo "retired build option accepted: $option" >&2; exit 1; fi
done
"$VIX_BIN" build app --dir "$PROJECT" >/dev/null
backend=("$PROJECT"/.vix/build/*/backend)
test -f "${backend[0]}/CMakeCache.txt"
echo "BuildToolCliCompatTest passed"
