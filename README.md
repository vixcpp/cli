# Vix.cpp CLI

The Vix command-line interface creates, builds, runs, tests, and manages dependencies for native C++ projects.

## Quick start

```sh
vix new hello
cd hello
vix build
vix run
```

For an existing C++ project, initialize Vix in its current directory:

```sh
vix init
vix build
vix run
```

## Commands

The Vix 3 command surface is intentionally small:

| Command | Purpose |
| --- | --- |
| `vix new <path>` | Create a minimal native Vix project. |
| `vix init` | Initialize the current directory as a Vix project. |
| `vix build [target-or-source.cpp]` | Build the project, target, or one C++ source file. |
| `vix run [target-or-source.cpp]` | Build and run a project target or one C++ source file. |
| `vix dev` | Run the project while watching for changes. |
| `vix test` | Run registered project tests. |
| `vix add <publisher>/<package>` | Add a Registry dependency. |
| `vix remove <publisher>/<package>` | Remove a root dependency. |
| `vix update` | Update dependencies and the lockfile. |
| `vix install` | Materialize dependencies already recorded in `vix.lock`. |
| `vix publish` | Submit the current package to its configured Registry. |
| `vix upgrade` | Upgrade a managed Vix installation. |

Use `vix help <command>` for command-specific usage, or `vix --help` for the command list. Global options are `--help`, `--version`, `--verbose`, and `--quiet`.

## Packages

Add a package by its publisher and package name, then install the resolved lockfile:

```sh
vix add softadastra/json
vix install
```

`vix install` installs the exact dependency set in `vix.lock`; it does not search for or update packages. Use `vix update` when you want to resolve newer compatible versions.

Publishing requires a valid `vix.package.toml`, Registry publishing configuration, and credentials supplied through the configured environment variable. A successful command reports that the Registry accepted the submission; that message does not by itself guarantee public indexing or website discoverability.

## Documentation

- CLI documentation: <https://docs.vixcpp.com/cli/>
- Vix documentation: <https://docs.vixcpp.com/>
- Source: <https://github.com/vixcpp/vix>

## Build the CLI from source

```sh
git clone https://github.com/vixcpp/vix.git
cd vix/modules/cli
cmake -S . -B build
cmake --build build
```

For the full platform build, configure and build from the repository root instead.

## License

MIT. See [`LICENSE`](../../LICENSE).
