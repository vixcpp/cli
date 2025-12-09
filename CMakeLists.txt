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
#   and links against core Vix modules via vix::vix.
# ====================================================================
project(vix_cli VERSION 1.6.4 LANGUAGES CXX)

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
add_executable(vix::cli ALIAS vix_cli)

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
# Strategy:
#   1) If umbrella has already defined vix::vix or vix::core → just use them.
#   2) Else, if we're in the monorepo (../core & ../utils exist) → add_subdirectory().
#   3) Else, fall back to the installed Vix package (find_package(Vix)).

# 1) Already in umbrella build?
if (TARGET vix::vix)
  message(STATUS "[cli] Using umbrella target vix::vix")
  target_link_libraries(vix_cli PRIVATE vix::vix)

elseif (TARGET vix::core)
  message(STATUS "[cli] Using umbrella target vix::core")
  target_link_libraries(vix_cli PRIVATE vix::core)

else()
  # 2) Monorepo, but not added via umbrella (e.g. running `vix build` in modules/cli)
  get_filename_component(_VIX_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  set(_VIX_CORE_DIR  "${_VIX_MODULES_DIR}/core")
  set(_VIX_UTILS_DIR "${_VIX_MODULES_DIR}/utils")

  if (EXISTS "${_VIX_CORE_DIR}/CMakeLists.txt" AND EXISTS "${_VIX_UTILS_DIR}/CMakeLists.txt")
    message(STATUS "[cli] Building inside Vix repo → adding local modules/core & modules/utils")
    add_subdirectory("${_VIX_UTILS_DIR}" "${CMAKE_BINARY_DIR}/_vix_utils")
    add_subdirectory("${_VIX_CORE_DIR}"  "${CMAKE_BINARY_DIR}/_vix_core")
    target_link_libraries(vix_cli PRIVATE vix::core vix::utils)

  else()
    # 3) Standalone build against system-installed Vix package
    find_package(Vix CONFIG REQUIRED)
    if (TARGET vix::vix)
      message(STATUS "[cli] Using system Vix package (vix::vix)")
      target_link_libraries(vix_cli PRIVATE vix::vix)
    elseif (TARGET vix::utils)
      message(STATUS "[cli] Using system Vix package (vix::utils)")
      target_link_libraries(vix_cli PRIVATE vix::utils)
    else()
      message(FATAL_ERROR
        "Missing dependencies for vix_cli.\n"
        " - Either build the CLI from the umbrella project, or\n"
        " - Ensure a proper VixConfig.cmake is installed (exporting vix::vix or vix::utils)."
      )
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

