#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?missing vix binary}"

reject_package_install() {
  local label="$1"
  shift
  local output
  if output="$("$VIX_BIN" install "$@" 2>&1)"; then
    echo "retired package installation syntax was accepted: $label" >&2
    exit 1
  fi
  grep -Fq -- 'usage: vix install [--offline]' <<<"$output" || {
    echo "package installation rejection did not explain the supported install syntax: $label" >&2
    echo "$output" >&2
    exit 1
  }
}

reject_package_install 'install <package>' vixcpp/ovi
reject_package_install 'install -g <package>' -g vixcpp/ovi
