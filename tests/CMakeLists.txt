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

if (COMMAND vix_enable_sanitizers)
  vix_enable_sanitizers(vix_cli_cmake_build_errors_tests)
elseif (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli_cmake_build_errors_tests PRIVATE vix_sanitizers)
endif()

add_test(
  NAME vix_cli_cmake_build_errors_tests
  COMMAND vix_cli_cmake_build_errors_tests
)

add_executable(vix_cli_git_progress_parser_tests GitProgressParserTests.cpp)
target_include_directories(vix_cli_git_progress_parser_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME vix_cli_git_progress_parser_tests COMMAND vix_cli_git_progress_parser_tests)

add_executable(vix_cli_module_manifest_tests ModuleManifestTests.cpp
  ../src/modules/ModuleManifest.cpp ../src/commands/modules/ModulesContent.cpp)
target_include_directories(vix_cli_module_manifest_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME vix_cli_module_manifest_tests COMMAND vix_cli_module_manifest_tests)

add_executable(vix_cli_module_graph_tests ModuleGraphTests.cpp
  ../src/modules/ModuleGraph.cpp)
target_include_directories(vix_cli_module_graph_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME vix_cli_module_graph_tests COMMAND vix_cli_module_graph_tests)

add_executable(vix_cli_dependency_ownership_tests DependencyOwnershipTests.cpp
  ../src/modules/DependencyOwnership.cpp ../src/modules/ModuleGraph.cpp
  ../src/modules/ModuleManifest.cpp ../src/app/AppManifest.cpp
  ../src/app/AppCMakeGenerator.cpp)
target_include_directories(vix_cli_dependency_ownership_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME vix_cli_dependency_ownership_tests COMMAND vix_cli_dependency_ownership_tests)

add_executable(vix_cli_dependency_constraints_tests DependencyConstraintsTests.cpp
  ../src/modules/DependencyConstraints.cpp ../src/util/Semver.cpp)
target_include_directories(vix_cli_dependency_constraints_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME vix_cli_dependency_constraints_tests COMMAND vix_cli_dependency_constraints_tests)

