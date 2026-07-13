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

make_registry_tree() {
  local dir="$1"
  local auth_repo="$2"
  local auth_commit="$3"
  local rix_repo="$4"
  local rix_commit="$5"
  mkdir -p "$dir/index"
  cat > "$dir/index/rix.auth.json" <<JSON
{
  "id": "rix/auth",
  "repo": { "url": "$auth_repo" },
  "versions": {
    "1.0.0": { "tag": "v1.0.0", "commit": "$auth_commit" }
  }
}
JSON
  cat > "$dir/index/rix.rix.json" <<JSON
{
  "id": "rix/rix",
  "repo": { "url": "$rix_repo" },
  "versions": {
    "1.0.0": { "tag": "v1.0.0", "commit": "$rix_commit" }
  }
}
JSON
}

make_registry() {
  local home="$1"
  local auth_repo="$2"
  local auth_commit="$3"
  local rix_repo="$4"
  local rix_commit="$5"
  make_registry_tree "$home/.vix/registry/index" "$auth_repo" "$auth_commit" "$rix_repo" "$rix_commit"
}

make_registry_repo() {
  local repo="$1"
  local auth_repo="$2"
  local auth_commit="$3"
  local rix_repo="$4"
  local rix_commit="$5"
  make_registry_tree "$repo" "$auth_repo" "$auth_commit" "$rix_repo" "$rix_commit"
  git -C "$repo" init -q -b main
  git -C "$repo" config user.email test@example.invalid
  git -C "$repo" config user.name "Vix Test"
  git -C "$repo" add .
  git -C "$repo" commit -q -m registry
}

configure_registry_rewrite() {
  local home="$1"
  local registry_repo="$2"
  mkdir -p "$home"
  HOME="$home" git config --global url."$registry_repo".insteadOf https://github.com/vixcpp/registry.git
}

make_project() {
  local project="$1"
  mkdir -p "$project"
  cat > "$project/vix.json" <<'JSON'
{
  "deps": [
    { "id": "rix/rix", "version": "1.0.0" }
  ]
}
JSON
}

json_lock_edit() {
  python3 - "$@"
}

make_auth_lock_obsolete() {
  local lock="$1"
  local hash="$2"
  json_lock_edit "$lock" "$hash" <<'PY'
import json, sys
path, value = sys.argv[1], sys.argv[2]
with open(path, 'r', encoding='utf-8') as f:
    data = json.load(f)
for dep in data['dependencies']:
    if dep['id'] == 'rix/auth':
        dep['hash'] = value
        dep.pop('hash_algorithm', None)
        dep.pop('hash_version', None)
with open(path, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)
    f.write('\n')
PY
}

make_auth_lock_bad_v2() {
  local lock="$1"
  json_lock_edit "$lock" <<'PY'
import json, sys
path = sys.argv[1]
with open(path, 'r', encoding='utf-8') as f:
    data = json.load(f)
for dep in data['dependencies']:
    if dep['id'] == 'rix/auth':
        dep['hash'] = '0' * 64
        dep['hash_algorithm'] = 'vix-package-sha256'
        dep['hash_version'] = 2
with open(path, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)
    f.write('\n')
PY
}

old_hash_including_git() {
  local checkout="$1"
  python3 - "$checkout" <<'PY'
import hashlib, os, sys
root = sys.argv[1]
files = []
for dirpath, _, filenames in os.walk(root):
    for name in filenames:
        path = os.path.join(dirpath, name)
        if os.path.isfile(path):
            files.append(path)
files.sort()
combined = b''
for path in files:
    rel = os.path.relpath(path, root).replace(os.sep, '/')
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    combined += rel.encode() + b':' + h.hexdigest().encode() + b'\n'
print(hashlib.sha256(combined).hexdigest())
PY
}

lock_dep_field() {
  local lock="$1"
  local dep_id="$2"
  local field="$3"
  python3 - "$lock" "$dep_id" "$field" <<'PY'
import json, sys
path, dep_id, field = sys.argv[1:4]
with open(path, 'r', encoding='utf-8') as f:
    data = json.load(f)
for dep in data['dependencies']:
    if dep['id'] == dep_id:
        print(dep.get(field, ''))
        break
PY
}

