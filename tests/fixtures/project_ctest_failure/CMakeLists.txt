cmake_minimum_required(VERSION 3.20)
project(vix_p7_failure LANGUAGES CXX)

enable_testing()
add_custom_target(noop ALL)
add_test(NAME intentional_failure COMMAND "${CMAKE_COMMAND}" -E false)
