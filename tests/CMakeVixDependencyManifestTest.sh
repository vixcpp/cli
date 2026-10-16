#!/usr/bin/env bash
# A CMake project uses CMake authority; legacy vix.app does not provide global install.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project"
PROJECT="$ROOT/project"
cat >"$PROJECT/vix.app" <<'APP'
name = "legacy-only"
type = "executable"
APP
cat >"$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(cmake_authority LANGUAGES CXX)
add_executable(app main.cpp)
CMAKE
printf 'int main() { return 0; }\n' >"$PROJECT/main.cpp"
if (cd "$PROJECT" && "$VIX_BIN" install) >/dev/null 2>&1; then
  echo 'legacy vix.app incorrectly established native package authority' >&2; exit 1
fi
if "$VIX_BIN" install "$PROJECT/dependency" >/dev/null 2>&1; then
  echo 'retired install package selector was accepted' >&2; exit 1
fi
"$VIX_BIN" build app --dir "$PROJECT" >/dev/null
backend=("$PROJECT"/.vix/build/*/backend)
test -f "${backend[0]}/CMakeCache.txt"
test -f "$PROJECT/vix.app"
test ! -e "$PROJECT/vix.toml"
echo "CMake Vix dependency-manifest legacy rejection passed"
