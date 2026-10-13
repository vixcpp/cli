cmake_minimum_required(VERSION 3.20)
project(vix_p7_ctest_execution LANGUAGES NONE)

enable_testing()
add_custom_target(noop ALL)

add_test(NAME alpha COMMAND "${CMAKE_COMMAND}" -E true)
add_test(NAME beta COMMAND "${CMAKE_COMMAND}" -E true)
add_test(NAME "network.connect[ipv6]+timeout" COMMAND "${CMAKE_COMMAND}" -E true)
add_test(NAME network.connectxiv6timeout COMMAND "${CMAKE_COMMAND}" -E true)
