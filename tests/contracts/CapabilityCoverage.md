# Capability coverage manifest

This manifest is deliberately separate from `OptionCoverage.md`: an option can
parse correctly while a supported execution path is broken. `PASS` means an
executable contract exists and is green; `FAIL` means its contract is red;
`UNAVAILABLE` names an environmental prerequisite; `UNCOVERED` is a required
future contract, never a pass.

| Command | Capability | Variant / source of truth | Test | Status |
|---|---|---|---|---|
| run | execution-path | `target.binary` / `RunTargetKind::Binary` | RunExecutionPathsContractTest | PASS |
| run | execution-path | `target.script` / `RunTargetKind::Script` | RunCoreContractTest | PASS |
| run | execution-path | `target.project` / `RunTargetKind::Project` | RunExecutionPathsContractTest | PASS |
| run | execution-path | `target.container` / `RunTargetKind::Container` | controlled runtime mock | UNCOVERED |
| run | execution-path | `strategy.direct` / `ScriptExecutionStrategy::Direct` | RunCoreContractTest | PASS |
| run | execution-path | `strategy.cmake-fallback` / `ScriptExecutionStrategy::CMakeFallback` | RunCompiledDependencyContractTest | PASS |
| run | execution-path | `manifest.vix` / `manifestMode` | RunExecutionPathsContractTest | PASS |
| run | project-format | `single-cpp` / `.cpp,.cc,.cxx` | RunCoreContractTest | PASS |
| run | project-format | `cmake-project` / `CMakeLists.txt` | RunExecutionPathsContractTest | PASS |
| run | project-format | `vix-app` / `vix.app` | project contract | UNCOVERED |
| run | project-format | `vix-manifest` / `.vix` | RunExecutionPathsContractTest | PASS |
| run | project-format | `executable` / executable file | RunExecutionPathsContractTest | PASS |
| run | project-format | `uri` / docker,container,ssh,http,https | RunExecutionPathsContractTest (local HTTP) | PASS |
| run | dependency | `none` | RunCoreContractTest | PASS |
| run | dependency | `local-header` | RunCacheContractTest | PASS |
| run | dependency | `transitive-local-header` | RunCacheContractTest | PASS |
| run | dependency | `header-only-managed` / `headerOnlyDepIncludeDirs` | dependency contract | UNCOVERED |
| run | dependency | `compiled` / `compiledDepPaths` | RunCompiledDependencyContractTest | PASS |
| run | dependency | `temporary-git` / `--dep` | RunScriptDependencyPathTest | PASS |
| run | dependency | `auto-deps-local` / `AutoDepsMode::Local` | auto-deps contract | UNCOVERED |
| run | dependency | `auto-deps-up` / `AutoDepsMode::Up` | auto-deps contract | UNCOVERED |
| run | linkage | `none` | RunCoreContractTest | PASS |
| run | linkage | `missing-symbol` | RunCoreContractTest | PASS |
| run | linkage | `external-library` / script link flags | RunCompiledDependencyContractTest | PASS |
| run | linkage | `vix-compiled-module` / `usesVixRuntime` | RunVixAppContractTest | PASS |
| run | cache | `cold,warm,source-touch,source-content` | RunCoreContractTest | PASS |
| run | cache | `local-header-content` | RunCacheContractTest | PASS |
| run | cache | `transitive-header-content` | RunCacheContractTest | PASS |
| run | cache | `compiler-or-flags-changed` | RunSingleCppCacheCliTest | PASS |
| run | cache | `previous-failure` | RunSingleCppCacheCliTest | PASS |
| run | runtime | `args,env,cwd,nonzero` | RunCoreContractTest | PASS |
| run | runtime | `signal` | runtime signal contract | UNCOVERED |
| run | runtime | `exception` | runtime exception contract | UNCOVERED |
| build | execution-path | `single-cpp` / `Options::singleCpp` | BuildExecutionPathsContractTest | PASS |
| build | execution-path | `cmake-ninja` / `BuildCommand::run` | BuildCoreContractTest | PASS |
| build | execution-path | `target-graph-executor` / `can_use_target_graph_executor` | BuildExecutionPathsContractTest | PASS |
| build | execution-path | `watch-cmake` | BuildWatchCliTest | PASS |
| build | execution-path | `watch-graph-executor` | graph watch contract | UNCOVERED |
| build | execution-path | `log-reader` / `showLog` | BuildCoreContractTest | PASS |
| build | project-format | `cmake-project` | BuildCoreContractTest | PASS |
| build | project-format | `vix-app` | app project contract | UNCOVERED |
| build | project-format | `single-cpp` | BuildExecutionPathsContractTest | PASS |
| build | dependency | `native-cmake-package` | BuildNativeCMakeDiscoveryTest | PASS |
| build | dependency | `local-vix-modules` | SdkProfileCompositionTest | PASS |
| build | dependency | `managed-sdk-composed` | SdkProfileCompositionTest | PASS |
| build | dependency | `managed-sdk-missing` | SdkProfileCompositionTest | PASS |
| build | linkage | `cmake-interface-target` | BuildNativeCMakeDiscoveryTest | PASS |
| build | linkage | `compiled-vix-target` | CMake package linkage contract | UNCOVERED |
| build | linkage | `transitive-system-library` | CMake package linkage contract | UNCOVERED |
| build | toolchain | `native` | BuildCoreContractTest | PASS |
| build | toolchain | `discovered-cross` | BuildToolCliCompatTest | PASS |
| build | toolchain | `cross-sysroot-propagation` | fake toolchain contract | UNCOVERED |
| build | output | `normal,verbose,quiet` | BuildProgressCliTest | PASS |
| build | output | `debug,debug-log,cmake-verbose` | BuildToolCliCompatTest | PASS |
| build | output | `report` | cloud service contract | UNAVAILABLE |
| sdk | profile | `default` / `profile_names()` | SDK profile contract | UNCOVERED |
| sdk | profile | `web` / `profile_names()` | SdkProfileCompositionTest | PASS |
| sdk | profile | `data` / `profile_names()` | SdkProfileCompositionTest | PASS |
| sdk | profile | `desktop` / `profile_names()` | SDK profile contract | UNCOVERED |
| sdk | profile | `p2p` / `profile_names()` | SDK profile contract | UNCOVERED |
| sdk | profile | `game` / `profile_names()` | SDK profile contract | UNCOVERED |
| sdk | profile | `agent` / `profile_names()` | SDK profile contract | UNCOVERED |
| sdk | profile | `all` / `profile_names()` | SDK profile contract | UNCOVERED |
| sdk | cmake-package | `VixConfig.cmake` | SdkProfileCompositionTest | PASS |
| sdk | cmake-package | `vix::vix umbrella target` | RunVixAppContractTest | PASS |
| sdk | cmake-package | `module public target` | module linkage contract | UNCOVERED |
