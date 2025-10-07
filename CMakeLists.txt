# ====================================================================
# Vix.cpp — CLI Module (Optional)
# ====================================================================
# Purpose:
#   - Developer command-line tooling (scaffolding, dev server, etc.).
#
# Target:
#   - vix_cli (executable)
#
# Notes:
#   - Kept independent from core to keep runtime deps minimal.
# ====================================================================

cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE CLI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(vix_cli ${CLI_SOURCES})

target_include_directories(vix_cli PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/modules/utils/include>
)

target_link_libraries(vix_cli PRIVATE Vix::utils)

# Put the binary next to other top-level outputs for convenience
set_target_properties(vix_cli PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

message(STATUS "CLI module 'vix_cli' configured successfully.")
message(STATUS "Executable will be: ${CMAKE_BINARY_DIR}/vix_cli")
