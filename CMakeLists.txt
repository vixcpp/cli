cmake_minimum_required(VERSION 3.20)

# ====================================================================
# @file CMakeLists.txt
# @brief Build configuration for the Vix.cpp CLI module.
#
# This file defines how the Vix Command Line Interface (CLI) is built,
# linked, installed, and exported within the Vix.cpp ecosystem.
#
# Overview
#   The CLI provides developer-facing commands for creating, building,
#   and running Vix-based C++ applications.
#
# Key Features
#   - Modern C++20 build with strict, portable warnings.
#   - Optional sanitizers (ASan/UBSan) and LTO in Release.
#   - Integrates with the umbrella export-set (VixTargets) so that a
#     system-wide install exposes the CLI in the Vix package context.
#   - Produces a single binary named `vix`.
#
# Typical Build (standalone)
#   cmake -S . -B build
#   cmake --build build -j
#   ./build/vix --help
#
# Integration (preferred)
#   When built from the umbrella project, the CLI inherits global flags
#   and links against core Vix modules via Vix::vix.
# ====================================================================
project(vix_cli VERSION 1.4.2 LANGUAGES CXX)

include(GNUInstallDirs)
include(CheckCXXCompilerFlag)

# (optionnel) Forcer le CLI en "core-only" si l'umbrella exporte VixOrm
# pour éviter toute traction ORM côté CLI.
set(CMAKE_DISABLE_FIND_PACKAGE_VixOrm ON)

# ---------------- Language / Standard ----------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------- Sources ----------------------------
file(GLOB_RECURSE CLI_SOURCES
     CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(vix_cli ${CLI_SOURCES})
# Alias (handy for umbrella exports/imports). CMake supports ALIAS for executables.
add_executable(Vix::cli ALIAS vix_cli)

target_include_directories(vix_cli PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ---------------- Warnings (portable) ----------------
function(vix_add_flag_if_supported tgt flag)
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" flag_var "${flag}")
  set(test_var "HAVE_${flag_var}")
  check_cxx_compiler_flag("${flag}" ${test_var})
  if(${test_var})
    target_compile_options(${tgt} PRIVATE "${flag}")
  endif()
endfunction()

if (MSVC)
  vix_add_flag_if_supported(vix_cli /W4)
  vix_add_flag_if_supported(vix_cli /permissive-)
  # Uncomment to enforce: vix_add_flag_if_supported(vix_cli /WX)
else()
  vix_add_flag_if_supported(vix_cli -Wall)
  vix_add_flag_if_supported(vix_cli -Wextra)
  vix_add_flag_if_supported(vix_cli -Wshadow)
  vix_add_flag_if_supported(vix_cli -Wconversion)
  vix_add_flag_if_supported(vix_cli -Wformat=2)
  vix_add_flag_if_supported(vix_cli -Wpedantic)
endif()

# ---------------- Dependencies -----------------------
# Priority:
#  1) If umbrella target exists → link Vix::vix (brings all modules).
#  2) Else try find_package(Vix) and link Vix::vix.
#  3) Else, as a minimal fallback, link Vix::utils (must be provided).
if (TARGET Vix::vix)
  target_link_libraries(vix_cli PRIVATE Vix::vix)
else()
  find_package(Vix QUIET CONFIG)
  if (Vix_FOUND AND TARGET Vix::vix)
    target_link_libraries(vix_cli PRIVATE Vix::vix)
  elseif (TARGET Vix::utils)
    target_link_libraries(vix_cli PRIVATE Vix::utils)
  else()
    # Last attempt: try to locate Vix package for utils only
    find_package(Vix QUIET CONFIG)
    if (TARGET Vix::utils)
      target_link_libraries(vix_cli PRIVATE Vix::utils)
    else()
      message(FATAL_ERROR
        "Missing dependencies. Build this module via the umbrella project "
        "(target Vix::vix) or install the Vix package that exports Vix::utils.")
    endif()
  endif()
endif()

# ---------------- CLI version wiring (dynamic) -------
# Priorité:
#   1) -DVIX_CLI_VERSION=... (cache)
#   2) env VIX_CLI_VERSION
#   3) dernier tag git (git describe --tags --abbrev=0)
#   4) project() VERSION (project(vix_cli VERSION x.y.z))
#   5) "dev" (fallback)
set(_VIX_CLI_VERSION "dev")

# 1) Cache var
if(DEFINED VIX_CLI_VERSION AND NOT "${VIX_CLI_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "${VIX_CLI_VERSION}")
# 2) Env var
elseif(DEFINED ENV{VIX_CLI_VERSION} AND NOT "$ENV{VIX_CLI_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "$ENV{VIX_CLI_VERSION}")
else()
  # 3) Git tag (silencieux si pas un repo)
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
      OUTPUT_VARIABLE _GIT_TAG
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_GIT_TAG)
      set(_VIX_CLI_VERSION "${_GIT_TAG}")
    endif()
  endif()
  # 4) project() VERSION si dispo
  if("${_VIX_CLI_VERSION}" STREQUAL "dev" AND DEFINED PROJECT_VERSION AND NOT "${PROJECT_VERSION}" STREQUAL "")
    set(_VIX_CLI_VERSION "${PROJECT_VERSION}")
  endif()
endif()

message(STATUS "Vix CLI version: ${_VIX_CLI_VERSION}")
target_compile_definitions(vix_cli PRIVATE VIX_CLI_VERSION="${_VIX_CLI_VERSION}")

# ---------------- Sanitizers (optional) ---------------
# Honours the umbrella option: -DVIX_ENABLE_SANITIZERS=ON
if (VIX_ENABLE_SANITIZERS)
  message(STATUS "[cli] enabling sanitizers on vix_cli")
  if (MSVC)
    # No default ASan/UBSan flags here for MSVC.
  else()
    target_compile_options(vix_cli PRIVATE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(   vix_cli PRIVATE                       -fsanitize=address,undefined)
  endif()
endif()

# ---------------- LTO in Release (opt-in) ------------
if (VIX_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if (_ipo_ok)
    set_property(TARGET vix_cli PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "[cli] IPO/LTO not supported: ${_ipo_msg}")
  endif()
endif()

# ---------------- Output layout ----------------------
# Place the executable at the umbrella/build root (or local build dir).
set_target_properties(vix_cli PROPERTIES
  OUTPUT_NAME vix
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ---------------- Install / Export -------------------
# Install the CLI and contribute it to the umbrella export-set `VixTargets`.
install(TARGETS vix_cli
  EXPORT  VixTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---------------- Status messages --------------------
message(STATUS "CLI module configured.")
message(STATUS "Executable will be: ${CMAKE_BINARY_DIR}/vix")

