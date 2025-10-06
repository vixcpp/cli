# ======================================================
# vix.cpp CLI module
# ======================================================

cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ======================================================
# Collect all CLI sources recursively
# ======================================================
file(GLOB_RECURSE CLI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# ======================================================
# Add CLI executable (avoid naming conflict with INTERFACE vix)
# ======================================================
add_executable(vix_cli ${CLI_SOURCES})

# ======================================================
# Include directories
# ======================================================
target_include_directories(vix_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include                       # CLI headers
    ${CMAKE_SOURCE_DIR}/modules/utils/include                # Utils headers from top-level
)

# ======================================================
# Link against utils library
# ======================================================
target_link_libraries(vix_cli PRIVATE utils)

# ======================================================
# Set output directory to top-level build dir
# ======================================================
set_target_properties(vix_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ======================================================
# Verbose messages for debug / info
# ======================================================
message(STATUS "CLI module 'vix_cli' configured successfully.")
message(STATUS "CLI sources: ${CLI_SOURCES}")
message(STATUS "Includes: ${CMAKE_SOURCE_DIR}/modules/utils/include")
message(STATUS "Executable will be: ${CMAKE_BINARY_DIR}/vix_cli")