assert_modern_auth_lock() {
  local lock="$1"
  [[ "$(lock_dep_field "$lock" rix/auth hash_algorithm)" == "vix-package-sha256" ]]
  [[ "$(lock_dep_field "$lock" rix/auth hash_version)" == "2" ]]
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

REGISTRY_REPO="$ROOT/registry-repo"
make_registry_repo "$REGISTRY_REPO" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"

HOME_A="$ROOT/home-a"
PROJECT_A="$ROOT/project-a"
mkdir -p "$HOME_A"
make_registry "$HOME_A" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
make_project "$PROJECT_A"
(
  cd "$PROJECT_A"
  HOME="$HOME_A" "$VIX_BIN" add rix/rix@1.0.0 >/dev/null
  grep -q '"id": "rix/auth"' vix.lock
  grep -q '"hash_algorithm": "vix-package-sha256"' vix.lock
  HOME="$HOME_A" "$VIX_BIN" install >/dev/null
)
LOCK_BEFORE="$(cat "$PROJECT_A/vix.lock")"

HOME_OLD="$ROOT/home-old"
PROJECT_OLD="$ROOT/project-old"
mkdir -p "$HOME_OLD"
cp -a "$PROJECT_A" "$PROJECT_OLD"
rm -rf "$PROJECT_OLD/.vix" "$PROJECT_OLD/build"
make_registry "$HOME_OLD" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
make_auth_lock_obsolete "$PROJECT_OLD/vix.lock" "$(printf '0%.0s' {1..64})"
(
  cd "$PROJECT_OLD"
  HOME="$HOME_OLD" "$VIX_BIN" install >"$ROOT/old-migrate.out" 2>&1
)
grep -q 'refreshing obsolete integrity metadata' "$ROOT/old-migrate.out"
grep -q 'rix/auth.*metadata updated' "$ROOT/old-migrate.out"
grep -q 'rix/auth.*installed' "$ROOT/old-migrate.out"
! grep -q 'Use: vix update' "$ROOT/old-migrate.out"
! grep -q 'Run: vix registry sync' "$ROOT/old-migrate.out"
test -f "$PROJECT_OLD/.vix/vix_deps.cmake"
assert_modern_auth_lock "$PROJECT_OLD/vix.lock"
test ! -e "$PROJECT_OLD/vix.lock.tmp"
python3 -m json.tool "$PROJECT_OLD/vix.lock" >/dev/null

HOME_GIT_HASH="$ROOT/home-git-hash"
PROJECT_GIT_HASH="$ROOT/project-git-hash"
mkdir -p "$HOME_GIT_HASH"
cp -a "$PROJECT_A" "$PROJECT_GIT_HASH"
rm -rf "$PROJECT_GIT_HASH/.vix" "$PROJECT_GIT_HASH/build"
make_registry "$HOME_GIT_HASH" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
OLD_GIT_HASH="$(old_hash_including_git "$HOME_A/.vix/store/git/rix.auth/$AUTH_COMMIT")"
make_auth_lock_obsolete "$PROJECT_GIT_HASH/vix.lock" "$OLD_GIT_HASH"
(
  cd "$PROJECT_GIT_HASH"
  HOME="$HOME_GIT_HASH" "$VIX_BIN" install >"$ROOT/git-hash-migrate.out" 2>&1
)
assert_modern_auth_lock "$PROJECT_GIT_HASH/vix.lock"

git -C "$AUTH_REPO" commit --allow-empty -q -m other
DIFFERENT_AUTH_COMMIT="$(git -C "$AUTH_REPO" rev-parse HEAD)"
HOME_REGISTRY_MISMATCH="$ROOT/home-registry-mismatch"
PROJECT_REGISTRY_MISMATCH="$ROOT/project-registry-mismatch"
mkdir -p "$HOME_REGISTRY_MISMATCH"
cp -a "$PROJECT_A" "$PROJECT_REGISTRY_MISMATCH"
rm -rf "$PROJECT_REGISTRY_MISMATCH/.vix" "$PROJECT_REGISTRY_MISMATCH/build"
make_registry "$HOME_REGISTRY_MISMATCH" "$AUTH_REPO" "$DIFFERENT_AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
make_auth_lock_obsolete "$PROJECT_REGISTRY_MISMATCH/vix.lock" "$(printf '1%.0s' {1..64})"
if (cd "$PROJECT_REGISTRY_MISMATCH" && HOME="$HOME_REGISTRY_MISMATCH" "$VIX_BIN" install >"$ROOT/registry-mismatch.out" 2>&1); then
  echo "install migrated lock despite registry commit mismatch" >&2
  exit 1
fi
grep -q 'registry metadata commit changed for rix/auth@1.0.0' "$ROOT/registry-mismatch.out"
! grep -q 'metadata updated' "$ROOT/registry-mismatch.out"
! grep -q 'Use: vix update' "$ROOT/registry-mismatch.out"

HOME_SYNC="$ROOT/home-sync"
PROJECT_SYNC="$ROOT/project-sync"
mkdir -p "$HOME_SYNC"
cp -a "$PROJECT_A" "$PROJECT_SYNC"
rm -rf "$PROJECT_SYNC/.vix" "$PROJECT_SYNC/build"
make_auth_lock_obsolete "$PROJECT_SYNC/vix.lock" "$(printf '2%.0s' {1..64})"
configure_registry_rewrite "$HOME_SYNC" "$REGISTRY_REPO"
test ! -e "$HOME_SYNC/.vix/registry/index"
(
  cd "$PROJECT_SYNC"
  HOME="$HOME_SYNC" "$VIX_BIN" install >"$ROOT/auto-sync.out" 2>&1
)
test -d "$HOME_SYNC/.vix/registry/index/.git"
test -f "$HOME_SYNC/.vix/registry/index/index/rix.auth.json"
test -f "$PROJECT_SYNC/.vix/vix_deps.cmake"
assert_modern_auth_lock "$PROJECT_SYNC/vix.lock"
! grep -q 'Run: vix registry sync' "$ROOT/auto-sync.out"
! grep -q 'Use: vix update' "$ROOT/auto-sync.out"
SYNC_LOCK_BEFORE="$(cat "$PROJECT_SYNC/vix.lock")"
(
  cd "$PROJECT_SYNC"
  HOME="$HOME_SYNC" "$VIX_BIN" install >"$ROOT/second-install.out" 2>&1
)
SYNC_LOCK_AFTER="$(cat "$PROJECT_SYNC/vix.lock")"
if [[ "$SYNC_LOCK_BEFORE" != "$SYNC_LOCK_AFTER" ]]; then
  echo "second vix install rewrote vix.lock" >&2
  exit 1
fi
! grep -q 'metadata updated' "$ROOT/second-install.out"

rm -rf "$PROJECT_SYNC/.vix"
(
  cd "$PROJECT_SYNC"
  HOME="$HOME_SYNC" "$VIX_BIN" install >"$ROOT/offline-store-registry.out" 2>&1
)
test -f "$PROJECT_SYNC/.vix/vix_deps.cmake"

HOME_BAD_V2="$ROOT/home-bad-v2"
PROJECT_BAD_V2="$ROOT/project-bad-v2"
mkdir -p "$HOME_BAD_V2"
cp -a "$PROJECT_A" "$PROJECT_BAD_V2"
rm -rf "$PROJECT_BAD_V2/.vix" "$PROJECT_BAD_V2/build"
make_registry "$HOME_BAD_V2" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
make_auth_lock_bad_v2 "$PROJECT_BAD_V2/vix.lock"
BAD_V2_LOCK_BEFORE="$(cat "$PROJECT_BAD_V2/vix.lock")"
if (cd "$PROJECT_BAD_V2" && HOME="$HOME_BAD_V2" "$VIX_BIN" install >"$ROOT/bad-v2.out" 2>&1); then
  echo "install migrated incorrect v2 hash" >&2
  exit 1
fi
grep -q 'integrity check failed: rix/auth' "$ROOT/bad-v2.out"
! grep -q 'metadata updated' "$ROOT/bad-v2.out"
BAD_V2_LOCK_AFTER="$(cat "$PROJECT_BAD_V2/vix.lock")"
if [[ "$BAD_V2_LOCK_BEFORE" != "$BAD_V2_LOCK_AFTER" ]]; then
  echo "bad v2 hash rewrote vix.lock" >&2
  exit 1
fi

HOME_DIRTY="$ROOT/home-dirty"
PROJECT_DIRTY="$ROOT/project-dirty"
mkdir -p "$HOME_DIRTY"
cp -a "$PROJECT_A" "$PROJECT_DIRTY"
rm -rf "$PROJECT_DIRTY/.vix" "$PROJECT_DIRTY/build"
make_registry "$HOME_DIRTY" "$AUTH_REPO" "$AUTH_COMMIT" "$RIX_REPO" "$RIX_COMMIT"
(
  cd "$PROJECT_DIRTY"
  HOME="$HOME_DIRTY" "$VIX_BIN" install >/dev/null
)
AUTH_CHECKOUT_DIRTY="$HOME_DIRTY/.vix/store/git/rix.auth/$AUTH_COMMIT"
printf '\n// changed by integrity test\n' >> "$AUTH_CHECKOUT_DIRTY/src/Auth.cpp"
if (cd "$PROJECT_DIRTY" && HOME="$HOME_DIRTY" "$VIX_BIN" install >"$ROOT/modified.out" 2>&1); then
  echo "install succeeded after tracked checkout modification" >&2
  exit 1
fi
grep -q 'integrity check failed: rix/auth' "$ROOT/modified.out"
grep -q 'local modifications to tracked files' "$ROOT/modified.out"
! grep -q 'vix store gc && vix install' "$ROOT/modified.out"

LOCK_AFTER="$(cat "$PROJECT_A/vix.lock")"
if [[ "$LOCK_BEFORE" != "$LOCK_AFTER" ]]; then
  echo "modern vix install rewrote vix.lock" >&2
  exit 1
fi

echo "RegistryIntegrityTest passed"
