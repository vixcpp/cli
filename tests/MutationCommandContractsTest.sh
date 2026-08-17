#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
cleanup() { rm -rf "$ROOT"; }
trap cleanup EXIT
export HOME="$ROOT/home"
mkdir -p "$HOME/.vix/registry/index/index"

assert_clean_mutation_state() {
  local project="$1"
  test -e "$project/.vix/locks/project-mutation.lock"
  test ! -d "$project/.vix/transactions" || test -z "$(find "$project/.vix/transactions" -mindepth 1 -maxdepth 1 -print -quit)"
  test -z "$(find "$project" -name '*.vix-txn-*' -print -quit)"
}

git_fixture() {
  local repo="$1" label="$2"
  mkdir -p "$repo"
  printf '%s\n' "$label" > "$repo/README.md"
  git -C "$repo" init -q
  git -C "$repo" config user.email test@example.invalid
  git -C "$repo" config user.name 'Vix Test'
  git -C "$repo" add .
  git -C "$repo" commit -q -m "$label"
  git -C "$repo" rev-parse HEAD
}

# Direct Git install: failure restores non-canonical bytes and an absent lock.
INSTALL_FAIL="$ROOT/install-fail"
mkdir -p "$INSTALL_FAIL"
printf '# ORIGINAL APP\n\nname = "install"\nstandard = "c++20"\n' > "$INSTALL_FAIL/vix.app"
printf '# ORIGINAL LOCK\n{ "lockVersion" : 1, "dependencies" : [] }\n' > "$INSTALL_FAIL/vix.lock"
cp "$INSTALL_FAIL/vix.app" "$INSTALL_FAIL/app.before"
cp "$INSTALL_FAIL/vix.lock" "$INSTALL_FAIL/lock.before"
if (cd "$INSTALL_FAIL" && "$VIX_BIN" install "file://$ROOT/no-such-local-repository" --name missing >/dev/null 2>&1); then exit 1; fi
cmp "$INSTALL_FAIL/app.before" "$INSTALL_FAIL/vix.app"
cmp "$INSTALL_FAIL/lock.before" "$INSTALL_FAIL/vix.lock"
assert_clean_mutation_state "$INSTALL_FAIL"

INSTALL_ABSENT="$ROOT/install-absent-lock"
mkdir -p "$INSTALL_ABSENT"
printf '# KEEP THIS COMMENT\n\nname = "absent_lock"\nstandard = "c++20"\n' > "$INSTALL_ABSENT/vix.app"
cp "$INSTALL_ABSENT/vix.app" "$INSTALL_ABSENT/app.before"
if (cd "$INSTALL_ABSENT" && "$VIX_BIN" install "file://$ROOT/no-such-local-repository" --name missing >/dev/null 2>&1); then exit 1; fi
cmp "$INSTALL_ABSENT/app.before" "$INSTALL_ABSENT/vix.app"
test ! -e "$INSTALL_ABSENT/vix.lock"
assert_clean_mutation_state "$INSTALL_ABSENT"

# Successful direct Git installation commits state and leaves no transaction files.
INSTALL_REPO="$ROOT/install-repo"
INSTALL_COMMIT="$(git_fixture "$INSTALL_REPO" install-fixture)"
INSTALL_OK="$ROOT/install-ok"
mkdir -p "$INSTALL_OK"
printf 'name = "install_ok"\nstandard = "c++20"\n' > "$INSTALL_OK/vix.app"
(cd "$INSTALL_OK" && "$VIX_BIN" install "$INSTALL_REPO" --name local --rev "$INSTALL_COMMIT" --header-only --include . >/dev/null)
grep -q '\[dependencies.local\]' "$INSTALL_OK/vix.app"
grep -q '"source": "git"' "$INSTALL_OK/vix.lock"
assert_clean_mutation_state "$INSTALL_OK"

# Local registry fixtures for update contracts.
A_REPO="$ROOT/a-repo"; A1="$(git_fixture "$A_REPO" a-v1)"
printf 'a-v2\n' > "$A_REPO/README.md"; git -C "$A_REPO" add .; git -C "$A_REPO" commit -q -m a-v2; A2="$(git -C "$A_REPO" rev-parse HEAD)"
B_REPO="$ROOT/b-repo"; B1="$(git_fixture "$B_REPO" b-v1)"
printf 'b-v2\n' > "$B_REPO/README.md"; git -C "$B_REPO" add .; git -C "$B_REPO" commit -q -m b-v2; B2="$(git -C "$B_REPO" rev-parse HEAD)"
cat > "$HOME/.vix/registry/index/index/test.a.json" <<JSON
{"repo":{"url":"$A_REPO"},"versions":{"1.0.0":{"tag":"v1","commit":"$A1"},"2.0.0":{"tag":"v2","commit":"$A2"}}}
JSON
cat > "$HOME/.vix/registry/index/index/test.b.json" <<JSON
{"repo":{"url":"$B_REPO"},"versions":{"1.0.0":{"tag":"v1","commit":"$B1"},"2.0.0":{"tag":"v2","commit":"$B2"}}}
JSON

