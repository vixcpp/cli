# Current CLI capability coverage

These rows describe supported current-Vix execution surfaces. `REGISTERED`
means a test is wired into CTest, not that it passed during static review.
Historical script/container/URI execution and managed SDK profiles are not
current CLI capabilities.

| Capability | Current contract | Test | Status |
| --- | --- | --- | --- |
| `standalone-cpp` | Explicit `.cpp` input to build/run | `vix_cli_run_core_contract` | REGISTERED |
| `external-cmake` | ProjectDiscovery + File API target artifact | `vix_cli_external_cmake_build_state` | REGISTERED |
| `native-project` | `vix.toml` authority and context-local build | `vix_cli_native_run_fast_path` | REGISTERED |
| `target-artifact` | Run executes discovered artifact, not guessed path | `vix_cli_build_execution_paths_contract` | REGISTERED |
| `runtime-arguments` | `--` tail is forwarded to program | `vix_cli_run_core_contract` | REGISTERED |
| `test-filter` | Case-sensitive literal substring | `vix_cli_tests_command_contract` | REGISTERED |
| `ctest-escape-hatch` | Terminal external-CMake forwarding | `vix_cli_tests_command_contract` | REGISTERED |
| `offline-install` | Exact locked local content only | `vix_cli_install_reconciliation_contract` | REGISTERED |
| `native-dev-lifecycle` | Watch/rebuild/restart on supported POSIX hosts | `vix_cli_native_dev_lifecycle` | REGISTERED |
| `distribution-upgrade` | UpgradeRuntime boundary | `vix_cli_distribution_runtime_tests` | REGISTERED |
