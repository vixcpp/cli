#!/usr/bin/env bash
set -euo pipefail

VIX_BIN="${1:?vix binary required}"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/vix-sdk-compose-test.XXXXXX")"
trap 'rm -rf "$ROOT"' EXIT

make_profile() {
  local home="$1"
  local profile="$2"
  local version="$3"
  shift 3
  local install="$home/sdk-$profile"
  mkdir -p "$home/.vix/sdk/$profile" "$install/lib/cmake/Vix" "$install/include"
  cat > "$home/.vix/sdk/$profile/current.json" <<JSON
{"installed_version":"$version","install_dir":"$install"}
JSON
  cat > "$install/lib/cmake/Vix/VixConfig.cmake" <<'CMAKE'
get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/VixTargets.cmake")
if (TARGET vix::vix_validation AND NOT TARGET vix::validation)
  add_library(vix::validation ALIAS vix::vix_validation)
endif()
if (TARGET vix::vix_webrpc AND NOT TARGET vix::webrpc)
  add_library(vix::webrpc ALIAS vix::vix_webrpc)
endif()
CMAKE
  {
    printf '# Generated fake VixTargets.cmake for SDK composition tests\n'
    for spec in "$@"; do
      local target="${spec%%|*}"
      local links=""
      if [[ "$spec" == *"|"* ]]; then
        links="${spec#*|}"
      fi
      printf '# Create imported target %s\n' "$target"
      printf 'if(NOT TARGET %s)\n' "$target"
      printf '  add_library(%s INTERFACE IMPORTED)\n' "$target"
      if [[ -n "$links" ]]; then
        printf '  set_target_properties(%s PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include" INTERFACE_LINK_LIBRARIES "%s")\n' "$target" "$links"
      else
        printf '  set_target_properties(%s PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include")\n' "$target"
      fi
      printf 'endif()\n\n'
    done
    printf '# Load information for each installed configuration.\n'
  } > "$install/lib/cmake/Vix/VixTargets.cmake"
}

make_project() {
  local dir="$1"
  shift
  mkdir -p "$dir"
  cat > "$dir/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.20)
project(vix_sdk_profile_fixture LANGUAGES CXX)
find_package(Vix CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE $*)
CMAKE
  if [[ " $* " == *" vix::validation "* ]]; then
    cat > "$dir/main.cpp" <<'CPP'
#include <vix/validation/Validate.hpp>
int main() { return vix_validation_header_marker(); }
CPP
  else
    cat > "$dir/main.cpp" <<'CPP'
int main() { return 0; }
CPP
  fi
}

composed_config() {
  local project="$1"
  find "$project" -path '*/.vix-sdk/*/lib/cmake/Vix/VixConfig.cmake' -print -quit
}

run_build() {
  local home="$1"
  local project="$2"
  HOME="$home" "$VIX_BIN" build --preset release --dir "$project" -- -DCMAKE_VERBOSE_MAKEFILE=OFF >/tmp/vix-sdk-compose-build.log 2>&1
}

HOME_A="$ROOT/home-a"
mkdir -p "$HOME_A"
make_profile "$HOME_A" data v2.7.3 vix::vix vix::core vix::cache vix::utils vix::json SQLite::SQLite3 'vix::db|SQLite::SQLite3;vix::json' vix::orm vix::kv 'vix::vix_validation|vix::core'
make_profile "$HOME_A" web v2.7.3 vix::vix vix::core vix::cache vix::utils vix::json vix::requests vix::vix_webrpc vix::middleware 'vix::websocket|vix::core;vix::utils;vix::json' vix::vix_validation
mkdir -p "$HOME_A/sdk-web/include/vix/validation"
cat > "$HOME_A/sdk-web/include/vix/validation/Validate.hpp" <<'HPP'
#pragma once
inline int vix_validation_header_marker() { return 0; }
HPP

make_project "$ROOT/project-web-data" vix::db vix::websocket
run_build "$HOME_A" "$ROOT/project-web-data"
config_web_data="$(composed_config "$ROOT/project-web-data")"
if [[ -z "$config_web_data" ]]; then
  echo "expected composed SDK config for web+data" >&2
  exit 1
fi
if ! grep -q 'add_library(vix::websocket INTERFACE IMPORTED)' "$config_web_data"; then
  cat "$config_web_data" >&2
  exit 1
