# ======================================================
# vix.cpp CLI module
# ======================================================

cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ======================================================
# Sources
# ======================================================
file(GLOB_RECURSE CLI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# ======================================================
# Create executable
# ======================================================
add_executable(vix ${CLI_SOURCES})

target_include_directories(vix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/modules/core/include
)

# ======================================================
# Link core library
# ======================================================
target_link_libraries(vix PRIVATE core)

# ======================================================
# Output info
# ======================================================
message(STATUS "CLI module 'vix' executable configured.")

