#!/usr/bin/env bash
# Failed mutations leave format-1 project authority untouched; retired flags reject.
set -euo pipefail
VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"; trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"; mkdir -p "$HOME"
PROJECT="$ROOT/project"
"$VIX_BIN" new "$PROJECT" >/dev/null
cp "$PROJECT/vix.toml" "$ROOT/manifest.before"
if "$VIX_BIN" add "file://$ROOT/missing-repository" --branch main --version compatible=1.0.0 >"$ROOT/add.out" 2>&1; then
  echo 'missing local Git package unexpectedly mutated the project' >&2; exit 1
fi
cmp "$ROOT/manifest.before" "$PROJECT/vix.toml"
test ! -e "$PROJECT/vix.lock"
if (cd "$PROJECT" && "$VIX_BIN" remove example/missing >/dev/null 2>&1); then
  echo 'remove accepted a dependency that is not declared' >&2; exit 1
fi
cmp "$ROOT/manifest.before" "$PROJECT/vix.toml"
if (cd "$PROJECT" && "$VIX_BIN" publish --dry-run >/dev/null 2>&1); then
  echo 'retired publish --dry-run was accepted' >&2; exit 1
fi
if (cd "$PROJECT" && "$VIX_BIN" update extra >/dev/null 2>&1); then
  echo 'update accepted a package selector' >&2; exit 1
fi
echo "MutationCommandContractsTest passed"