fi
if ! grep -q 'INTERFACE_LINK_LIBRARIES "vix::core;vix::utils;vix::json"' "$config_web_data"; then
  cat "$config_web_data" >&2
  exit 1
fi
if grep -A4 'set_target_properties(vix::websocket' "$config_web_data" | grep -q 'SQLite::SQLite3'; then
  cat "$config_web_data" >&2
  exit 1
fi

make_project "$ROOT/project-validation" vix::db vix::validation
run_build "$HOME_A" "$ROOT/project-validation"
config_validation="$(composed_config "$ROOT/project-validation")"
if [[ -z "$config_validation" ]]; then
  echo "expected composed SDK config for validation" >&2
  exit 1
fi
if ! grep -q "$HOME_A/sdk-web/include" "$config_validation"; then
  cat "$config_validation" >&2
  exit 1
fi
if grep -A4 'set_target_properties(vix::vix_validation' "$config_validation" | grep -q "$HOME_A/sdk-data/include"; then
  cat "$config_validation" >&2
  exit 1
fi

make_project "$ROOT/project-web" vix::websocket vix::requests vix::webrpc vix::middleware
run_build "$HOME_A" "$ROOT/project-web"

make_project "$ROOT/project-data" vix::db vix::orm vix::cache vix::kv
run_build "$HOME_A" "$ROOT/project-data"

HOME_B="$ROOT/home-b"
mkdir -p "$HOME_B"
make_profile "$HOME_B" web v2.7.3 vix::vix vix::core vix::cache vix::utils vix::json vix::requests vix::vix_webrpc vix::middleware 'vix::websocket|vix::core;vix::utils;vix::json' vix::vix_validation
make_profile "$HOME_B" data v2.7.3 vix::vix vix::core vix::cache vix::utils vix::json SQLite::SQLite3 'vix::db|SQLite::SQLite3;vix::json' vix::orm vix::kv
make_project "$ROOT/project-order" vix::db vix::websocket
run_build "$HOME_B" "$ROOT/project-order"

HOME_MISSING="$ROOT/home-missing"
mkdir -p "$HOME_MISSING"
make_profile "$HOME_MISSING" data v2.7.3 vix::vix vix::core vix::cache vix::utils vix::json SQLite::SQLite3 'vix::db|SQLite::SQLite3;vix::json' vix::orm vix::kv
make_project "$ROOT/project-missing-web" vix::websocket
set +e
HOME="$HOME_MISSING" "$VIX_BIN" build --preset release --dir "$ROOT/project-missing-web" >"$ROOT/missing.log" 2>&1
status=$?
set -e
if [[ "$status" -eq 0 ]]; then
  echo "expected missing web SDK build to fail" >&2
  exit 1
fi
if ! grep -q "Missing SDK modules" "$ROOT/missing.log"; then
  cat "$ROOT/missing.log" >&2
  exit 1
fi
if ! grep -q "vix::websocket  provider profile: web" "$ROOT/missing.log"; then
  cat "$ROOT/missing.log" >&2
  exit 1
fi
if ! grep -q "vix upgrade --sdk web" "$ROOT/missing.log"; then
  cat "$ROOT/missing.log" >&2
  exit 1
fi
if grep -q "unresolved CMake target" "$ROOT/missing.log"; then
  cat "$ROOT/missing.log" >&2
  exit 1
fi
if grep -q "Unable to determine the project directory" "$ROOT/missing.log"; then
  cat "$ROOT/missing.log" >&2
  exit 1
fi

HOME_VERSION="$ROOT/home-version"
mkdir -p "$HOME_VERSION"
make_profile "$HOME_VERSION" data v2.7.3 vix::vix vix::core vix::cache vix::utils vix::json SQLite::SQLite3 'vix::db|SQLite::SQLite3;vix::json'
make_profile "$HOME_VERSION" web v2.8.0 vix::vix vix::core vix::cache vix::utils vix::json 'vix::websocket|vix::core;vix::utils;vix::json'
make_project "$ROOT/project-version" vix::db vix::websocket
set +e
HOME="$HOME_VERSION" "$VIX_BIN" build --preset release --dir "$ROOT/project-version" >"$ROOT/version.log" 2>&1
status=$?
set -e
if [[ "$status" -eq 0 ]]; then
  echo "expected incompatible SDK versions to fail" >&2
  exit 1
fi
if ! grep -q "incompatible versions" "$ROOT/version.log"; then
  cat "$ROOT/version.log" >&2
  exit 1
fi
