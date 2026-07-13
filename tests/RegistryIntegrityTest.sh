#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
cleanup() { rm -rf "$ROOT"; }
trap cleanup EXIT

git_init_fixture() {
  local dir="$1"
  local tag="$2"
  git -C "$dir" init -q
  git -C "$dir" config user.email test@example.invalid
  git -C "$dir" config user.name "Vix Test"
  git -C "$dir" add .
  git -C "$dir" commit -q -m init
  git -C "$dir" tag "$tag"
  git -C "$dir" rev-parse HEAD
}

make_registry() {
  local home="$1"
  local auth_repo="$2"
  local auth_commit="$3"
  local rix_repo="$4"
  local rix_commit="$5"
  mkdir -p "$home/.vix/registry/index/index"
  cat > "$home/.vix/registry/index/index/rix.auth.json" <<JSON
{
  "id": "rix/auth",
  "repo": { "url": "$auth_repo" },
  "versions": {
    "1.0.0": { "tag": "v1.0.0", "commit": "$auth_commit" }
  }
}
JSON
  cat > "$home/.vix/registry/index/index/rix.rix.json" <<JSON
{
  "id": "rix/rix",
  "repo": { "url": "$rix_repo" },
  "versions": {
    "1.0.0": { "tag": "v1.0.0", "commit": "$rix_commit" }
  }
}
JSON
}

AUTH_REPO="$ROOT/auth"
mkdir -p "$AUTH_REPO/include/rix/auth" "$AUTH_REPO/src"
cat > "$AUTH_REPO/include/rix/auth/Auth.hpp" <<'HPP'
#pragma once
namespace rix::auth { int value(); }
HPP
cat > "$AUTH_REPO/src/Auth.cpp" <<'CPP'
#include <rix/auth/Auth.hpp>
namespace rix::auth { int value() { return 42; } }
CPP
cat > "$AUTH_REPO/vix.json" <<'JSON'
{
  "name": "auth",
  "type": "library"
}
JSON
AUTH_COMMIT="$(git_init_fixture "$AUTH_REPO" v1.0.0)"

RIX_REPO="$ROOT/rix"
mkdir -p "$RIX_REPO/include/rix" "$RIX_REPO/src"
cat > "$RIX_REPO/include/rix/Rix.hpp" <<'HPP'
#pragma once
namespace rix { int value(); }
HPP
cat > "$RIX_REPO/src/Rix.cpp" <<'CPP'
#include <rix/Rix.hpp>
namespace rix { int value() { return 7; } }
CPP
cat > "$RIX_REPO/vix.json" <<'JSON'
{
  "name": "rix",
  "type": "library",
  "deps": [
    { "id": "rix/auth", "version": "1.0.0" }
  ]
}
JSON
RIX_COMMIT="$(git_init_fixture "$RIX_REPO" v1.0.0)"

HOME_A="$ROOT/home-a"
PROJECT_A="$ROOT/project-a"
mkdir -p "$HOME_A" "$PROJECT_A"
make_registry "$HOME_A" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
cat > "$PROJECT_A/vix.json" <<'JSON'
{
  "deps": [
    { "id": "rix/rix", "version": "1.0.0" }
  ]
}
JSON

(
  cd "$PROJECT_A"
  HOME="$HOME_A" "$VIX_BIN" add rix/rix@1.0.0 >/dev/null
  grep -q '"id": "rix/auth"' vix.lock
  grep -q '"hash_algorithm": "vix-package-sha256"' vix.lock
  HOME="$HOME_A" "$VIX_BIN" install >/dev/null
)

LOCK_BEFORE="$(cat "$PROJECT_A/vix.lock")"

HOME_B="$ROOT/home-b"
PROJECT_B="$ROOT/project-b"
mkdir -p "$HOME_B"
cp -a "$PROJECT_A" "$PROJECT_B"
rm -rf "$PROJECT_B/.vix" "$PROJECT_B/build"
make_registry "$HOME_B" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
(
  cd "$PROJECT_B"
  HOME="$HOME_B" "$VIX_BIN" install >/dev/null
)

AUTH_CHECKOUT_B="$HOME_B/.vix/store/git/rix.auth/$AUTH_COMMIT"
printf '\n// changed by integrity test\n' >> "$AUTH_CHECKOUT_B/src/Auth.cpp"
if (cd "$PROJECT_B" && HOME="$HOME_B" "$VIX_BIN" install >"$ROOT/modified.out" 2>&1); then
  echo "install succeeded after tracked checkout modification" >&2
  exit 1
fi
grep -q 'integrity check failed: rix/auth' "$ROOT/modified.out"
grep -q 'local modifications to tracked files' "$ROOT/modified.out"
! grep -q 'vix store gc && vix install' "$ROOT/modified.out"

HOME_C="$ROOT/home-c"
PROJECT_C="$ROOT/project-c"
mkdir -p "$HOME_C"
cp -a "$PROJECT_A" "$PROJECT_C"
rm -rf "$PROJECT_C/.vix" "$PROJECT_C/build"
make_registry "$HOME_C" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
python3 - "$PROJECT_C/vix.lock" <<'PY'
import json, sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8') as f:
    data = json.load(f)
for dep in data['dependencies']:
    if dep['id'] == 'rix/auth':
        dep['hash'] = '0' * 64
        dep.pop('hash_algorithm', None)
        dep.pop('hash_version', None)
with open(path, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)
    f.write('\n')
PY
if (cd "$PROJECT_C" && HOME="$HOME_C" "$VIX_BIN" install >"$ROOT/oldhash.out" 2>&1); then
  echo "install succeeded with obsolete/bad lock hash" >&2
  exit 1
fi
grep -q 'locked integrity metadata does not match the content produced by the current hash algorithm' "$ROOT/oldhash.out"
grep -q 'Use: vix update' "$ROOT/oldhash.out"
! grep -q 'vix store gc && vix install' "$ROOT/oldhash.out"

LOCK_AFTER="$(cat "$PROJECT_A/vix.lock")"
if [[ "$LOCK_BEFORE" != "$LOCK_AFTER" ]]; then
  echo "vix install rewrote vix.lock" >&2
  exit 1
fi
