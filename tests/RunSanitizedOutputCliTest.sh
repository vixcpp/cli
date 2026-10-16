#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT
mkdir -p "$ROOT/home"
mkdir -p "$ROOT/cache" "$ROOT/config" "$ROOT/data"

run_isolated()
{
  env HOME="$ROOT/home" \
    XDG_CACHE_HOME="$ROOT/cache" \
    XDG_CONFIG_HOME="$ROOT/config" \
    XDG_DATA_HOME="$ROOT/data" \
    "$VIX_BIN" run "$@"
}

cat >"$ROOT/healthy.cpp" <<'CPP'
#include <iostream>
int main() {
  std::cout << "VIX_AUTO_DIAGNOSTIC_STDOUT_MARKER\n";
  std::cerr << "VIX_AUTO_DIAGNOSTIC_STDERR_MARKER\n";
  return 0;
}
CPP

if ! (
  cd "$ROOT"
  run_isolated healthy.cpp
) >"$ROOT/healthy.stdout" 2>"$ROOT/healthy.stderr"; then
  cat "$ROOT/healthy.stdout" "$ROOT/healthy.stderr" >&2
  echo "healthy default-diagnostic run unexpectedly failed" >&2
  exit 1
fi

for marker in VIX_AUTO_DIAGNOSTIC_STDOUT_MARKER VIX_AUTO_DIAGNOSTIC_STDERR_MARKER; do
  if ! grep -Fq "$marker" "$ROOT/healthy.stdout" "$ROOT/healthy.stderr"; then
    cat "$ROOT/healthy.stdout" "$ROOT/healthy.stderr" >&2
    echo "default-diagnostic run did not visibly preserve $marker" >&2
    exit 1
  fi
  count="$(grep -hFo "$marker" "$ROOT/healthy.stdout" "$ROOT/healthy.stderr" | wc -l)"
  if [[ "$count" -ne 1 ]]; then
    cat "$ROOT/healthy.stdout" "$ROOT/healthy.stderr" >&2
    echo "default-diagnostic run replayed $marker $count times" >&2
    exit 1
  fi
done

cat >"$ROOT/use_after_free.cpp" <<'CPP'
int main() {
  int *p = new int(42);
  delete p;
  return *p;
}
CPP

if (
  cd "$ROOT"
  run_isolated use_after_free.cpp
) >"$ROOT/uaf.stdout" 2>"$ROOT/uaf.stderr"; then
  cat "$ROOT/uaf.stdout" "$ROOT/uaf.stderr" >&2
  echo "default-diagnostic use-after-free unexpectedly succeeded" >&2
  exit 1
fi

if ! grep -Eiq 'use-after-free' "$ROOT/uaf.stdout" "$ROOT/uaf.stderr"; then
  cat "$ROOT/uaf.stdout" "$ROOT/uaf.stderr" >&2
  echo "default run did not report the use-after-free" >&2
  exit 1
fi

if ! grep -Eq 'use_after_free\.cpp:[0-9]+' "$ROOT/uaf.stdout" "$ROOT/uaf.stderr"; then
  cat "$ROOT/uaf.stdout" "$ROOT/uaf.stderr" >&2
  echo "default run diagnostic omitted the source location" >&2
  exit 1
fi
