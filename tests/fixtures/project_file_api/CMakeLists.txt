cmake_minimum_required(VERSION 3.20)
project(vix_p3_fixture LANGUAGES CXX)

add_library(core STATIC core.cpp)
add_library(shared SHARED shared.cpp)
add_library(objects OBJECT object.cpp)
add_library(headers INTERFACE)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE core)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE core)

add_custom_target(generate_assets)

add_subdirectory(subproject)
