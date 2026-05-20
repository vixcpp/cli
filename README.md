# Vix.cpp CLI Module

Official command-line interface for Vix.cpp.

The Vix CLI provides a modern developer workflow for C++ projects: create, build, run, test, format, manage dependencies, inspect the environment, and package applications.

## Documentation

Full documentation is available here:

https://docs.vixcpp.com/cli/

## Main commands

```bash
vix new app
cd app
vix install
vix dev
```

```bash
vix build
vix run
vix tests
vix check
vix fmt
```

## Script mode

Run a single C++ file directly:

```bash
vix run main.cpp
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
