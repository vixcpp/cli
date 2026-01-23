#  @file CMakeLists.cpp
#  @author Gaspard Kirira
#
#  Copyright 2025, Gaspard Kirira.  All rights reserved.
#  https://github.com/vixcpp/vix
#  Use of this source code is governed by a MIT license
#  that can be found in the License file.
#
# ====================================================================
# Vix.cpp — CLI Module
# ====================================================================
cmake_minimum_required(VERSION 3.20)

project(vix_cli VERSION 1.16.5 LANGUAGES CXX)

include(GNUInstallDirs)
include(CheckCXXCompilerFlag)

set(CMAKE_DISABLE_FIND_PACKAGE_VixOrm ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE CLI_SOURCES
     CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(vix_cli ${CLI_SOURCES})
add_executable(vix::cli ALIAS vix_cli)

target_include_directories(vix_cli PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
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

if (TARGET vix::vix)
  message(STATUS "[cli] Using umbrella target vix::vix")
  target_link_libraries(vix_cli PRIVATE vix::vix)

elseif (TARGET vix::core)
  message(STATUS "[cli] Using umbrella target vix::core")
  target_link_libraries(vix_cli PRIVATE vix::core)

else()
  get_filename_component(_VIX_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  set(_VIX_CORE_DIR  "${_VIX_MODULES_DIR}/core")
  set(_VIX_UTILS_DIR "${_VIX_MODULES_DIR}/utils")

  if (EXISTS "${_VIX_CORE_DIR}/CMakeLists.txt" AND EXISTS "${_VIX_UTILS_DIR}/CMakeLists.txt")
    message(STATUS "[cli] Building inside Vix repo → adding local modules/core & modules/utils")
    add_subdirectory("${_VIX_UTILS_DIR}" "${CMAKE_BINARY_DIR}/_vix_utils")
    add_subdirectory("${_VIX_CORE_DIR}"  "${CMAKE_BINARY_DIR}/_vix_core")
    target_link_libraries(vix_cli PRIVATE vix::core)

  else()
    find_package(Vix CONFIG REQUIRED)
    if (TARGET vix::vix)
      message(STATUS "[cli] Using system Vix package (vix::vix)")
      target_link_libraries(vix_cli PRIVATE vix::vix)
    elseif (TARGET vix::core)
      message(STATUS "[cli] Using system Vix package (vix::core)")
      target_link_libraries(vix_cli PRIVATE vix::core)
    else()
      message(FATAL_ERROR
        "Missing dependencies for vix_cli.\n"
        "Expected vix::vix or vix::core from the installed Vix package."
      )
    endif()
  endif()
endif()

if (TARGET vix_warnings)
  target_link_libraries(vix_cli PRIVATE vix_warnings)
endif()

if (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli PRIVATE vix_sanitizers)
endif()

set(_VIX_CLI_VERSION "dev")

if(DEFINED VIX_CLI_VERSION AND NOT "${VIX_CLI_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "${VIX_CLI_VERSION}")

elseif(DEFINED ENV{VIX_CLI_VERSION} AND NOT "$ENV{VIX_CLI_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "$ENV{VIX_CLI_VERSION}")

elseif(DEFINED VIX_UMBRELLA_VERSION AND NOT "${VIX_UMBRELLA_VERSION}" STREQUAL "")
  set(_VIX_CLI_VERSION "${VIX_UMBRELLA_VERSION}")

else()
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --always
      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
      OUTPUT_VARIABLE _GIT_DESCRIBE
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_GIT_DESCRIBE)
      set(_VIX_CLI_VERSION "${_GIT_DESCRIBE}")
    endif()
  endif()

  if("${_VIX_CLI_VERSION}" STREQUAL "dev" AND DEFINED PROJECT_VERSION AND NOT "${PROJECT_VERSION}" STREQUAL "")
    set(_VIX_CLI_VERSION "v${PROJECT_VERSION}")
  endif()
endif()

message(STATUS "Vix CLI version: ${_VIX_CLI_VERSION}")
target_compile_definitions(vix_cli PRIVATE VIX_CLI_VERSION="${_VIX_CLI_VERSION}")

if (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli PRIVATE vix_sanitizers)
endif()

if (VIX_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if (_ipo_ok)
    set_property(TARGET vix_cli PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "[cli] IPO/LTO not supported: ${_ipo_msg}")
  endif()
endif()

set_target_properties(vix_cli PROPERTIES
  OUTPUT_NAME vix
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

install(TARGETS vix_cli
  EXPORT  VixTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

message(STATUS "CLI module configured.")
message(STATUS "Executable will be: ${CMAKE_BINARY_DIR}/vix")

