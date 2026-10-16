#!/usr/bin/env bash
# Historical managed SDK profile switches are rejected, not silently revived.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/project"
cat >"$ROOT/project/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(no_managed_sdk_cli LANGUAGES CXX)
add_executable(app main.cpp)
CMAKE
printf 'int main() { return 0; }\n' >"$ROOT/project/main.cpp"
for args in 'build --managed-sdk' 'build --preset release' 'build --managed-sdk --preset web' 'upgrade --sdk web'; do
  # Deliberate expansion to test each removed public syntax.
  # shellcheck disable=SC2086
  if "$VIX_BIN" $args --dir "$ROOT/project" >/dev/null 2>&1; then
    echo "historical SDK/profile syntax was accepted: $args" >&2; exit 1
  fi
done
"$VIX_BIN" build app --dir "$ROOT/project" >/dev/null
if find "$ROOT/project" -maxdepth 3 -name '*sdk*' -print -quit | grep -q .; then
  echo 'ordinary project build created managed SDK profile state' >&2; exit 1
fi
echo "SdkProfileCompositionTest passed (retired profile coverage)"
