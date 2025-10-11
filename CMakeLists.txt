# ====================================================================
# @file CMakeLists.txt
# @brief Build configuration for the Vix.cpp CLI module.
#
# This file defines how the Vix Command Line Interface (CLI) is built,
# linked, and installed within the Vix.cpp ecosystem. It supports both
# standalone builds and integration inside the umbrella Vix project.
#
# ## Overview
# The CLI provides developer-facing commands for creating, building,
# and running Vix-based C++ applications.
#
# **Key Features:**
# - Automatically detects the top-level Vix build or standalone mode.
# - Enables modern C++20 compilation with strict warnings.
# - Optionally supports sanitizers (AddressSanitizer, UBSan) and LTO.
# - Builds a single binary executable named **`vix`**.
#
# **Typical Build:**
# ```bash
# cd vixcpp/vix/modules/cli
# cmake -B build -S .
# cmake --build build
# ./build/vix --help
# ```
#
# **Integration Mode (preferred):**
# When built as part of the main umbrella project (target `Vix::vix`),
# this module automatically inherits global compiler flags and links
# against all core Vix modules (utils, json, core, etc.).
#
# @note
# The CLI binary will always be output as `vix` at the build root.
#
# @version 0.1.0
# @date 2025
# @authors
# SoftAdAstra
# ====================================================================

cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1.0 LANGUAGES CXX)

# --- Language / Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Sources
file(GLOB_RECURSE CLI_SOURCES
     CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(vix_cli ${CLI_SOURCES})
add_executable(Vix::cli ALIAS vix_cli)

# --- Includes (specific to CLI)
target_include_directories(vix_cli PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# --- Cross-platform compiler warnings
include(CheckCXXCompilerFlag)
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
else()
  vix_add_flag_if_supported(vix_cli -Wall)
  vix_add_flag_if_supported(vix_cli -Wextra)
  vix_add_flag_if_supported(vix_cli -Wshadow)
  vix_add_flag_if_supported(vix_cli -Wconversion)
endif()

# --- Dependencies
# 1) If built under the umbrella project → link to main Vix target.
# 2) Otherwise fallback to minimal dependency (Vix::utils).
if (TARGET Vix::vix)
  target_link_libraries(vix_cli PRIVATE Vix::vix)
elseif (TARGET Vix::utils)
  target_link_libraries(vix_cli PRIVATE Vix::utils)
else()
  find_package(vix_utils QUIET CONFIG)
  if (vix_utils_FOUND)
    target_link_libraries(vix_cli PRIVATE Vix::utils)
  else()
    message(FATAL_ERROR
      "Missing dependencies. Build this module via the umbrella project "
      "(target Vix::vix) or install vix_utils exporting Vix::utils.")
  endif()
endif()

# --- Sanitizers (optional, inherited from top-level)
# Enable ASan/UBSan when -DVIX_ENABLE_SANITIZERS=ON
if (VIX_ENABLE_SANITIZERS)
  message(STATUS "[cli] enabling sanitizers on vix_cli")
  target_compile_options(vix_cli PRIVATE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
  target_link_options(   vix_cli PRIVATE                       -fsanitize=address,undefined)
endif()

# --- (Optional) LTO for Release if supported
if (VIX_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if (_ipo_ok)
    set_property(TARGET vix_cli PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "[cli] IPO/LTO not supported: ${_ipo_msg}")
  endif()
endif()

# --- Output: binary 'vix' at build root
set_target_properties(vix_cli PROPERTIES
  OUTPUT_NAME vix
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# --- Installation
include(GNUInstallDirs)
install(TARGETS vix_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- Messages
message(STATUS "CLI module configured.")
message(STATUS "Executable will be: ${CMAKE_BINARY_DIR}/vix")
