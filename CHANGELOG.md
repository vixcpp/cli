# Changelog - Vix CLI

All notable changes to `@vix/cli` are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Added

- Added project-aware note command support.

### Fixed

- Enabled note server request logs for CLI-driven note workflows.

## [1.75.0] - 2026-06-23

### Added

- Added the `vix note` command.

## [1.74.0] - 2026-06-08

### Changed

- Improved dependency, build, install, registry, and publishing diagnostics.
- Improved compiler context preservation in user diagnostics.
- Improved test failure, CTest, assertion, and GoogleTest diagnostic rendering.
- Improved runtime failure diagnostics, including resource failures and bad variant access.
- Improved generated backend/web templates, production config templates, and generated manifest guidance.
- Clarified direct and locked dependency views.

### Fixed

- Resolved runnable executables more generically for `vix run` and CMake output directories.
- Kept default execution output clean.
- Used absolute system archiver paths during builds.
- Removed default blank lines before code frames.
- Preferred user code locations over system headers in diagnostics.
- Detected broken build tool runtime libraries.
- Prepared registry indexes and cleaned stale registry entries before publishing.
- Made command capture portable on Windows.

## [1.73.0] - 2026-05-22

### Added

- Added game export command support.

## [1.72.0] - 2026-05-21

### Added

- Added the game project template.

### Fixed

- Kept generated game templates running correctly.
- Improved CMake build error diagnostics and export dependency detection.

## [1.71.0] - 2026-05-21

### Added

- Added Vue template support.
- Added full-stack development workflow support.

### Fixed

- Configured standalone dependencies in a stable order.
- Refined package listing and package info output.
- Simplified global package workflow behavior.

## [1.70.0] - 2026-05-19

### Added

- Added `vix.app` scaffold and validation checks.
- Expanded `vix.app` manifest handling and generated CMake project support.

### Fixed

- Auto-linked installed registry dependencies in script/project runs.
- Preserved manifest metadata when updating dependencies.
- Separated user project paths from CMake source paths.

## [1.69.0] - 2026-05-18

### Changed

- Moved the REPL engine to the dedicated reply module.
- Linked the standalone reply module from CMake.

## [1.68.0] - 2026-05-18

### Added

- Added warning check support.
- Added paginated compiler warning output.
- Added humanized compiler warning rendering.

### Fixed

- Handled Unicode warning quotes safely.

### Documentation

- Completed command help output coverage.

## [1.67.1] - 2026-05-18

### Changed

- Made build, run, dev, and test workflows more generic.
- Made graph executor behavior safer for production use.

## [1.67.0] - 2026-05-18

### Added

- Strengthened graph execution and cache behavior.

### Changed

- Improved `vix build --explain` safety and graph fallback behavior.

## [1.66.0] - 2026-05-17

### Changed

- Made the graph target executor the default build path.

## [1.65.0] - 2026-05-17

### Fixed

- Used the user project directory for project watch mode.
- Resolved dev session executables more robustly.
- Reconfigured dev sessions on config changes.

### Added

- Added dev session watcher support.
- Routed dev mode through the project watcher.

## [1.64.1] - 2026-05-16

### Added

- Added agent command help.
- Added timeout option parsing for the agent command.

## [1.64.0] - 2026-05-16

### Added

- Added the AI agent command to the CLI.

## [1.63.0] - 2026-05-13

### Changed

- Polished `vix modules` command output.
- Improved new project library workflow.

## [1.62.0] - 2026-05-08

### Changed

- Styled `vix check` output consistently.
- Improved virtual compiler diagnostic handling.

## [1.61.0] - 2026-05-08

### Changed

- Improved `vix tests` output and filtering.
- Made replay and docs behavior opt-in for `vix run`.
- Improved concept constraint diagnostics.

### Fixed

- Used portable process execution for the tests command.

## [1.60.4] - 2026-05-08

### Changed

- Improved dev mode rebuild flow.
- Guarded experimental graph build paths.

## [1.60.3] - 2026-05-07

### Changed

- Improved build output responsiveness.
- Reduced verbose build output noise.

## [1.60.2] - 2026-05-07

### Changed

- Polished build progress and diagnostics.
- Unified build output styling.
- Built the default project target instead of always building `all`.

## [1.60.1] - 2026-05-07

### Added

- Imported Ninja build edges into the graph executor.
- Added guarded target-aware graph execution.

