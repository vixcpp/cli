#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
FIXTURE_DIR="${2:?missing fixture directory}"
ROOT="$(mktemp -d)"
cleanup() {
  rm -rf "$ROOT"
}
trap cleanup EXIT

export HOME="$ROOT/home"
export VIX_GLOBAL_PREFIX="$ROOT/vix-global"
mkdir -p "$HOME/.vix/registry/index/index" "$VIX_GLOBAL_PREFIX" "$HOME/.local/bin"
export PATH="$HOME/.local/bin:$PATH"

PKG_REPO="$ROOT/ovi-repo"
cp -R "$FIXTURE_DIR" "$PKG_REPO"
git -C "$PKG_REPO" init -q
git -C "$PKG_REPO" config user.email test@example.invalid
git -C "$PKG_REPO" config user.name "Vix Test"
git -C "$PKG_REPO" add .
git -C "$PKG_REPO" commit -q -m "ovi fixture"
COMMIT="$(git -C "$PKG_REPO" rev-parse HEAD)"

cat > "$HOME/.vix/registry/index/index/vixcpp.ovi.json" <<JSON
{
  "name": "ovi",
  "namespace": "vixcpp",
  "repo": { "url": "$PKG_REPO" },
  "versions": {
    "0.1.0": {
      "tag": "v0.1.0",
      "commit": "$COMMIT"
    }
  }
}
JSON

"$VIX_BIN" install -g vixcpp/ovi

test -x "$VIX_GLOBAL_PREFIX/bin/ovi"
test -x "$VIX_GLOBAL_PREFIX/bin/ovi-doctor"
test -f "$VIX_GLOBAL_PREFIX/include/ovi/ovi.hpp"
test -f "$VIX_GLOBAL_PREFIX/lib/cmake/ovi/oviTargets.cmake"

command -v ovi | grep -q "$HOME/.local/bin/ovi"
ovi --version | grep -q "ovi 0.1.0"
ovi --help | grep -q "usage: ovi"
ovi greet Gaspard | grep -q "Hello, Gaspard!"
ovi-doctor | grep -q "ovi doctor ok"

grep -q '"executables"' "$VIX_GLOBAL_PREFIX/installed.json"
grep -q '"shims"' "$VIX_GLOBAL_PREFIX/installed.json"
grep -q '"ovi-doctor"' "$VIX_GLOBAL_PREFIX/installed.json"
grep -q "$VIX_GLOBAL_PREFIX/bin" "$HOME/.bashrc"
"$VIX_BIN" list -g --json | grep -q '"ovi"'

"$VIX_BIN" uninstall -g vixcpp/ovi
test ! -e "$VIX_GLOBAL_PREFIX/bin/ovi"
test ! -e "$VIX_GLOBAL_PREFIX/bin/ovi-doctor"
test ! -e "$HOME/.local/bin/ovi"
test ! -e "$HOME/.local/bin/ovi-doctor"
test ! -e "$VIX_GLOBAL_PREFIX/include/ovi/ovi.hpp"