write_update_project() {
  local project="$1"
  mkdir -p "$project"
  printf '{\n  "deps" : [ { "id" : "test/a", "version" : "1.0.0" }, { "id" : "test/b", "version" : "1.0.0" } ]\n}\n' > "$project/vix.json"
  printf '{ "lockVersion" : 1, "dependencies" : [ { "id":"test/a", "version":"1.0.0", "hash":"old-a" }, { "id":"test/b", "version":"1.0.0", "hash":"old-b" } ] }\n' > "$project/vix.lock"
}

UPDATE_FAIL="$ROOT/update-fail"; write_update_project "$UPDATE_FAIL"
cp "$UPDATE_FAIL/vix.json" "$UPDATE_FAIL/manifest.before"; cp "$UPDATE_FAIL/vix.lock" "$UPDATE_FAIL/lock.before"
rm "$HOME/.vix/registry/index/index/test.b.json"
if (cd "$UPDATE_FAIL" && "$VIX_BIN" update >/dev/null 2>&1); then exit 1; fi
cmp "$UPDATE_FAIL/manifest.before" "$UPDATE_FAIL/vix.json"
cmp "$UPDATE_FAIL/lock.before" "$UPDATE_FAIL/vix.lock"
assert_clean_mutation_state "$UPDATE_FAIL"
test ! -e "$UPDATE_FAIL/.vix/locks/install.lock"; test ! -e "$UPDATE_FAIL/.vix/locks/update.lock"; test ! -e "$UPDATE_FAIL/.vix/locks/module.lock"

UPDATE_MISSING="$ROOT/update-missing-lock"; write_update_project "$UPDATE_MISSING"; rm "$UPDATE_MISSING/vix.lock"
if (cd "$UPDATE_MISSING" && "$VIX_BIN" update >/dev/null 2>&1); then exit 1; fi
test ! -e "$UPDATE_MISSING/vix.lock"
assert_clean_mutation_state "$UPDATE_MISSING"

cat > "$HOME/.vix/registry/index/index/test.b.json" <<JSON
{"repo":{"url":"$B_REPO"},"versions":{"1.0.0":{"tag":"v1","commit":"$B1"},"2.0.0":{"tag":"v2","commit":"$B2"}}}
JSON
UPDATE_OK="$ROOT/update-ok"; write_update_project "$UPDATE_OK"
(cd "$UPDATE_OK" && "$VIX_BIN" update >/dev/null)
grep -q '"version": "2.0.0"' "$UPDATE_OK/vix.json"
test "$(grep -c '"version": "2.0.0"' "$UPDATE_OK/vix.lock")" -eq 2
assert_clean_mutation_state "$UPDATE_OK"

module_failure_case() {
  local project="$1" registration="$2"
  mkdir -p "$project/modules"
  printf '%s' "$registration" > "$project/$3"
  cp "$project/$3" "$project/original"
  if (cd "$project" && VIX_TEST_FAIL_MODULE_REGISTRATION=1 "$VIX_BIN" modules add auth >"$project/failure.log" 2>&1); then exit 1; fi
  grep -q 'Injected module registration failure.' "$project/failure.log"
  test ! -e "$project/modules/auth"
  test -z "$(find "$project/modules" -name '.auth.vix-txn-*' -print -quit)"
  cmp "$project/original" "$project/$3"
  assert_clean_mutation_state "$project"
}

module_failure_case "$ROOT/module-app-fail" $'# ORIGINAL APP\n\nname = "module_app"\nstandard = "c++20"\n' vix.app
module_failure_case "$ROOT/module-cmake-fail" $'# ORIGINAL CMAKE\ncmake_minimum_required(VERSION 3.20)\nproject(module_cmake)\n' CMakeLists.txt

MODULE_OK="$ROOT/module-ok"
mkdir -p "$MODULE_OK/modules"
printf '# KEEP\nname = "module_ok"\nstandard = "c++20"\n' > "$MODULE_OK/vix.app"
(cd "$MODULE_OK" && "$VIX_BIN" modules add auth >/dev/null)
test -f "$MODULE_OK/modules/auth/vix.module"; test -f "$MODULE_OK/modules/auth/CMakeLists.txt"
test -f "$MODULE_OK/modules/auth/include/auth/api.hpp"; test -f "$MODULE_OK/modules/auth/src/auth.cpp"; test -f "$MODULE_OK/modules/auth/tests/test_auth.cpp"
grep -q '\[module.auth\]' "$MODULE_OK/vix.app"
assert_clean_mutation_state "$MODULE_OK"
(cd "$MODULE_OK" && "$VIX_BIN" modules disable auth >/dev/null)
grep -q 'enabled = false' "$MODULE_OK/vix.app"; assert_clean_mutation_state "$MODULE_OK"
(cd "$MODULE_OK" && "$VIX_BIN" modules enable auth >/dev/null)
grep -q 'enabled = true' "$MODULE_OK/vix.app"; assert_clean_mutation_state "$MODULE_OK"
