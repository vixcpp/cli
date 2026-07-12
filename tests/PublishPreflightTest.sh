#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
cleanup() { rm -rf "$ROOT"; }
trap cleanup EXIT

export HOME="$ROOT/home"
mkdir -p "$HOME"

git_config() {
  git -C "$1" config user.email test@example.invalid
  git -C "$1" config user.name "Vix Test"
}

make_registry() {
  local bare="$ROOT/registry-origin.git"
  local work="$ROOT/registry-work"
  git init --bare -q "$bare"
  git init -q "$work"
  git_config "$work"
  mkdir -p "$work/index"
  touch "$work/index/.keep"
  git -C "$work" add index/.keep
  git -C "$work" commit -q -m init
  git -C "$work" branch -M main
  git -C "$work" remote add origin "$bare"
  git -C "$work" push -q origin main
  git --git-dir="$bare" symbolic-ref HEAD refs/heads/main
  mkdir -p "$HOME/.vix/registry"
  git clone -q "$bare" "$HOME/.vix/registry/index"
}

make_package() {
  local repo="$1"
  local origin="$2"
  local name="$3"
  local ns="$4"
  local version="$5"
  git init --bare -q "$origin"
  git init -q "$repo"
  git_config "$repo"
  mkdir -p "$repo/include/$name"
  cat > "$repo/include/$name/$name.hpp" <<HPP
#pragma once
/// Return the fixture value.
namespace $name { inline int value() { return 42; } }
HPP
  cat > "$repo/vix.json" <<JSON
{
  "name": "$name",
  "namespace": "$ns",
  "version": "$version",
  "type": "header-only",
  "include": "include",
  "deps": [],
  "license": "MIT",
  "description": "A fixture package used to test publish preflight.",
  "keywords": ["cpp", "vix"],
  "repository": "${origin%.git}",
  "authors": [{"name": "Vix Test", "github": "vixcpp"}]
}
JSON
  git -C "$repo" add .
  git -C "$repo" commit -q -m init
  git -C "$repo" branch -M main
  git -C "$repo" remote add origin "$origin"
  git -C "$repo" tag "v$version"
  git -C "$repo" push -q origin main
  git --git-dir="$origin" symbolic-ref HEAD refs/heads/main
  git -C "$repo" push -q origin "v$version"
}

make_registry

PKG="$ROOT/ovi"
PKG_ORIGIN="$ROOT/ovi-origin.git"
make_package "$PKG" "$PKG_ORIGIN" ovi vixcpp 0.1.0

OUT="$(cd "$PKG" && "$VIX_BIN" publish --dry-run 2>&1)"
printf '%s\n' "$OUT" | grep -q 'vixcpp/ovi@0.1.0 is ready to publish'
if printf '%s\n' "$OUT" | grep -q 'index/vixcpp.ovi.json'; then
  echo "dry-run leaked registry path" >&2
  exit 1
fi

JSON_OUT="$(cd "$PKG" && "$VIX_BIN" publish --dry-run --json 2>&1)"
printf '%s\n' "$JSON_OUT" | grep -q '"status": "ready"'
printf '%s\n' "$JSON_OUT" | grep -q '"package": "vixcpp/ovi"'
printf '%s\n' "$JSON_OUT" | grep -q '"path": "ovi/ovi.hpp"'

# Version mismatch: manifest says 0.1.0, tag says 0.2.0.
git -C "$PKG" tag v0.2.0
git -C "$PKG" push -q origin v0.2.0
if (cd "$PKG" && "$VIX_BIN" publish 0.2.0 --dry-run >/tmp/vix-publish-mismatch.out 2>&1); then
  echo "version mismatch was accepted" >&2
  exit 1
fi
grep -q 'Version mismatch' /tmp/vix-publish-mismatch.out

# Existing registry says this repository belongs to vixcpp/ovi.
REG_WORK="$ROOT/registry-work"
cat > "$REG_WORK/index/vixcpp.ovi.json" <<JSON
{
  "name": "ovi",
  "namespace": "vixcpp",
  "repo": {"url": "${PKG_ORIGIN%.git}", "defaultBranch": "main"},
  "versions": {}
}
JSON
git -C "$REG_WORK" add index/vixcpp.ovi.json
git -C "$REG_WORK" commit -q -m 'add ovi'
git -C "$REG_WORK" push -q origin main
git -C "$HOME/.vix/registry/index" fetch -q origin

# Rename manifest in same source repository and publish a new tag.
cat > "$PKG/vix.json" <<JSON
{
  "name": "ovi-renamed",
  "namespace": "vixcpp",
  "version": "0.3.0",
  "type": "header-only",
  "include": "include",
  "deps": [],
  "license": "MIT",
  "description": "A fixture package used to test publish identity changes.",
  "keywords": ["cpp", "vix"],
  "repository": "${PKG_ORIGIN%.git}",
  "authors": [{"name": "Vix Test", "github": "vixcpp"}]
}
JSON
git -C "$PKG" add vix.json
git -C "$PKG" commit -q -m rename-manifest
git -C "$PKG" tag v0.3.0
git -C "$PKG" push -q origin main
git -C "$PKG" push -q origin v0.3.0
if (cd "$PKG" && "$VIX_BIN" publish 0.3.0 --dry-run >/tmp/vix-publish-identity.out 2>&1); then
  echo "identity change was accepted" >&2
  exit 1
fi
grep -q 'Package identity changed' /tmp/vix-publish-identity.out
grep -q 'Registered' /tmp/vix-publish-identity.out
grep -q 'vixcpp/ovi' /tmp/vix-publish-identity.out
