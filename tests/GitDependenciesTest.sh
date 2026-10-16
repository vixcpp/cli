#!/usr/bin/env bash
# Current Git dependency front door rejects old project/global-install forms.
set -euo pipefail
VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME" "$ROOT/legacy" "$ROOT/empty"
cat >"$ROOT/legacy/vix.app" <<'APP'
name = "retired-git-app"
standard = "c++20"
APP
printf 'int main() { return 0; }\n' >"$ROOT/legacy/main.cpp"
if (cd "$ROOT/legacy" && "$VIX_BIN" add ./dependency) >/dev/null 2>&1; then
  echo 'Git add accepted a legacy vix.app project as authority' >&2; exit 1
fi
if (cd "$ROOT/empty" && "$VIX_BIN" install ./dependency) >/dev/null 2>&1; then
  echo 'vix install accepted a package selector' >&2; exit 1
fi
if (cd "$ROOT/empty" && "$VIX_BIN" add ./dependency --branch main --tag v1) >/dev/null 2>&1; then
  echo 'mutually exclusive Git revision selectors were accepted' >&2; exit 1
fi
test ! -e "$ROOT/legacy/vix.toml"
test ! -e "$ROOT/legacy/vix.lock"
test ! -e "$ROOT/empty/vix.lock"
echo "GitDependenciesTest passed (current front-door rejection coverage)"
