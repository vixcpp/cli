#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="${TMPDIR:-/tmp}/vix-cli-build-planning-$$"
PROJECT="$ROOT/project"

cleanup() {
  rm -rf "$ROOT"
}
trap cleanup EXIT

mkdir -p "$PROJECT" "$ROOT/home"
cat > "$PROJECT/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(vix_cli_build_planning LANGUAGES CXX)
add_executable(vix_cli_build_planning main.cpp)
CMAKE

cat > "$PROJECT/main.cpp" <<'CPP'
int main() { return 0; }
CPP

run_build() {
  HOME="$ROOT/home" CCACHE_DISABLE=1 VIX_LOG_LEVEL=debug \
    "$VIX_BIN" build --verbose --dir "$PROJECT" "$@"
}

first_output="$(run_build --launcher none --linker default)"
printf '%s\n' "$first_output" | grep -q "Configuring project (dev)"
test -d "$PROJECT/build-ninja"
test -f "$PROJECT/build-ninja/.vix-config.sig"
test -f "$PROJECT/build-ninja/CMakeCache.txt"
first_sig="$(cat "$PROJECT/build-ninja/.vix-config.sig")"
printf '%s\n' "$first_sig" | grep -q "preset=dev-ninja"
printf '%s\n' "$first_sig" | grep -q "linker=1"
if printf '%s\n' "$first_sig" | grep -Eq '^(useCache|verbose|cmakeVerbose|launcher|launcherTool)='; then
  echo "presentation or execution option leaked into configuration signature" >&2
  exit 1
fi

# These alter presentation or execution policy only. They must not alter the
# persisted CMake configuration identity.
HOME="$ROOT/home" CCACHE_DISABLE=1 "$VIX_BIN" build --dir "$PROJECT" \
  --launcher none --linker default >/dev/null
test "$(cat "$PROJECT/build-ninja/.vix-config.sig")" = "$first_sig"

HOME="$ROOT/home" CCACHE_DISABLE=1 "$VIX_BIN" build --cmake-verbose --dir "$PROJECT" \
  --launcher none --linker default >/dev/null
test "$(cat "$PROJECT/build-ninja/.vix-config.sig")" = "$first_sig"

HOME="$ROOT/home" CCACHE_DISABLE=1 "$VIX_BIN" build --quiet --dir "$PROJECT" \
  --launcher none --linker default >/dev/null
test "$(cat "$PROJECT/build-ninja/.vix-config.sig")" = "$first_sig"

second_output="$(run_build --launcher none --linker default)"
if printf '%s\n' "$second_output" | grep -q "Configuring project (dev)"; then
  echo "same signature unexpectedly configured" >&2
  exit 1
fi

nocache_output="$(run_build --no-cache --launcher none --linker default)"
printf '%s\n' "$nocache_output" | grep -q "Configuring project (dev)"
test "$(cat "$PROJECT/build-ninja/.vix-config.sig")" = "$first_sig"

clean_output="$(run_build --clean --launcher none --linker default)"
printf '%s\n' "$clean_output" | grep -q "Configuring project (dev)"

warning_output="$(run_build --warning-check --launcher none --linker default)"
printf '%s\n' "$warning_output" | grep -q "Configuring project (dev)"
warning_sig="$(cat "$PROJECT/build-ninja/.vix-config.sig")"
test "$warning_sig" != "$first_sig"
printf '%s\n' "$warning_sig" | grep -q "warningCheck=1"

release_output="$(run_build --preset release --launcher none --linker default)"
printf '%s\n' "$release_output" | grep -q "Configuring project (release)"
test -d "$PROJECT/build-release"
test -f "$PROJECT/build-release/.vix-config.sig"

rm -rf "$PROJECT/build-ninja"
mkdir -p "$PROJECT/build-ninja/.vix-config.sig.tmp"
warning_write_output="$(run_build --launcher none --linker default)"
printf '%s\n' "$warning_write_output" | grep -q "Warning: unable to write config signature file"