## [1.60.0] - 2026-05-07

### Added

- Imported compile commands into the build graph.

## [1.59.0] - 2026-05-06

### Added

- Added incremental build graph support.

## [1.58.0] - 2026-05-06

### Added

- Added sanitizer run support.

### Changed

- Improved compiler and runtime error diagnostics.

## [1.57.0] - 2026-05-05

### Added

- Added recording and replay support for single-file script runs.

## [1.56.4] - 2026-04-27

### Fixed

- Correctly forwarded runtime arguments after the target in `vix run`.

## [1.56.3] - 2026-04-27

### Changed

- Removed parser and direct script runner debug logs from production output.

## [1.56.2] - 2026-04-27

### Documentation

- Added single-file build examples to CLI help.

## [1.56.1] - 2026-04-27

### Added

- Added single-file binary export to `vix build`.

## [1.56.0] - 2026-04-27

### Added

- Added binary export support to `vix build`.

## [1.55.x] - 2026-04-23 to 2026-04-27

### Added

- Added fast direct compile path for Vix scripts.
- Added modular template error rules.
- Added template, container, coroutine, and polymorphism diagnostics.

### Fixed

- Preserved interactive prompt output in passthrough mode.
- Forwarded stdin for interactive scripts.
- Preserved piped stdin in the direct script runner.
- Suppressed runtime abort noise in the script runner.
- Forced CMake fallback for DB, ORM, and runtime-dependent scripts.
- Restored friendly compile errors in the direct script runner.
- Generalized Vix include detection in script probing.

## [1.54.x] - 2026-04-17 to 2026-04-27

### Added

- Added DB flags for script and build workflows.

### Changed

- Unified `new` and registry output styling.
- Improved `new` command behavior.
- Removed `Config::getInstance` usage from CLI templates/config flows.

### Fixed

- Forced CMake fallback for scripts using the Vix runtime.
- Preserved sanitizer developer experience.
- Invalidated stale script caches.
- Included CMake arguments in config signatures to avoid stale cache issues.
- Prepared script projects correctly in dev mode.

## [1.50.0 - 1.53.0] - 2026-04-10 to 2026-04-17

### Added

- Added SQLite/MySQL flags and clean support for run/build workflows.
- Added `--local-cache` for script mode.
- Added unified cache behavior for `run` and `check`.

### Changed

- Reorganized run flow, script pipeline, and process execution.

### Fixed

- Resolved executable targets from build directories instead of assuming project names.
- Detected Vix runtime scripts correctly in CMake fallback.
- Fixed script cache signatures.

## Earlier History

### Project Workflow

- Added and evolved `vix new`, `vix make`, `vix make:<type>`, `vix run`, `vix dev`, `vix build`, `vix check`, `vix tests`, `vix fmt`, `vix clean`, `vix reset`, `vix task`, `vix replay`, and `vix repl`.

### Dependency And Registry Workflow

- Added registry package management through `vix registry`, `vix add`, `vix search`, `vix install`, `vix i`, `vix deps`, `vix update`, `vix up`, `vix outdated`, `vix remove`, `vix list`, `vix store`, `vix publish`, and `vix unpublish`.
- Added deterministic dependency installation, lockfile-aware dependency views, scoped package names, transitive dependency resolution, and semver-aware registry behavior.

### Packaging And Verification

- Added `vix pack`, `vix verify`, and `vix cache` workflows.
- Improved package validation, duplicate version handling, registry metadata generation, and publish/unpublish portability.

### Production And Operations

- Added production diagnostics, service management, nginx proxy management, deploy workflows, health checks, log analysis, websocket diagnostics, DB migration/backup commands, and environment diagnostics.

### Diagnostics And Developer Experience

- Added structured compiler diagnostics for templates, containers, coroutines, polymorphism, sanitizers, CMake errors, runtime failures, test failures, and production logs.
- Improved shell UX with cleaner output, better color helpers, unified rendering, and readable tips.
- Improved Windows, macOS, GCC, Clang, and MSVC portability across the CLI.

## Summary

The Vix CLI evolved from a project runner into a full C++ runtime command-line tool. It now supports project scaffolding, direct single-file execution, smart CMake fallback, binary export, dependency resolution, registry publishing, package verification, module management, P2P and websocket tooling, ORM/database workflows, production deployment checks, task automation, shell completion, diagnostics, and a cleaner cross-platform developer experience.
