# Vix.cpp CLI Module

Official command-line interface for Vix.cpp.

The Vix CLI provides a modern developer workflow for C++ projects: create, build, run, test, format, manage dependencies, inspect the environment, package applications, and deploy projects.

## Documentation

Full CLI documentation:

https://docs.vixcpp.com/cli/

## Quick start

```bash
vix new app
cd app
vix install
vix dev
```

Build and run:

```bash
vix build
vix run
```

Run tests and checks:

```bash
vix test
vix check
vix fmt
```

## Script mode

Run a single C++ file directly:

```bash
vix run main.cpp
```

You can also run a C++ file directly:

```bash
vix main.cpp
```

## Dependency management

```bash
vix add @softadastra/json
vix install
vix update
vix list
vix remove @softadastra/json
```

More information:

https://docs.vixcpp.com/cli/add

## Project information

```bash
vix info
vix doctor
```

More information:

https://docs.vixcpp.com/cli/info

## CLI help

```text
$ vix -h

Usage:
  vix <command> [options]
  vix <file.cpp>
  vix help <command>

Common commands:
  new <name>                 Create a new Vix project
  init                       Initialize the current directory
  add <package>              Add a dependency
  install                    Install project dependencies
  run                        Build and run a project or C++ file
  dev                        Start development mode
  build                      Configure and build
  test                       Run project tests
  deploy                     Deploy the application

Project:
  make                       Generate C++ scaffolding
  check                      Validate a project or source file
  replay                     Replay a recorded execution
  repl                       Start the interactive REPL
  task                       Run project tasks
  fmt                        Format C++ source files
  clean                      Remove local build caches
  reset                      Reset caches and dependencies
  modules                    Manage optional project modules

Applications:
  note                       Open a Vix Note document
  desktop                    Run a web application as desktop
  mobile                     Generate mobile WebView shells
  game                       Manage Vix game projects
  agent                      Run the local-first Vix AI agent

Production:
  production                 Show production status
  service                    Manage the system service
  proxy                      Manage reverse proxy configuration
  health                     Check application health
  logs                       Show application and proxy logs
  env                        Validate environment variables
  ws                         Diagnose WebSocket endpoints

Dependencies and registry:
  registry                   Manage the registry index
  search <query>             Search registry packages
  list                       List project dependencies
  remove <package>           Remove a dependency
  update                     Update dependencies
  outdated                   Check outdated dependencies
  store                      Manage the local package store
  publish                    Publish a package version
  unpublish                  Remove a published version

Packaging:
  pack                       Create a distributable package
  verify                     Verify package integrity
  cache                      Cache a package locally

Cloud:
  login                      Connect to Softadastra Cloud
  logout                     Remove the local cloud session
  cloud                      Manage Cloud project links
  doctor --cloud             Diagnose Cloud connectivity

Data and networking:
  db                         Inspect SQLite databases
  orm                        Manage database migrations
  p2p                        Run P2P tools

System:
  doctor                     Check the installation and toolchain
  info                       Show environment information
  completion                 Generate shell completions
  upgrade                    Upgrade the Vix CLI
  uninstall                  Uninstall the Vix CLI

Global options:
  --verbose                  Enable debug output
  -q, --quiet                Only show warnings and errors
  --log-level <level>        Set trace, debug, info, warn, error or critical
  -h, --help                 Show command help
  -v, --version              Show the installed version

Run 'vix help <command>' for detailed command usage.
Documentation: https://docs.vixcpp.com/cli/
```

## Build

### Standalone CLI build

```bash
git clone https://github.com/vixcpp/vix.git
cd vix/modules/cli

cmake -B build -S .
cmake --build build -j$(nproc)
```

Run the binary:

```bash
./build/vix
```

### Full Vix build

```bash
git clone https://github.com/vixcpp/vix.git
cd vix

cmake -B build -S .
cmake --build build -j$(nproc)
```

## Useful links

- Documentation: https://docs.vixcpp.com/
- CLI documentation: https://docs.vixcpp.com/cli/
- Engineering notes: https://blog.vixcpp.com/
- Registry: https://registry.vixcpp.com/
- GitHub: https://github.com/vixcpp/vix

## License

MIT License.

See [`LICENSE`](../../LICENSE) for details.
