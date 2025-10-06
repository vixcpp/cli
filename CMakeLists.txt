# ======================================================
# vix.cpp CLI module
# ======================================================

cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ======================================================
# Sources (inclut tous les .cpp récursivement)
# ======================================================
file(GLOB_RECURSE CLI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# ======================================================
# Create executable
# ======================================================
add_executable(vix ${CLI_SOURCES})

# ======================================================
# Include directories
# ======================================================
target_include_directories(vix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include        # headers CLI
    ${CMAKE_SOURCE_DIR}/modules/core          # headers core + utils
    ${CMAKE_SOURCE_DIR}/modules/core/utils    # explicit utils include
)

# ======================================================
# Link core library
# ======================================================
target_link_libraries(vix PRIVATE core)      

# ======================================================
# Set output directory to top-level build dir
# ======================================================
set_target_properties(vix PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}  
)

# ======================================================
# Optional: verbose message
# ======================================================
message(STATUS "CLI module 'vix' executable configured.")
message(STATUS "All source files included: ${CLI_SOURCES}")
