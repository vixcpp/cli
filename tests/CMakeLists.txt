#  @file CMakeLists.txt
#  @author Gaspard Kirira
#
#  Copyright 2025, Gaspard Kirira.
#  All rights reserved.
#  https://github.com/vixcpp/vix
#
#  Use of this source code is governed by a MIT license
#  that can be found in the License file.

add_executable(vix_cli_cmake_build_errors_tests
  CMakeBuildErrorsTests.cpp
  ../src/errors/build/CMakeBuildErrors.cpp
  ../src/build/BuildStyle.cpp
)

target_include_directories(vix_cli_cmake_build_errors_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if (TARGET vix::core)
  target_link_libraries(vix_cli_cmake_build_errors_tests PRIVATE vix::core)
endif()

if (TARGET vix::utils)
  target_link_libraries(vix_cli_cmake_build_errors_tests PRIVATE vix::utils)
endif()

if (TARGET vix::json)
  target_link_libraries(vix_cli_cmake_build_errors_tests PRIVATE vix::json)
endif()

if (TARGET vix_warnings)
  target_link_libraries(vix_cli_cmake_build_errors_tests PRIVATE vix_warnings)
endif()

add_test(
  NAME vix_cli_cmake_build_errors_tests
  COMMAND vix_cli_cmake_build_errors_tests
)


add_test(
  NAME vix_cli_global_install_ovi
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/GlobalInstallOviTest.sh
    ${CMAKE_BINARY_DIR}/vix
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ovi
)

add_test(
  NAME vix_cli_git_dependencies
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/GitDependenciesTest.sh
    ${CMAKE_BINARY_DIR}/vix
)
