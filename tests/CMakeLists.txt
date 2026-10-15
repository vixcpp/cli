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

add_executable(vix_cli_code_frame_style_tests
  CodeFrameStyleTests.cpp
  ../src/errors/CodeFrame.cpp
)
target_include_directories(vix_cli_code_frame_style_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_code_frame_style_tests COMMAND vix_cli_code_frame_style_tests)

add_executable(vix_cli_style_windows_macro_compile_test StyleWindowsMacroCompileTest.cpp)
target_include_directories(vix_cli_style_windows_macro_compile_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_style_windows_macro_compile_test
  COMMAND vix_cli_style_windows_macro_compile_test
)

add_executable(vix_cli_clang_gcc_parser_tests
  ClangGccParserTests.cpp
  ../src/errors/ClangGccParser.cpp
)
target_include_directories(vix_cli_clang_gcc_parser_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_clang_gcc_parser_tests COMMAND vix_cli_clang_gcc_parser_tests)

add_executable(vix_cli_compiler_diagnostic_presentation_tests
  CompilerDiagnosticPresentationTests.cpp
)
target_include_directories(vix_cli_compiler_diagnostic_presentation_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(
  NAME vix_cli_compiler_diagnostic_presentation_tests
  COMMAND vix_cli_compiler_diagnostic_presentation_tests
)

add_executable(vix_cli_git_progress_parser_tests GitProgressParserTests.cpp)
target_include_directories(vix_cli_git_progress_parser_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME vix_cli_git_progress_parser_tests COMMAND vix_cli_git_progress_parser_tests)

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

add_executable(vix_cli_project_mutation_tests ProjectMutationTests.cpp ../src/util/ProjectMutation.cpp)
target_include_directories(vix_cli_project_mutation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME vix_cli_project_mutation_tests COMMAND vix_cli_project_mutation_tests)

add_executable(vix_cli_project_discovery_tests
  ProjectDiscoveryTests.cpp
  ../src/project/ProjectDiscovery.cpp
)
target_include_directories(vix_cli_project_discovery_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_project_discovery_tests COMMAND vix_cli_project_discovery_tests)

add_executable(vix_cli_native_new_project_tests
  NativeNewProjectTests.cpp
  ../src/commands/NewCommand.cpp
  ../src/commands/InitCommand.cpp
  ../src/commands/new/NewGenerator.cpp
  ../src/project/ProjectDiscovery.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_new_project_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_new_project_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_new_project_tests COMMAND vix_cli_native_new_project_tests)

add_executable(vix_cli_target_selection_tests
  TargetSelectionTests.cpp
  ../src/project/TargetSelection.cpp
)
target_include_directories(vix_cli_target_selection_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_target_selection_tests COMMAND vix_cli_target_selection_tests)

add_executable(vix_cli_cmake_file_api_tests
  CMakeFileApiTests.cpp
  ../src/project/cmake/CMakeFileApi.cpp
  ../src/project/cmake/CTestDiscovery.cpp
  ../src/project/cmake/CMakeProjectAdapter.cpp
  ../src/project/TargetSelection.cpp
)
target_include_directories(vix_cli_cmake_file_api_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_definitions(vix_cli_cmake_file_api_tests PRIVATE
  VIX_P3_FIXTURE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/project_file_api"
  VIX_P3_CMAKE_COMMAND="${CMAKE_COMMAND}"
)
if (TARGET vix::engine)
  target_link_libraries(vix_cli_cmake_file_api_tests PRIVATE vix::engine)
endif()
add_test(NAME vix_cli_cmake_file_api_tests COMMAND vix_cli_cmake_file_api_tests)

add_executable(vix_cli_ctest_discovery_tests
  CTestDiscoveryTests.cpp
  ../src/project/cmake/CTestDiscovery.cpp
)
target_include_directories(vix_cli_ctest_discovery_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
if (TARGET vix::engine)
  target_link_libraries(vix_cli_ctest_discovery_tests PRIVATE vix::engine)
endif()
add_test(NAME vix_cli_ctest_discovery_tests COMMAND vix_cli_ctest_discovery_tests)

add_executable(vix_cli_test_selection_tests
  TestSelectionTests.cpp
  ../src/project/TestSelection.cpp
)
target_include_directories(vix_cli_test_selection_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_test_selection_tests COMMAND vix_cli_test_selection_tests)

add_executable(vix_cli_native_semantic_model_tests
  NativeSemanticModelTests.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_semantic_model_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_native_semantic_model_tests COMMAND vix_cli_native_semantic_model_tests)

add_executable(vix_cli_native_toml_parser_tests
  NativeTomlParserTests.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_toml_parser_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_toml_parser_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_toml_parser_tests COMMAND vix_cli_native_toml_parser_tests)

add_executable(vix_cli_native_canonical_hash_tests
  NativeCanonicalHashTests.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
)
target_include_directories(vix_cli_native_canonical_hash_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_canonical_hash_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_canonical_hash_tests COMMAND vix_cli_native_canonical_hash_tests)

add_executable(vix_cli_native_lockfile_tests
  NativeLockfileTests.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_lockfile_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_lockfile_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_lockfile_tests COMMAND vix_cli_native_lockfile_tests)

add_executable(vix_cli_native_project_lock_state_tests
  NativeProjectLockStateTests.cpp
  ../src/project/native/ProjectLockState.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_project_lock_state_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_project_lock_state_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_project_lock_state_tests COMMAND vix_cli_native_project_lock_state_tests)

add_executable(vix_cli_native_resolver_tests
  NativeResolverTests.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_resolver_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_resolver_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_resolver_tests COMMAND vix_cli_native_resolver_tests)

add_executable(vix_cli_native_resolution_catalog_tests
  NativeResolutionCatalogTests.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_resolution_catalog_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_resolution_catalog_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_resolution_catalog_tests COMMAND vix_cli_native_resolution_catalog_tests)

add_executable(vix_cli_native_metadata_acquisition_tests
  NativeMetadataAcquisitionTests.cpp
  ../src/project/native/NativeDependencyMutation.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_metadata_acquisition_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_metadata_acquisition_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_metadata_acquisition_tests COMMAND vix_cli_native_metadata_acquisition_tests)

add_executable(vix_cli_native_package_command_runtime_tests
  NativePackageCommandRuntimeTests.cpp
  ../src/project/native/PackageCommandRuntime.cpp
  ../src/project/native/NativeDependencyMutation.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
  ../src/project/native/RegistryMetadataSource.cpp
  ../src/project/native/RegistrySnapshotState.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/MetadataStorage.cpp
  ../src/project/native/FilesystemMetadataStorage.cpp
  ../src/project/native/GitPackageMetadataSource.cpp
  ../src/project/native/GitPackageContentSource.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/RegistryContentTransport.cpp
)
target_include_directories(vix_cli_native_package_command_runtime_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_command_runtime_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
if (TARGET vix::crypto)
  target_link_libraries(vix_cli_native_package_command_runtime_tests PRIVATE vix::crypto)
endif()
if (TARGET vix::requests)
  target_link_libraries(vix_cli_native_package_command_runtime_tests PRIVATE vix::requests)
endif()
find_package(Threads REQUIRED)
target_link_libraries(vix_cli_native_package_command_runtime_tests PRIVATE Threads::Threads)
if (TARGET vix_warnings)
  target_link_libraries(vix_cli_native_package_command_runtime_tests PRIVATE vix_warnings)
endif()
add_test(NAME vix_cli_native_package_command_runtime_tests
  COMMAND vix_cli_native_package_command_runtime_tests)

add_executable(vix_cli_native_registry_directory_tests
  NativeRegistryDirectoryTests.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_registry_directory_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_registry_directory_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_registry_directory_tests COMMAND vix_cli_native_registry_directory_tests)

add_executable(vix_cli_native_registry_authority_tests
  NativeRegistryAuthorityTests.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_registry_authority_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_registry_authority_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_registry_authority_tests COMMAND vix_cli_native_registry_authority_tests)

add_executable(vix_cli_native_registry_snapshot_state_tests
  NativeRegistrySnapshotStateTests.cpp
  ../src/project/native/RegistrySnapshotState.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_registry_snapshot_state_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_registry_snapshot_state_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_registry_snapshot_state_tests
  COMMAND vix_cli_native_registry_snapshot_state_tests)

add_executable(vix_cli_native_registry_offline_metadata_tests
  NativeRegistryOfflineMetadataTests.cpp
  ../src/project/native/MetadataStorage.cpp
  ../src/project/native/RegistrySnapshotState.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_registry_offline_metadata_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_registry_offline_metadata_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_registry_offline_metadata_tests
  COMMAND vix_cli_native_registry_offline_metadata_tests)

add_executable(vix_cli_native_registry_metadata_source_tests
  NativeRegistryMetadataSourceTests.cpp
  ../src/project/native/RegistryMetadataSource.cpp
  ../src/project/native/RegistrySnapshotState.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/MetadataStorage.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_registry_metadata_source_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_registry_metadata_source_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
if (TARGET vix::crypto)
  target_link_libraries(vix_cli_native_registry_metadata_source_tests PRIVATE vix::crypto)
endif()
if (TARGET vix::requests)
  target_link_libraries(vix_cli_native_registry_metadata_source_tests PRIVATE vix::requests)
endif()
add_test(NAME vix_cli_native_registry_metadata_source_tests
  COMMAND vix_cli_native_registry_metadata_source_tests)

add_executable(vix_cli_native_filesystem_metadata_storage_tests
  NativeFilesystemMetadataStorageTests.cpp
  ../src/project/native/FilesystemMetadataStorage.cpp
  ../src/project/native/MetadataStorage.cpp
  ../src/project/native/RegistrySnapshotState.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_filesystem_metadata_storage_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_filesystem_metadata_storage_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_filesystem_metadata_storage_tests
  COMMAND vix_cli_native_filesystem_metadata_storage_tests)

add_executable(vix_cli_native_package_content_store_tests
  NativePackageContentStoreTests.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/MetadataStorage.cpp
  ../src/project/native/RegistrySnapshotState.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_package_content_store_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_content_store_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_package_content_store_tests
  COMMAND vix_cli_native_package_content_store_tests)

add_executable(vix_cli_native_package_content_digest_tests
  NativePackageContentDigestTests.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_package_content_digest_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_content_digest_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_package_content_digest_tests
  COMMAND vix_cli_native_package_content_digest_tests)

add_executable(vix_cli_native_filesystem_package_content_store_tests
  NativeFilesystemPackageContentStoreTests.cpp
  ../src/project/native/FilesystemPackageContentStore.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_filesystem_package_content_store_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_filesystem_package_content_store_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_filesystem_package_content_store_tests
  COMMAND vix_cli_native_filesystem_package_content_store_tests)

add_executable(vix_cli_native_package_materialization_tests
  NativePackageMaterializationTests.cpp
  ../src/project/native/PackageMaterialization.cpp
  ../src/project/native/FilesystemPackageContentStore.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_package_materialization_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_materialization_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_package_materialization_tests
  COMMAND vix_cli_native_package_materialization_tests)

add_executable(vix_cli_native_package_install_tests
  NativePackageInstallTests.cpp
  ../src/project/native/NativePackageInstall.cpp
  ../src/project/native/PackageContentAcquisition.cpp
  ../src/project/native/PackageMaterialization.cpp
  ../src/project/native/FilesystemPackageContentStore.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/ProjectLockState.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_package_install_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_install_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
add_test(NAME vix_cli_native_package_install_tests
  COMMAND vix_cli_native_package_install_tests)

add_executable(vix_cli_native_package_build_graph_tests
  NativePackageBuildGraphTests.cpp
  ../src/project/native/PackageBuildGraph.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/ProjectLockState.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_package_build_graph_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_build_graph_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
target_compile_definitions(vix_cli_native_package_build_graph_tests PRIVATE
  VIX_PACKAGE_BUILD_CMAKE_COMMAND="${CMAKE_COMMAND}"
)
add_test(NAME vix_cli_native_package_build_graph_tests
  COMMAND vix_cli_native_package_build_graph_tests)

add_executable(vix_cli_native_package_content_acquisition_tests
  NativePackageContentAcquisitionTests.cpp
  ../src/project/native/PackageContentAcquisition.cpp
  ../src/project/native/RegistryContentTransport.cpp
  ../src/project/native/FilesystemPackageContentStore.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_package_content_acquisition_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_content_acquisition_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
if (TARGET vix::requests)
  target_link_libraries(vix_cli_native_package_content_acquisition_tests PRIVATE vix::requests)
endif()
add_test(NAME vix_cli_native_package_content_acquisition_tests
  COMMAND vix_cli_native_package_content_acquisition_tests)

add_executable(vix_cli_native_development_runtime_tests
  NativeDevelopmentRuntimeTests.cpp
  ../src/project/ProjectDiscovery.cpp
  ../src/project/TargetSelection.cpp
  ../src/project/TestSelection.cpp
  ../src/project/cmake/CMakeFileApi.cpp
  ../src/project/cmake/CMakeProjectAdapter.cpp
  ../src/project/cmake/CTestDiscovery.cpp
  ../src/project/cmake/CTestExecution.cpp
  ../src/project/native/NativeDevelopmentRuntime.cpp
  ../src/project/native/NativePackageInstallCommand.cpp
  ../src/project/native/NativePackageInstall.cpp
  ../src/project/native/PackageCommandRuntime.cpp
  ../src/project/native/NativeDependencyMutation.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/Resolver.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
  ../src/project/native/RegistryMetadataSource.cpp
  ../src/project/native/RegistrySnapshotState.cpp
  ../src/project/native/RegistryAuthority.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/MetadataStorage.cpp
  ../src/project/native/FilesystemMetadataStorage.cpp
  ../src/project/native/GitPackageMetadataSource.cpp
  ../src/project/native/GitPackageContentSource.cpp
  ../src/project/native/PackageContentAcquisition.cpp
  ../src/project/native/RegistryContentTransport.cpp
  ../src/project/native/PackageMaterialization.cpp
  ../src/project/native/FilesystemPackageContentStore.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/ProjectLockState.cpp
  ../src/project/native/PackageBuildGraph.cpp
)
target_include_directories(vix_cli_native_development_runtime_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_development_runtime_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
if (TARGET vix::engine)
  target_link_libraries(vix_cli_native_development_runtime_tests PRIVATE vix::engine)
endif()
if (TARGET vix::process)
  target_link_libraries(vix_cli_native_development_runtime_tests PRIVATE vix::process)
endif()
if (TARGET vix::json)
  target_link_libraries(vix_cli_native_development_runtime_tests PRIVATE vix::json)
endif()
if (TARGET vix::crypto)
  target_link_libraries(vix_cli_native_development_runtime_tests PRIVATE vix::crypto)
endif()
if (TARGET vix::requests)
  target_link_libraries(vix_cli_native_development_runtime_tests PRIVATE vix::requests)
endif()
if (TARGET vix_warnings)
  target_link_libraries(vix_cli_native_development_runtime_tests PRIVATE vix_warnings)
endif()
find_package(Threads REQUIRED)
target_link_libraries(vix_cli_native_development_runtime_tests PRIVATE Threads::Threads)
add_test(NAME vix_cli_native_development_runtime_tests
  COMMAND vix_cli_native_development_runtime_tests)

add_executable(vix_cli_native_git_package_content_source_tests
  NativeGitPackageContentSourceTests.cpp
  ../src/project/native/PackageContentAcquisition.cpp
  ../src/project/native/FilesystemPackageContentStore.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/GitPackageContentSource.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/RegistryDirectory.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_git_package_content_source_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_git_package_content_source_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
find_package(Threads REQUIRED)
target_link_libraries(vix_cli_native_git_package_content_source_tests PRIVATE Threads::Threads)
add_test(NAME vix_cli_native_git_package_content_source_tests
  COMMAND vix_cli_native_git_package_content_source_tests)

add_executable(vix_cli_native_git_package_metadata_source_tests
  NativeGitPackageMetadataSourceTests.cpp
  ../src/project/native/GitPackageMetadataSource.cpp
  ../src/project/native/GitPackageContentSource.cpp
  ../src/project/native/MetadataAcquisition.cpp
  ../src/project/native/ResolutionCatalog.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_git_package_metadata_source_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_git_package_metadata_source_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
target_link_libraries(vix_cli_native_git_package_metadata_source_tests PRIVATE Threads::Threads)
add_test(NAME vix_cli_native_git_package_metadata_source_tests
  COMMAND vix_cli_native_git_package_metadata_source_tests)

add_executable(vix_cli_ctest_execution_tests
  CTestExecutionTests.cpp
  ../src/project/cmake/CTestExecution.cpp
)
target_include_directories(vix_cli_ctest_execution_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_ctest_execution_tests COMMAND vix_cli_ctest_execution_tests)

add_test(
  NAME vix_cli_tests_target_migration_tests
  COMMAND ${CMAKE_COMMAND}
    -DVIX_CLI_EXECUTABLE=$<TARGET_FILE:vix_cli>
    -DVIX_P7_FIXTURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/project_ctest_execution
    -DVIX_P7_FAILURE_FIXTURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/project_ctest_failure
    -P ${CMAKE_CURRENT_SOURCE_DIR}/TestsTargetMigrationTest.cmake
)

add_executable(vix_cli_dev_command_forwarding_tests
  DevCommandForwardingTests.cpp
  ../src/commands/DevCommand.cpp
)
target_include_directories(vix_cli_dev_command_forwarding_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
add_test(NAME vix_cli_dev_command_forwarding_tests COMMAND vix_cli_dev_command_forwarding_tests)

add_test(
  NAME vix_cli_build_target_migration_tests
  COMMAND ${CMAKE_COMMAND}
    -DVIX_CLI_EXECUTABLE=$<TARGET_FILE:vix_cli>
    -DVIX_P5_FIXTURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/project_file_api
    -P ${CMAKE_CURRENT_SOURCE_DIR}/BuildTargetMigrationTest.cmake
)

add_test(
  NAME vix_cli_run_target_migration_tests
  COMMAND ${CMAKE_COMMAND}
    -DVIX_CLI_EXECUTABLE=$<TARGET_FILE:vix_cli>
    -DVIX_P6_FIXTURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/project_file_api
    -P ${CMAKE_CURRENT_SOURCE_DIR}/RunTargetMigrationTest.cmake
)

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

if (TARGET vix::env)
  target_link_libraries(vix_cli_package_hash_tests PRIVATE vix::env)
elseif (TARGET vix_env)
  target_link_libraries(vix_cli_package_hash_tests PRIVATE vix_env)
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
  NAME vix_cli_mutation_command_contracts
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/MutationCommandContractsTest.sh
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

add_executable(vix_cli_native_package_publication_tests
  NativePackagePublicationTests.cpp
  ../src/project/native/PackagePublication.cpp
  ../src/project/native/PackageContentDigest.cpp
  ../src/project/native/PackageContentStore.cpp
  ../src/project/native/Lockfile.cpp
  ../src/project/native/CanonicalHash.cpp
  ../src/project/native/TomlParser.cpp
  ../src/project/native/Validation.cpp
)
target_include_directories(vix_cli_native_package_publication_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_include_directories(vix_cli_native_package_publication_tests SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tomlplusplus
)
if (TARGET vix::requests)
  target_link_libraries(vix_cli_native_package_publication_tests PRIVATE vix::requests)
endif()
if (TARGET vix::json)
  target_link_libraries(vix_cli_native_package_publication_tests PRIVATE vix::json)
endif()
if (TARGET vix_warnings)
  target_link_libraries(vix_cli_native_package_publication_tests PRIVATE vix_warnings)
endif()
add_test(NAME vix_cli_native_package_publication_tests
  COMMAND vix_cli_native_package_publication_tests)

add_test(
  NAME vix_cli_registry_integrity
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/RegistryIntegrityTest.sh
    ${CMAKE_BINARY_DIR}/vix
)

add_test(
  NAME vix_cli_registry_dependency_lifecycle
  COMMAND
    bash
    ${CMAKE_CURRENT_SOURCE_DIR}/RegistryDependencyLifecycleTest.sh
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

add_test(
  NAME vix_cli_cmake_vix_dependency_manifest
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/CMakeVixDependencyManifestTest.sh ${CMAKE_BINARY_DIR}/vix
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
add_test(NAME vix_cli_build_ninja_incrementality_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/build/BuildNinjaIncrementalityContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_install_reconciliation_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/install/InstallReconciliationContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_vix_app_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunVixAppContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_execution_paths_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunExecutionPathsContractTest.sh ${CMAKE_BINARY_DIR}/vix)
add_test(NAME vix_cli_run_compiled_dependency_contract
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/contracts/run/RunCompiledDependencyContractTest.sh ${CMAKE_BINARY_DIR}/vix)
set_tests_properties(vix_cli_run_vix_app_contract vix_cli_run_cache_contract vix_cli_build_passthrough_contract PROPERTIES LABELS "contract;known-regression")
