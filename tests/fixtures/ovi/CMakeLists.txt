cmake_minimum_required(VERSION 3.20)
project(ovi VERSION 0.1.0 LANGUAGES CXX)

add_library(ovi INTERFACE)
add_library(ovi::ovi ALIAS ovi)

target_include_directories(ovi INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

add_executable(ovi_cli src/main.cpp)
set_target_properties(ovi_cli PROPERTIES OUTPUT_NAME ovi)
target_link_libraries(ovi_cli PRIVATE ovi::ovi)

add_executable(ovi_doctor_cli src/doctor.cpp)
set_target_properties(ovi_doctor_cli PROPERTIES OUTPUT_NAME ovi-doctor)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS ovi EXPORT oviTargets)
install(TARGETS ovi_cli ovi_doctor_cli RUNTIME DESTINATION bin)
install(EXPORT oviTargets FILE oviTargets.cmake NAMESPACE ovi:: DESTINATION lib/cmake/ovi)
