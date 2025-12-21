# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)  
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]
## [1.13.2] - 2025-12-22

### Added
- 

### Changed
- 

### Removed
- 

## [1.13.1] - 2025-12-21

### Added
- 

### Changed
- 

### Removed
- 

## [1.13.0] - 2025-12-21

### Added
- 

### Changed
- 

### Removed
- 

## [1.12.0] - 2025-12-20

### Added
- 

### Changed
- 

### Removed
- 


## v1.12.0 — 2025-12-20

### Added

- **`vix tests [path]`**: new command to run project tests easily.
  - Acts as an alias of **`vix check --tests`** for a faster, more discoverable workflow.
  - Supports both project validation and test execution depending on the detected target.

### Changed

- CLI global help output: improved readability by adding consistent **left padding / indentation** across sections (Usage, Commands, Options, Examples, Links).
- `vix check` internals: updates to support the new `tests` entrypoint and shared flows.

### Notes

- This release focuses on improving the **testing UX** and making the CLI help output feel more modern and readable.

## [1.11.0] - 2025-12-20

### Added

-

### Changed

-

### Removed

-

## [1.10.0] - 2025-12-20

### Added

-

### Changed

-

### Removed

-

feat(cli/run): add --san and --ubsan flags for script mode with clean sanitizer reports

- Add --san (ASan+UBSan) and --ubsan (UBSan only) flags to `vix run`
- Extend RunCommand Options to track sanitizer mode explicitly
- Enable sanitizer-aware CMake generation for single-file .cpp scripts
- Apply sanitizer runtime environment reliably at execution time
- Improve runtime error detection and messaging (alloc/dealloc mismatch, UBSan)
- Update `vix run -h` to document new sanitizer options

This makes `vix run main.cpp --san/--ubsan` a fast, ergonomic way
to debug memory errors and undefined behavior in standalone scripts.

### Added

-

### Changed

-

### Removed

-

## [1.9.2] - 2025-12-19

### Added

-

### Changed

-

### Removed

-

## v1.9.2 — CLI Packaging & Signing UX Fix

### Fixed

- Fixed `vix pack` blocking when signing was auto-detected and minisign required a passphrase.
- Ensured `--sign=auto` never blocks execution (non-interactive signing).

### Added

- Added explicit signing modes: `--sign=auto | never | required` (npm-style behavior).
- Clear user-facing messages when signing is required (tool, key, file, prompt).

### Improved

- Better CLI UX around package signing and minisign integration.
- Clear distinction between optional and mandatory cryptographic signing.

## [1.9.1] - 2025-12-19

### Added

-

### Changed

-

### Removed

-

# Changelog

## 1.9.1 — 2025-XX-XX

### CLI

- Improved `vix pack` signing behavior:
  - Minisign password prompt is now visible when `--verbose` is enabled
  - Prevents silent blocking when a protected secret key is used
- Improved CLI help output:
  - Cleaner global help layout
  - Clearer `pack` and `verify` command descriptions
  - Better examples aligned with real workflows

### Packaging

- `vix pack` now provides a smoother UX when signing packages
- Explicit feedback when a `.vixpkg` artifact is successfully created

### Verification

- Added `vix verify` command:
  - Manifest v2 validation
  - Payload digest verification
  - Optional minisign signature verification
  - Auto-detection of latest `dist/<name>@<version>`
  - Support for `.vixpkg` artifacts
- Added `--require-signature` and strict verification modes

### Security

- Clear separation between unsigned and signed packages
- Environment-based key discovery:
  - `VIX_MINISIGN_SECKEY` for signing
  - `VIX_MINISIGN_PUBKEY` for verification

---

This release focuses on **developer experience**, **security clarity**, and
a more professional packaging & verification workflow.

## [1.9.0] - 2025-12-19

### Added

-

### Changed

-

### Removed

-

## v1.9.0 — 2025-01-18

### Added

- `vix pack`: new CLI command to package a Vix project into a distributable artifact.
- Generation of `dist/<name>@<version>/` with optional `.vixpkg` zip archive.
- Manifest v2 (`vix.manifest.v2`) including:
  - Package metadata (name, version, kind, license).
  - ABI detection (OS, architecture).
  - Toolchain information (C++ compiler, standard, CMake version and generator).
  - Layout flags (include, src, lib, modules, README).
  - Exports and dependencies from `vix.toml`.

### Security

- Payload integrity verification via:
  - Stable SHA256 listing of payload files.
  - `meta/payload.digest` (content digest).
- Optional Ed25519 signature using `minisign`:
  - Signature stored as `meta/payload.digest.minisig`.
  - Secret key provided via `VIX_MINISIGN_SECKEY`.

### Changed

- `vix help` now lists the `pack` command.
- `vix help pack` provides detailed usage and options.

### Notes

- Signing is optional and only enabled when `minisign` is available and `VIX_MINISIGN_SECKEY` is set.
- The manifest is generated after checksums to avoid self-referential hashes.

## [1.8.1] - 2025-12-17

### Added

-

### Changed

-

### Removed

-

## [1.8.0] - 2025-12-14

### Added

-

### Changed

-

### Removed

-

## [1.7.0] - 2025-12-12

### Added

-

### Changed

-

### Removed

-

## [1.6.7] - 2025-12-11

### Added

-

### Changed

-

### Removed

-

## [1.6.6] - 2025-12-11

### Added

-

### Changed

-

### Removed

-

cli: fix WebSocket linkage in RunScript script-mode builds

- Corrected the internal CMake generation so script builds now link against vix::websocket
- Prevents unresolved symbols (LowLevelServer::run, Session, etc.)
- Allows using <vix/websocket.hpp> directly inside standalone scripts
- Improves reliability of `vix run` when testing HTTP+WS combined examples

## [1.6.5] - 2025-12-10

### Added

-

### Changed

-

### Removed

-

### Added

- Upcoming improvements will appear here.

### Changed

- Pending changes will be listed here.

### Fixed

- Pending fixes will be listed here.

---

## [1.0.0] - Initial Release

### Added

- Modular C++ framework structure with `core`, `orm`, `cli`, `docs`, `middleware`, `websocket`, `devtools`, `examples`.
- `App` class for simplified HTTP server setup.
- Router system supporting dynamic route parameters (`/users/{id}` style).
- JSON response wrapper using `nlohmann::json`.
- Middleware system for request handling.
- Example endpoints `/hello`, `/ping`, and `/users/{id}`.
- Thread-safe signal handling for graceful shutdown.
- Basic configuration system (`Config` class) to manage JSON config files.

### Changed

- Logger integrated using `spdlog` with configurable log levels.
- Improved request parameter extraction for performance.

### Fixed

- Path parameter extraction now correctly handles `string_view` types.
- Fixed default response for unmatched routes (`404` JSON message).

---

## [0.0.1] - Draft

### Added

- Project skeleton created.
- Basic CMake setup and folder structure.
- Placeholder modules for `core`, `orm`, and `examples`.
