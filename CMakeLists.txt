# ======================================================
# vix.cpp CLI module
# ======================================================

cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ======================================================
# Sources (include all .cpp recursively)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/include        # headers CLI
    ${CMAKE_SOURCE_DIR}/modules/utils/include  # headers utils
)

# ======================================================
# Link libraries
# ======================================================
target_link_libraries(vix PRIVATE utils)

# ======================================================
# Output directory
# ======================================================
set_target_properties(vix PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}  
)

# ======================================================
# Optional: verbose messages
# ======================================================
message(STATUS "CLI module 'vix' executable configured.")
message(STATUS "All source files included: ${CLI_SOURCES}")
message(STATUS "Includes: ${CMAKE_SOURCE_DIR}/modules/utils/include")