file(GLOB VIX_RUNTIME_DIAGNOSTIC_RULE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/errors/runtime/*.cpp"
)

file(GLOB VIX_COMPILER_DIAGNOSTIC_RULE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/errors/rules/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/errors/template/*.cpp"
)

add_executable(vix_cli_runtime_diagnostics_tests
  RuntimeDiagnosticsTests.cpp
  ../src/errors/RawLogDetectors.cpp
  ../src/errors/ErrorPipeline.cpp
  ../src/errors/CodeFrame.cpp
  ${VIX_RUNTIME_DIAGNOSTIC_RULE_SOURCES}
  ${VIX_COMPILER_DIAGNOSTIC_RULE_SOURCES}
)

target_include_directories(vix_cli_runtime_diagnostics_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if (TARGET vix_warnings)
  target_link_libraries(vix_cli_runtime_diagnostics_tests PRIVATE vix_warnings)
endif()

add_test(
  NAME vix_cli_runtime_diagnostics_tests
  COMMAND vix_cli_runtime_diagnostics_tests
)

add_executable(vix_cli_package_hash_tests
  PackageHashTests.cpp
  ../src/util/Hash.cpp
  ../src/util/Fs.cpp
)

target_include_directories(vix_cli_package_hash_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if (TARGET vix::crypto)
  target_link_libraries(vix_cli_package_hash_tests PRIVATE vix::crypto)
endif()

if (TARGET vix::utils)
  target_link_libraries(vix_cli_package_hash_tests PRIVATE vix::utils)
endif()

if (TARGET vix_warnings)
  target_link_libraries(vix_cli_package_hash_tests PRIVATE vix_warnings)
endif()

if (COMMAND vix_enable_sanitizers)
  vix_enable_sanitizers(vix_cli_package_hash_tests)
elseif (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli_package_hash_tests PRIVATE vix_sanitizers)
endif()

add_test(
  NAME vix_cli_package_hash_tests
  COMMAND vix_cli_package_hash_tests
)

add_executable(vix_cli_build_task_process_executor_tests
  BuildTaskProcessExecutorTests.cpp
  ../src/build/BuildTaskProcessExecutor.cpp
)

target_include_directories(vix_cli_build_task_process_executor_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if (TARGET vix::engine)
  target_link_libraries(vix_cli_build_task_process_executor_tests PRIVATE vix::engine)
endif()

if (TARGET vix_warnings)
  target_link_libraries(vix_cli_build_task_process_executor_tests PRIVATE vix_warnings)
endif()

if (COMMAND vix_enable_sanitizers)
  vix_enable_sanitizers(vix_cli_build_task_process_executor_tests)
elseif (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli_build_task_process_executor_tests PRIVATE vix_sanitizers)
endif()

add_test(
  NAME vix_cli_build_task_process_executor_tests
  COMMAND vix_cli_build_task_process_executor_tests
)

add_executable(vix_cli_build_context_compat_tests
  BuildContextCompatTests.cpp
)

target_include_directories(vix_cli_build_context_compat_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if (TARGET vix::engine)
  target_link_libraries(vix_cli_build_context_compat_tests PRIVATE vix::engine)
endif()

if (TARGET vix_warnings)
  target_link_libraries(vix_cli_build_context_compat_tests PRIVATE vix_warnings)
endif()

if (COMMAND vix_enable_sanitizers)
  vix_enable_sanitizers(vix_cli_build_context_compat_tests)
elseif (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli_build_context_compat_tests PRIVATE vix_sanitizers)
endif()

add_test(
  NAME vix_cli_build_context_compat_tests
  COMMAND vix_cli_build_context_compat_tests
)

add_executable(vix_cli_build_tool_args_tests
  BuildToolArgsTests.cpp
)

target_include_directories(vix_cli_build_tool_args_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if (TARGET vix::engine)
  target_link_libraries(vix_cli_build_tool_args_tests PRIVATE vix::engine)
endif()

if (TARGET vix_warnings)
  target_link_libraries(vix_cli_build_tool_args_tests PRIVATE vix_warnings)
endif()

if (COMMAND vix_enable_sanitizers)
  vix_enable_sanitizers(vix_cli_build_tool_args_tests)
elseif (VIX_ENABLE_SANITIZERS AND TARGET vix_sanitizers)
  target_link_libraries(vix_cli_build_tool_args_tests PRIVATE vix_sanitizers)
endif()

add_test(
  NAME vix_cli_build_tool_args_tests
  COMMAND vix_cli_build_tool_args_tests
)

add_test(
  NAME vix_cli_build_tool_cli_compat
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/BuildToolCliCompatTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_build_planning_compat
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/BuildPlanningCompatTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_build_watch
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/BuildWatchCliTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_build_progress
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/BuildProgressCliTest.sh
    ${CMAKE_BINARY_DIR}/vix
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

add_test(
  NAME vix_cli_init_and_auto_git
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/InitAndAutoGitTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_run_script_dependency_path
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/RunScriptDependencyPathTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_run_single_cpp_cache
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/RunSingleCppCacheCliTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_publish_preflight
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/PublishPreflightTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_registry_integrity
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/RegistryIntegrityTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_sdk_profile_composition
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/SdkProfileCompositionTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_native_cmake_discovery
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/BuildNativeCMakeDiscoveryTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

# Contract tests deliberately consume the built CLI, so help text, parser and
# observable execution stay tied together. The App test is a real regression
# contract when an installed SDK include directory is supplied.
add_test(NAME vix_cli_public_option_coverage
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/PublicOptionCoverageTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_capability_coverage
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/CapabilityCoverageGateTest.sh)
add_test(NAME vix_cli_command_coverage
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/CommandCoverageGateTest.sh)
add_test(NAME vix_cli_global_option_coverage
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/GlobalOptionCoverageGateTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_global_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/GlobalCliContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_all_contracts
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/AllCliContractsTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_core_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunCoreContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_cache_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunCacheContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_build_core_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/build/BuildCoreContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_build_passthrough_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/build/BuildPassthroughContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_build_execution_paths_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/build/BuildExecutionPathsContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_install_cmake_compatibility_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/install/InstallCMakeCompatibilityContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_install_reconciliation_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/install/InstallReconciliationContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_vix_app_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunVixAppContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_execution_paths_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunExecutionPathsContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_compiled_dependency_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunCompiledDependencyContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_dev_option_coverage
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/dev/DevOptionCoverageTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_dev_project_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/dev/DevProjectContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_dev_single_cpp_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/dev/DevSingleCppContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_dev_single_cpp_signal_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/dev/DevSingleCppSignalContractTest.sh ${CMAKE_BINARY_DIR}/vix)
set_tests_properties(vix_cli_run_vix_app_contract vix_cli_run_cache_contract vix_cli_build_passthrough_contract PROPERTIES LABELS "contract;known-regression")
