cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(vix
    src/main.cpp
    src/CLI.cpp
    src/commands/NewCommand.cpp
    src/commands/RunCommand.cpp
    src/commands/BuildCommand.cpp
)

target_include_directories(vix PUBLIC include)

find_package(vix_core REQUIRED)
target_link_libraries(vix PRIVATE vix_core)
