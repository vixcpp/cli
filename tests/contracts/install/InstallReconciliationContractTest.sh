#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
export HOME="$ROOT/home"
mkdir -p "$HOME"

fail() { echo "InstallReconciliationContractTest: $*" >&2; exit 1; }

# Historical vix.app data is not project authority. Init establishes the
# current native project, and install remains a no-op without declared roots.
APP="$ROOT/app"
mkdir -p "$APP"
cat >"$APP/vix.app" <<'EOF'
name = "historical-only"
type = "executable"
[dependencies.legacy]
git = "file:///not-a-current-dependency-source"
EOF
(cd "$APP" && "$VIX_BIN" init >/dev/null)
test -f "$APP/vix.toml" || fail 'init did not create native project authority'
test ! -e "$APP/vix.lock" || fail 'init unexpectedly created a dependency lock'
(cd "$APP" && "$VIX_BIN" install --offline >/dev/null) || fail 'offline install failed for a valid empty native project'
(cd "$APP" && "$VIX_BIN" install >/dev/null) || fail 'online install failed for a valid empty native project'
test ! -e "$APP/vix.lock" || fail 'no-op install created or rewrote a lock graph'

# A legacy manifest alone must not authorize package installation.
LEGACY_ONLY="$ROOT/legacy-only"
mkdir -p "$LEGACY_ONLY"
cp "$APP/vix.app" "$LEGACY_ONLY/vix.app"
if output="$(cd "$LEGACY_ONLY" && "$VIX_BIN" install 2>&1)"; then
  fail 'install accepted legacy vix.app without vix.toml'
fi
grep -Fq -- 'native-project-required (vix.toml not found)' <<<"$output" || {
  fail 'legacy-only project rejection did not identify missing native authority'
}

echo "InstallReconciliationContractTest passed"
