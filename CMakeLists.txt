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
# Add executable
# ======================================================
add_executable(vix ${CLI_SOURCES})

# ======================================================
# Include directories
# ======================================================
target_include_directories(vix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include           # CLI headers
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/utils/include  # Utils headers relative to CLI
)

# ======================================================
# Link against utils library
# ======================================================
target_link_libraries(vix PRIVATE utils)

# ======================================================
# Set output directory to top-level build dir
# ======================================================
set_target_properties(vix PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ======================================================
# Verbose messages
# ======================================================
message(STATUS "CLI module 'vix' configured.")
message(STATUS "Sources: ${CLI_SOURCES}")
message(STATUS "Includes: ${CMAKE_CURRENT_SOURCE_DIR}/modules/utils/include")
