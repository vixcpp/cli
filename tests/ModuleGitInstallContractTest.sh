#!/usr/bin/env bash
# Focused, network-free LOT 7 contract for module-owned Git dependencies.
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"
mkdir -p "$HOME"

fail() { echo "ModuleGitInstallContractTest: $*" >&2; exit 1; }

make_repo() {
  local repo="$1"
  mkdir -p "$repo"
  cat > "$repo/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(sample_dep LANGUAGES CXX)
add_library(sample_dep INTERFACE)
add_library(sample::dep ALIAS sample_dep)
EOF
  git -C "$repo" init -q
  git -C "$repo" config user.email test@example.invalid
  git -C "$repo" config user.name 'Vix Test'
  git -C "$repo" add .
  git -C "$repo" commit -qm A
  git -C "$repo" tag v1
  git -C "$repo" rev-parse HEAD
}

write_project() {
  local project="$1"
  mkdir -p "$project/modules/auth" "$project/modules/billing"
  cat > "$project/vix.app" <<'EOF'
name = "module_git_contract"
standard = "c++20"
[module.auth]
enabled = true
[module.billing]
enabled = true
EOF
  printf 'name = "auth"\nkind = "module"\n[deps]\nlinks = []\n' > "$project/modules/auth/vix.module"
  printf 'name = "billing"\nkind = "module"\n[deps]\nlinks = []\n' > "$project/modules/billing/vix.module"
}

REPO="$ROOT/repo"
A="$(make_repo "$REPO")"
BRANCH="$(git -C "$REPO" symbolic-ref --short HEAD)"
printf '# revision B\n' >> "$REPO/CMakeLists.txt"
git -C "$REPO" add . && git -C "$REPO" commit -qm B
B="$(git -C "$REPO" rev-parse HEAD)"
git -C "$REPO" tag v2

APP="$ROOT/app"; write_project "$APP"
(cd "$APP" && "$VIX_BIN" install "$REPO" --name sample --tag v1 --target sample::dep --module auth >install.log 2>&1) || { cat "$APP/install.log" >&2; fail 'initial module install failed'; }
grep -Fq '[dependencies.sample]' "$APP/modules/auth/vix.module" || fail 'module declaration missing'
grep -Fq "git = \"$REPO\"" "$APP/modules/auth/vix.module" || fail 'Git field missing'
grep -Fq 'tag = "v1"' "$APP/modules/auth/vix.module" || fail 'tag field missing'
grep -Fq 'target = "sample::dep"' "$APP/modules/auth/vix.module" || fail 'target field missing'
grep -Fq "$A" "$APP/vix.lock" || fail 'resolved tag pin missing from root lock'
test ! -e "$APP/modules/auth/vix.lock" || fail 'module lockfile created'
test -e "$APP/.vix/deps/sample" || fail 'project materialization missing'
test ! -d "$APP/modules/auth/.vix" || fail 'module cache created'
cp "$APP/modules/auth/vix.module" "$APP/auth.before"; cp "$APP/vix.lock" "$APP/lock.before"
(cd "$APP" && "$VIX_BIN" install "$REPO" --name sample --tag v1 --target sample::dep -m auth >reinstall.log 2>&1)
cmp "$APP/auth.before" "$APP/modules/auth/vix.module"
cmp "$APP/lock.before" "$APP/vix.lock"
grep -Fq 'already installed' "$APP/reinstall.log" || fail 'warm reinstall was not a no-op'
grep -Eq 'resolving|connecting|receiving' "$APP/reinstall.log" && fail 'warm reinstall animated progress'

# Each selector is emitted as canonical ModuleManifest syntax; changing the
# sole owner still leaves one declaration and one root lock pin.
(cd "$APP" && "$VIX_BIN" install "$REPO" --name sample --branch "$BRANCH" --target sample::dep --module auth >/dev/null)
grep -Fq "branch = \"$BRANCH\"" "$APP/modules/auth/vix.module" || fail 'branch field missing'
(cd "$APP" && "$VIX_BIN" install "$REPO" --name sample --rev "$B" --subdirectory . --target sample::dep --module auth >/dev/null)
test "$(grep -Fc '[dependencies.sample]' "$APP/modules/auth/vix.module")" -eq 1 || fail 'duplicate module declaration'
grep -Fq "rev = \"$B\"" "$APP/modules/auth/vix.module" || fail 'revision B declaration missing'
grep -Fq 'subdirectory = "."' "$APP/modules/auth/vix.module" || fail 'subdirectory field missing'
grep -Fq "$B" "$APP/vix.lock" || fail 'revision B lock pin missing'

# The prospective ownership constraints reject a competing revision before publication.
cp "$APP/modules/auth/vix.module" "$APP/auth.conflict.before"; cp "$APP/vix.lock" "$APP/lock.conflict.before"
if (cd "$APP" && "$VIX_BIN" install "$REPO" --name sample --rev "$A" --target sample::dep --module billing >/dev/null 2>&1); then fail 'module revision conflict accepted'; fi
cmp "$APP/auth.conflict.before" "$APP/modules/auth/vix.module"
cmp "$APP/lock.conflict.before" "$APP/vix.lock"

# Module diagnostics must win over any attempted remote access.
for mode in unknown disabled missing invalid; do
  CASE="$ROOT/$mode"; write_project "$CASE"
  case "$mode" in
    unknown) module=ghost ;;
    disabled) sed -i '/\[module.billing\]/{n;s/enabled = true/enabled = false/;}' "$CASE/vix.app"; module=billing ;;
    missing) rm "$CASE/modules/auth/vix.module"; module=auth ;;
    invalid) printf '[broken\n' > "$CASE/modules/auth/vix.module"; module=auth ;;
  esac
  if (cd "$CASE" && "$VIX_BIN" install 'file:///definitely/unreachable' --module "$module" >out 2>&1); then fail "$mode module accepted"; fi
  grep -qi 'module' "$CASE/out" || fail "$mode did not report module diagnostic"
  grep -qiE 'git clone|ls-remote|unreachable' "$CASE/out" && fail "$mode touched Git"
done

# Materialization and metadata publication failures preserve exact authoritative bytes.
ROLL="$ROOT/rollback"; write_project "$ROLL"
cp "$ROLL/modules/auth/vix.module" "$ROLL/auth.before"
if (cd "$ROLL" && VIX_TEST_FAIL_MODULE_MATERIALIZATION=1 "$VIX_BIN" install "$REPO" --name sample --rev "$A" --module auth >/dev/null 2>&1); then fail 'materialization seam did not fail'; fi
cmp "$ROLL/auth.before" "$ROLL/modules/auth/vix.module"; test ! -e "$ROLL/vix.lock" || fail 'materialization failure created lock'
test ! -e "$ROLL/.vix/deps/sample" || fail 'incomplete materialization retained'
if (cd "$ROLL" && VIX_TEST_FAIL_MODULE_METADATA_PUBLICATION=1 "$VIX_BIN" install "$REPO" --name sample --rev "$A" --module auth >/dev/null 2>&1); then fail 'publication seam did not fail'; fi
cmp "$ROLL/auth.before" "$ROLL/modules/auth/vix.module"; test ! -e "$ROLL/vix.lock" || fail 'publication failure created lock'
