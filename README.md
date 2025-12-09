# 🧩 Vix.cpp — CLI Module  
### Modern C++ Runtime Tooling • Zero-Friction Development • Fast Web Apps

![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![License](https://img.shields.io/badge/License-MIT-green)
![Status](https://img.shields.io/badge/Status-Stable-success)
![Platform](https://img.shields.io/badge/Platform-Linux%20|%20macOS%20|%20Windows-lightgrey)
![Runtime](https://img.shields.io/badge/Runtime-Vix.cpp%201.x-orange)

> **Vix CLI** is the official command-line interface for  
> **[Vix.cpp](https://github.com/vixcpp/vix)** — the modern C++ backend runtime.  
> It provides a *smooth*, *unified* and *developer-friendly* workflow for creating, building and running C++20 applications.

---

# 🚀 Overview

The Vix CLI (`vix`) brings to C++ what tools like **Cargo**, **Deno**, **Bun**, and **Vue CLI** bring to their ecosystems:

- Simple project creation  
- Fast builds with CMake presets  
- Instant execution with clean logs  
- Developer-oriented error messages  
- Script-mode for running `.cpp` files like a scripting language  

It finally gives C++ a **modern runtime experience**.

---

# ⚙️ Features

### 🏗️ **Project scaffolding**
Create a complete application structure in seconds:

```bash
vix new blog
```

### ⚡ **Smart build system**
- Uses CMake presets automatically  
- Detects missing configs  
- Parallel builds (`-j`)  
- Spinner & colored logs  
- ErrorHandler integration for friendlier compiler diagnostics  

### 🚀 **Run applications instantly**
```bash
vix run
```

Includes:
- Automatic build if needed  
- Live filtered logs  
- Runtime log-level injection  
- Line-buffered stdout for real-time logs  
- Clean SIGINT handling (Ctrl+C)  

### 🧠 **ErrorHandler: “your C++ teacher inside the CLI”**
- Detects overload resolution issues  
- Explains template deduction errors  
- Detects ambiguous calls, missing includes, wrong signatures  
- Clear, structured messages with hints  

### 🛠️ **Script mode (single .cpp execution)**
```bash
vix run demo.cpp
```

The CLI transparently:
1. Creates a temporary CMake project  
2. Builds it  
3. Runs it with enhanced diagnostics  

---

# 🧰 Commands

```bash
vix <command> [options]
```

| Command                      | Description                                              |
|-----------------------------|----------------------------------------------------------|
| `vix new <name>`            | Create a new project scaffold                            |
| `vix build [name]`          | Build the root or a child application                    |
| `vix run [name] [--args]`   | Build (if needed) and run an application                 |
| `vix version`               | Display CLI version and runtime details                  |
| `vix help [cmd]`            | Show help for the CLI or a specific command              |

---

# 🧪 Usage Examples

### Create a new project
```bash
vix new blog
```

### Build it
```bash
vix build
```

### Run the app
```bash
vix run -- --port 8080
```

### Example output
```
Vix.cpp runtime is ready 🚀

Logs:

Using configuration file: config/config.json
[I] ThreadPool started with 8 workers
[I] Acceptor initialized on port 8080
[I] Server request timeout set to 5000 ms
```

### Run a single C++ file
```bash
vix run hello.cpp
```

---

# 🧩 Architecture

The CLI uses a **command registry** for extensibility:

```cpp
std::unordered_map<std::string, CommandHandler> commands_;
```

### Main components

| Path                                         | Responsibility                                      |
|----------------------------------------------|------------------------------------------------------|
| `include/vix/cli/CLI.hpp`                    | CLI parser and command dispatcher                    |
| `src/CLI.cpp`                                | Entry point + command registry                       |
| `src/ErrorHandler.cpp`                       | Friendly compiler diagnostics + pattern detection    |
| `src/commands/NewCommand.cpp`                | Project scaffolding logic                            |
| `src/commands/BuildCommand.cpp`              | Build orchestration (presets + fallback)             |
| `src/commands/RunCommand.cpp`                | Runtime orchestration + environment injection        |
| `src/commands/run/RunScript.cpp`             | Script mode for single .cpp execution                |

### Runtime integration
`vix run` automatically injects:

```
VIX_STDOUT_MODE=line
```

→ Enables real-time logs in Vix.cpp applications.

---

# 🔧 Build & Installation

### Build the CLI standalone

```bash
git clone https://github.com/vixcpp/vix.git
cd vix/modules/cli
cmake -B build -S .
cmake --build build -j$(nproc)
```

Output binary:

```bash
./build/vix
```

### Build as part of the umbrella Vix project

```bash
cd vix
cmake -B build -S .
cmake --build build
```

The CLI is included automatically.

---

# ⚙️ Configuration & Options

### Environment variables

| Variable            | Description                                    |
|--------------------|------------------------------------------------|
| `VIX_LOG_LEVEL`    | Controls runtime log level (trace → critical)  |
| `VIX_STDOUT_MODE`  | Forces real-time stdout flush when set to `line` |

### CMake flags

| Option                   | Default | Description                                |
|--------------------------|---------|--------------------------------------------|
| `VIX_ENABLE_SANITIZERS` | OFF     | Enable ASan + UBSan                        |
| `VIX_ENABLE_LTO`        | OFF     | Enable link-time optimization               |

Example:

```bash
cmake -B build -S . -DVIX_ENABLE_SANITIZERS=ON
```

---

# 📦 Output (CLI Help)

```
Vix.cpp — Modern C++ backend runtime
Version: v1.6.x

Usage:
  vix [GLOBAL OPTIONS] <COMMAND> [ARGS...]

Commands:
  new <name>             Create a new Vix project
  build [name]           Configure and build a project
  run [name] [--args]    Build (if needed) and run a project
  help [command]         Show help
  version                Show CLI version

Global options:
  --verbose              Debug logs for the runtime
  -q, --quiet            Only warnings and errors
  --log-level <level>    Trace | debug | info | warn | error | critical

Examples:
  vix new api
  vix run api -- --port 8080
  vix --log-level debug run api
```

---

# 🧾 License

**MIT License** © [Gaspard Kirira](https://github.com/gkirira)  
See [`LICENSE`](../../LICENSE) for details.
