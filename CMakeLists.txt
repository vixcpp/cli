#  @file CMakeLists.txt
#  @author Gaspard Kirira
#
#  Copyright 2025, Gaspard Kirira.  All rights reserved.
#  https://github.com/vixcpp/vix
#  Use of this source code is governed by a MIT license
#  that can be found in the License file.
#
# ====================================================================
# Vix.cpp - CLI Module
# ====================================================================

cmake_minimum_required(VERSION 3.20)

# ----------------------------------------------------
# Standalone vs Umbrella
# - If this module is built alone: define a project()
# - If built from umbrella: DO NOT call project() here
# ----------------------------------------------------
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(vix_cli VERSION 1.16.5 LANGUAGES CXX)
endif()

include(GNUInstallDirs)
include(CheckCXXCompilerFlag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VIX_ENABLE_LTO "Enable LTO in Release builds" OFF)

# ----------------------------------------------------
# CLI test isolation
# ----------------------------------------------------
option(VIX_CLI_BUILD_TESTS "Build Vix CLI tests" OFF)

if (VIX_CLI_BUILD_TESTS)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

  set(ASYNC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASYNC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

  set(TEMPLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(TEMPLATE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(TEMPLATE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

  set(VIX_JSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(VIX_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(VIX_CRYPTO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(VIX_SYNC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(VIX_CACHE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

file(GLOB_RECURSE CLI_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
# AppProjectResolver belongs to the retired vix.app project architecture.
# Keep the historical source available for reference, but do not compile it
# into the canonical CLI.
list(FILTER CLI_SOURCES EXCLUDE REGEX "/src/app/AppProjectResolver\\.cpp$")
# These command implementations remain historical source assets, but are not
# part of the canonical C++ development CLI.
list(FILTER CLI_SOURCES EXCLUDE REGEX
  "/src/commands/(AgentCommand|GameExportCommand|NoteCommand|DesktopCommand|MobileCommand|MobileCommandUnavailable|OrmCommand|DbCommand|P2PCommand|TaskCommand|ServiceCommand|ProxyCommand|HealthCommand|DeployCommand|LogsCommand|EnvCommand|ProductionCommand|WsCommand)\\.cpp$|/src/commands/replay/ReplayCommand\\.cpp$|/src/commands/(db|ws|env)/.*\\.cpp$")
list(APPEND CLI_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/commands/run/RunDetail.cpp"
)
list(REMOVE_DUPLICATES CLI_SOURCES)

add_executable(vix_cli ${CLI_SOURCES})
add_executable(vix::cli ALIAS vix_cli)

target_include_directories(vix_cli PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(vix_cli SYSTEM PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus>
)

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
  vix_add_flag_if_supported(vix_cli -Wformat=2)
  vix_add_flag_if_supported(vix_cli -Wpedantic)
endif()

# ----------------------------------------------------
# Dependency resolution helpers
# ----------------------------------------------------
set(_VIX_LOCAL_LINK_TARGETS "")
set(_VIX_LOCAL_INCLUDE_DIRS "")

function(vix_append_if_exists out_var path_value)
  if (EXISTS "${path_value}")
    set(${out_var} "${${out_var}};${path_value}" PARENT_SCOPE)
  endif()
endfunction()

# ----------------------------------------------------
# Reply module
# - The CLI REPL command depends on vix::reply.
# - Load it before umbrella/core short-circuit branches.
# ----------------------------------------------------
get_filename_component(_VIX_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(_VIX_REPLY_DIR    "${_VIX_MODULES_DIR}/reply")
set(_VIX_PROCESS_DIR  "${_VIX_MODULES_DIR}/process")
set(_VIX_ENGINE_DIR   "${_VIX_MODULES_DIR}/engine")
set(_VIX_REQUESTS_DIR "${_VIX_MODULES_DIR}/requests")

if (EXISTS "${_VIX_REPLY_DIR}/CMakeLists.txt" AND NOT TARGET vix::reply)
  add_subdirectory("${_VIX_REPLY_DIR}" "${CMAKE_BINARY_DIR}/_vix_reply")
endif()

if (EXISTS "${_VIX_PROCESS_DIR}/CMakeLists.txt" AND NOT TARGET vix::process)
  set(VIX_PROCESS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(VIX_PROCESS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

  add_subdirectory(
    "${_VIX_PROCESS_DIR}"
    "${CMAKE_BINARY_DIR}/_vix_process"
  )
endif()

if (TARGET vix::process)
  target_link_libraries(vix_cli PRIVATE vix::process)
else()
  message(FATAL_ERROR
    "[cli] RegistryCatalog requires the vix::process target"
  )
endif()

if (EXISTS "${_VIX_ENGINE_DIR}/CMakeLists.txt" AND NOT TARGET vix::engine)
  set(VIX_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${_VIX_ENGINE_DIR}" "${CMAKE_BINARY_DIR}/_vix_engine")
endif()

if (TARGET vix::engine)
  target_link_libraries(vix_cli PRIVATE vix::engine)
else()
  message(FATAL_ERROR
    "[cli] Build graph components require the vix::engine target"
  )
endif()

# ----------------------------------------------------
# Link dependencies
# Priority:
# 1) umbrella targets already present
# 2) local sibling modules when building in repo layout
# 3) installed packages via find_package
# ----------------------------------------------------
if (TARGET vix::vix)
  message(STATUS "[cli] Using umbrella target vix::vix")
  target_link_libraries(vix_cli PRIVATE vix::vix)

  if (TARGET vix::reply)
    target_link_libraries(vix_cli PRIVATE vix::reply)
  endif()

elseif (TARGET vix::core)
  message(STATUS "[cli] Using umbrella target vix::core")
  target_link_libraries(vix_cli PRIVATE vix::core)

  if (TARGET vix::reply)
    target_link_libraries(vix_cli PRIVATE vix::reply)
  endif()

else()
  get_filename_component(_VIX_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

  set(_VIX_CORE_DIR     "${_VIX_MODULES_DIR}/core")
  set(_VIX_ASYNC_DIR    "${_VIX_MODULES_DIR}/async")
  set(_VIX_JSON_DIR     "${_VIX_MODULES_DIR}/json")
  set(_VIX_TEMPLATE_DIR "${_VIX_MODULES_DIR}/template")
  set(_VIX_CRYPTO_DIR   "${_VIX_MODULES_DIR}/crypto")
  set(_VIX_SYNC_DIR     "${_VIX_MODULES_DIR}/sync")
  set(_VIX_CACHE_DIR    "${_VIX_MODULES_DIR}/cache")
  set(_VIX_REQUESTS_DIR  "${_VIX_MODULES_DIR}/requests")
  set(_VIX_ENGINE_DIR    "${_VIX_MODULES_DIR}/engine")

  set(_LOCAL_LAYOUT_OK FALSE)
  if (EXISTS "${_VIX_CORE_DIR}/CMakeLists.txt")
    set(_LOCAL_LAYOUT_OK TRUE)
  endif()

  if (_LOCAL_LAYOUT_OK)
    message(STATUS "[cli] Building inside Vix repo -> resolving local sibling modules")

    if (EXISTS "${_VIX_ASYNC_DIR}/CMakeLists.txt" AND NOT TARGET vix::async)
      add_subdirectory("${_VIX_ASYNC_DIR}" "${CMAKE_BINARY_DIR}/_vix_async")
    endif()

    if (EXISTS "${_VIX_JSON_DIR}/CMakeLists.txt" AND NOT TARGET vix::json)
      add_subdirectory("${_VIX_JSON_DIR}" "${CMAKE_BINARY_DIR}/_vix_json")
    endif()

    if (EXISTS "${_VIX_REPLY_DIR}/CMakeLists.txt" AND NOT TARGET vix::reply)
      add_subdirectory("${_VIX_REPLY_DIR}" "${CMAKE_BINARY_DIR}/_vix_reply")
    endif()

    if (EXISTS "${_VIX_TEMPLATE_DIR}/CMakeLists.txt" AND NOT TARGET vix::template)
      add_subdirectory("${_VIX_TEMPLATE_DIR}" "${CMAKE_BINARY_DIR}/_vix_template")
    endif()

    if (EXISTS "${_VIX_CORE_DIR}/CMakeLists.txt" AND NOT TARGET vix::core)
      set(VIX_CORE_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

      add_subdirectory(
        "${_VIX_CORE_DIR}"
        "${CMAKE_BINARY_DIR}/_vix_core"
      )
    endif()

    if (EXISTS "${_VIX_REQUESTS_DIR}/CMakeLists.txt" AND NOT TARGET vix::requests)
      set(VIX_REQUESTS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(VIX_REQUESTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(VIX_REQUESTS_INSTALL OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_VIX_REQUESTS_DIR}" "${CMAKE_BINARY_DIR}/_vix_requests")
    endif()

    if (EXISTS "${_VIX_CRYPTO_DIR}/CMakeLists.txt" AND NOT TARGET vix::crypto)
      set(VIX_CRYPTO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(VIX_CRYPTO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_VIX_CRYPTO_DIR}" "${CMAKE_BINARY_DIR}/_vix_crypto")
    endif()

    if (EXISTS "${_VIX_SYNC_DIR}/CMakeLists.txt" AND NOT TARGET vix::sync)
      add_subdirectory("${_VIX_SYNC_DIR}" "${CMAKE_BINARY_DIR}/_vix_sync")
    endif()

    if (EXISTS "${_VIX_CACHE_DIR}/CMakeLists.txt" AND NOT TARGET vix::cache)
      add_subdirectory("${_VIX_CACHE_DIR}" "${CMAKE_BINARY_DIR}/_vix_cache")
    endif()

    if (EXISTS "${_VIX_ENGINE_DIR}/CMakeLists.txt" AND NOT TARGET vix::engine)
      set(VIX_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_VIX_ENGINE_DIR}" "${CMAKE_BINARY_DIR}/_vix_engine")
    endif()

    if (TARGET vix::core)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::core)
    endif()

    if (TARGET vix::async)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::async)
    endif()

    if (TARGET vix::json)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::json)
    endif()

    if (TARGET vix::reply)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::reply)
    endif()

    if (TARGET vix::template)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::template)
    endif()

    if (TARGET vix::requests)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::requests)
    endif()

    if (TARGET vix::crypto)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::crypto)
    endif()

    if (TARGET vix::sync)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::sync)
    endif()

    if (TARGET vix::cache)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::cache)
    endif()

    if (TARGET vix::engine)
      list(APPEND _VIX_LOCAL_LINK_TARGETS vix::engine)
    endif()

    # Fallback header visibility for modules that expose headers but not imported targets
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_CORE_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_ASYNC_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_JSON_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_REPLY_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_TEMPLATE_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_REQUESTS_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_CRYPTO_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_SYNC_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_CACHE_DIR}/include")
    vix_append_if_exists(_VIX_LOCAL_INCLUDE_DIRS "${_VIX_ENGINE_DIR}/include")

    if (_VIX_LOCAL_LINK_TARGETS)
      list(REMOVE_DUPLICATES _VIX_LOCAL_LINK_TARGETS)
      message(STATUS "[cli] Linking local modules: ${_VIX_LOCAL_LINK_TARGETS}")
      target_link_libraries(vix_cli PRIVATE ${_VIX_LOCAL_LINK_TARGETS})
    else()
      message(FATAL_ERROR
        "[cli] Local Vix repository layout detected, but no usable module targets were created."
      )
    endif()

    if (_VIX_LOCAL_INCLUDE_DIRS)
      list(REMOVE_DUPLICATES _VIX_LOCAL_INCLUDE_DIRS)
      target_include_directories(vix_cli PRIVATE ${_VIX_LOCAL_INCLUDE_DIRS})
    endif()

  else()
    message(STATUS "[cli] Local sibling modules not found -> trying installed packages")

    find_package(Vix CONFIG QUIET)
    find_package(vixcrypto CONFIG QUIET)
    find_package(vix_requests CONFIG QUIET)
    find_package(vix_engine CONFIG QUIET)

    if (TARGET vix::vix)
      message(STATUS "[cli] Using installed package target vix::vix")
      target_link_libraries(vix_cli PRIVATE vix::vix)
    elseif (TARGET vix::core)
      message(STATUS "[cli] Using installed package target vix::core")
      target_link_libraries(vix_cli PRIVATE vix::core)

      if (TARGET vix::crypto)
        target_link_libraries(vix_cli PRIVATE vix::crypto)
      endif()

      if (TARGET vix::engine)
        target_link_libraries(vix_cli PRIVATE vix::engine)
      endif()
    else()
      message(FATAL_ERROR
        "Missing dependencies for vix_cli.\n"
        "Expected one of:\n"
        "  - umbrella targets vix::vix or vix::core\n"
        "  - local sibling modules in ../core, ../crypto, etc.\n"
        "  - installed packages providing vix::vix or vix::core"
      )
    endif()
  endif()
endif()

# ----------------------------------------------------
# Canonical environment dependency
# ----------------------------------------------------
if (NOT TARGET vix::env AND NOT TARGET vix_env)
  if (EXISTS "${_VIX_MODULES_DIR}/env/CMakeLists.txt")
    add_subdirectory("${_VIX_MODULES_DIR}/env" "${CMAKE_BINARY_DIR}/_vix_env")
  else()
    find_package(vix_env CONFIG REQUIRED)
  endif()
endif()

if (TARGET vix::env)
  target_link_libraries(vix_cli PRIVATE vix::env)
elseif (TARGET vix_env)
  target_link_libraries(vix_cli PRIVATE vix_env)
else()
  message(FATAL_ERROR "[cli] Canonical environment access requires vix::env")
endif()

# ----------------------------------------------------
# Canonical logging dependency
# ----------------------------------------------------
if (NOT TARGET vix::log AND NOT TARGET vix_log)
  if (EXISTS "${_VIX_MODULES_DIR}/log/CMakeLists.txt")
    add_subdirectory("${_VIX_MODULES_DIR}/log" "${CMAKE_BINARY_DIR}/_vix_log")
  else()
    find_package(vix_log CONFIG REQUIRED)
  endif()
endif()

if (TARGET vix::log)
  target_link_libraries(vix_cli PRIVATE vix::log)
elseif (TARGET vix_log)
  target_link_libraries(vix_cli PRIVATE vix_log)
else()
  message(FATAL_ERROR "[cli] Canonical logging requires vix::log")
endif()

# ----------------------------------------------------
# Account authentication support
# ----------------------------------------------------
if (TARGET vix::requests)
  message(STATUS "[cli] Account authentication enabled")
  target_link_libraries(vix_cli PRIVATE vix::requests)
  target_compile_definitions(vix_cli PRIVATE VIX_CLI_HAS_REQUESTS=1)
else()
  message(FATAL_ERROR "[cli] Account authentication requires vix::requests")
endif()

# ----------------------------------------------------
# Optional crypto command support
# ----------------------------------------------------
if (TARGET vix::crypto)
  message(STATUS "[cli] Crypto command enabled")
  target_link_libraries(vix_cli PRIVATE vix::crypto)
  target_compile_definitions(vix_cli PRIVATE VIX_CLI_HAS_CRYPTO=1)
else()
  message(STATUS "[cli] Crypto command disabled: vix::crypto target not found")
endif()

# ----------------------------------------------------
if (TARGET vix_warnings)
  target_link_libraries(vix_cli PRIVATE vix_warnings)
endif()

if (COMMAND vix_enable_sanitizers)
  vix_enable_sanitizers(vix_cli)
elseif (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli PRIVATE vix_sanitizers)
endif()

# ----------------------------------------------------
# Version string
# ----------------------------------------------------
set(_VIX_CLI_VERSION "dev")

if (DEFINED VIX_CLI_VERSION AND NOT "${VIX_CLI_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "${VIX_CLI_VERSION}")

elseif (DEFINED ENV{VIX_CLI_VERSION} AND NOT "$ENV{VIX_CLI_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "$ENV{VIX_CLI_VERSION}")

elseif (DEFINED VIX_UMBRELLA_VERSION AND NOT "${VIX_UMBRELLA_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "${VIX_UMBRELLA_VERSION}")

else()
  find_package(Git QUIET)
  if (Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --always
      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
      OUTPUT_VARIABLE _GIT_DESCRIBE
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if (_GIT_DESCRIBE)
      set(_VIX_CLI_VERSION "${_GIT_DESCRIBE}")
    endif()
  endif()

  if ("${_VIX_CLI_VERSION}" STREQUAL "dev" AND DEFINED PROJECT_VERSION AND NOT "${PROJECT_VERSION}" STREQUAL "")
    set(_VIX_CLI_VERSION "v${PROJECT_VERSION}")
  endif()
endif()

message(STATUS "Vix CLI version: ${_VIX_CLI_VERSION}")
target_compile_definitions(vix_cli PRIVATE VIX_CLI_VERSION="${_VIX_CLI_VERSION}")

# ----------------------------------------------------
# LTO (Release only)
# ----------------------------------------------------
if (VIX_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if (_ipo_ok)
    set_property(TARGET vix_cli PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "[cli] IPO/LTO not supported: ${_ipo_msg}")
  endif()
endif()

# ----------------------------------------------------
# Output name + stable output directory for CI packaging
# ----------------------------------------------------
set_target_properties(vix_cli PROPERTIES
  OUTPUT_NAME vix
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
)

if(APPLE)
  set_target_properties(vix_cli PROPERTIES
    BUILD_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
  )
elseif(UNIX)
  set_target_properties(vix_cli PROPERTIES
    BUILD_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
  )
endif()

# ----------------------------------------------------
# Install (standalone only)
# - Umbrella handles install itself
# ----------------------------------------------------
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  install(TARGETS vix_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

# ----------------------------------------------------
# Tests
# ----------------------------------------------------
if (VIX_CLI_BUILD_TESTS)
  set(BUILD_TESTING ON CACHE BOOL "" FORCE)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "CLI module configured.")
message(STATUS "Executable will be: ${CMAKE_BINARY_DIR}/vix")
