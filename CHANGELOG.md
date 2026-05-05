# Changelog - Vix CLI

## Unreleased / dev

### Fixed
- Synchronized the global `vix help` output with all commands registered in the dispatcher.
- Fixed missing commands in the help screen, including:
  - `search`
  - `publish`
  - `unpublish`
  - `modules`
  - `completion`
  - `up`
  - `i`
  - `deps`
  - `test`
  - `make:<type>`
- Updated CLI help links to use the official documentation domain:
  - `https://docs.vixcpp.com`
  - `https://docs.vixcpp.com/cli/`
  - `https://registry.vixcpp.com`
- Improved command grouping in the global help output.
- Added clearer usage examples for common workflows:
  - `vix run main.cpp`
  - `vix build main.cpp --out app`
  - `vix make:class User`
  - `vix add @cnerium/app`
  - `vix install`

### Added
- Added replay support for recorded single-file script executions.
- Added support for recording and replaying single-file script runs.
- Added a dedicated `replay` command.
- Added support for single-file binary export through `vix build`.
- Added fast direct compile path for Vix scripts.
- Added smart CMake fallback for scripts requiring Vix runtime, DB, ORM, or compiled dependencies.
- Added improved build metadata storage under the Vix home directory.
- Added better cache fingerprinting for direct script runs.
- Added support for SQLite/MySQL and local-cache behavior in script mode.
- Added structured compiler and runtime diagnostics, including template, container, coroutine, polymorphism, sanitizer, and CMake error detection.
- Added command suggestions using dispatcher entries.
- Added `fmt`, `task`, `info`, `completion`, `outdated`, `update`, `upgrade`, `doctor`, `uninstall`, `modules`, `make`, `p2p`, `pack`, `verify`, `cache`, `publish`, and `unpublish` commands across the CLI evolution.

### Changed
- Improved `vix run` behavior for single-file C++ scripts.
- Improved `vix build` output handling for single-file builds.
- Improved direct script cache behavior and stale-cache detection.
- Improved forwarding of runtime arguments after the target in `vix run`.
- Improved handling of `--run` and `--` separators in script mode.
- Improved passthrough runtime behavior for interactive prompts and piped stdin.
- Improved Ctrl+C behavior by treating script SIGINT as a normal shutdown.
- Improved runtime output rendering by reducing noise from aborts, duplicate logs, CMake internals, Ninja internals, and spinners.
- Improved `vix new` internals and output rendering.
- Refactored `new` and `modules` command internals.
- Refactored build/run script execution flow around direct compile, CMake fallback, cache signatures, and runtime process handling.
- Replaced legacy `deps` workflow with `install`, while keeping `deps` as a deprecated alias.
- Improved registry dependency workflow with deterministic installation, lockfile support, transitive dependency resolution, semver resolution, scoped package names, global package support, and outdated checks.
- Improved generated project scaffolding, CMake templates, README files, `vix.json`, tasks, and optional module layout.
- Improved shell UX with cleaner output, better color helpers, unified UI rendering, and more readable tips.
- Improved Windows/MSVC compatibility by fixing path handling, unused warnings, missing includes, link issues, and platform-specific process behavior.

### Fixed
- Fixed script runtime detection for direct compile vs CMake fallback.
- Fixed CMake fallback for scripts using Vix runtime.
- Fixed CMake fallback for DB and ORM scripts.
- Fixed header-only, compiled, and header-plus-source dependency handling.
- Fixed auto-dependency filtering for direct script runs.
- Fixed stale cache issues by including CMake arguments in config signatures.
- Fixed executable target resolution from build directories.
- Fixed runtime argument forwarding in `vix run`.
- Fixed interactive stdin and prompt preservation in script runner.
- Fixed sanitizer output stability and diagnostics.
- Fixed misleading CMake error detection.
- Fixed project root detection and preset mapping.
- Fixed `vix tests` behavior by auto-configuring/building when needed.
- Fixed `vix publish` validation, duplicate version rejection, idempotency, and registry metadata generation.
- Fixed `vix unpublish` registration and Windows build compatibility.
- Fixed numerous compiler warnings across the CLI module.
- Fixed Windows, macOS, Clang, GCC, and MSVC portability issues.

## Major feature history

### Project workflow
- `vix new`
- `vix make`
- `vix make:<type>`
- `vix run`
- `vix dev`
- `vix build`
- `vix check`
- `vix tests`
- `vix fmt`
- `vix clean`
- `vix reset`
- `vix task`
- `vix replay`
- `vix repl`

### Dependency and registry workflow
- `vix registry`
- `vix add`
- `vix search`
- `vix install`
- `vix i`
- `vix deps`
- `vix update`
- `vix up`
- `vix outdated`
- `vix remove`
- `vix list`
- `vix store`
- `vix publish`
- `vix unpublish`

### Packaging
- `vix pack`
- `vix verify`
- `vix cache`

### Runtime and advanced tooling
- `vix p2p`
- `vix orm`
- `vix modules`

### System commands
- `vix info`
- `vix doctor`
- `vix upgrade`
- `vix uninstall`
- `vix completion`

## Summary

The Vix CLI evolved from a simple project runner into a complete C++ runtime command-line tool. It now supports project scaffolding, direct single-file execution, smart CMake fallback, binary export, dependency resolution, registry publishing, package verification, module management, P2P tooling, ORM migrations, task automation, shell completion, system diagnostics, and a cleaner developer experience across Linux, macOS, and Windows.
