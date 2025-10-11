# 🧩 Vix.cpp — CLI Module

> **Powerful developer CLI for the Vix.cpp ecosystem**  
> Create, build, and run blazing-fast C++ web applications with ease.

---

## 📖 Overview

The **Vix CLI** (`vix`) is the command-line interface for the [Vix.cpp](https://github.com/SoftAdAstra/vix.cpp) framework.  
It provides a simple and expressive way to interact with the Vix runtime, create new projects, and manage your development workflow.

The CLI is lightweight, modular, and extensible — designed to feel like **FastAPI** or **Vue CLI**, but for **modern C++20**.

---

## ⚙️ Features

- 🏗️ **Project scaffolding** — `vix new <name>` to create a new project.
- ⚡ **Instant build & run** — Unified commands to build and execute Vix apps.
- 🧩 **Extensible command registry** — Add your own commands via modules.
- 🧠 **Cross-platform support** — Works on Linux, macOS, and Windows.
- 🧰 **Modern C++20 codebase** — Built with strict warnings, sanitizers, and optional LTO.
- 🎨 **Clean terminal logging** — Rich, colorized logs powered by `Vix::Logger`.

---

## 🏗️ Build & Installation

### 1️⃣ Build standalone

You can build the CLI alone for testing or development:

```bash
git clone https://github.com/SoftAdAstra/vix.cpp.git
cd vixcpp/vix/modules/cli
cmake -B build -S .
cmake --build build -j$(nproc)
```

The binary will be output at:

```bash
./build/vix
```

### 2️⃣ Build as part of the umbrella project

If you’re building the full Vix.cpp framework:

```bash
cd vixcpp/vix
cmake -B build -S .
cmake --build build -j$(nproc)
```

This automatically includes the CLI module (Vix::cli target).

# 🚀 Usage

```bash
vix <command> [options]
```

# Available Commands

```markdown
| Command                   | Description                                        |
| ------------------------- | -------------------------------------------------- |
| `vix new <name>`          | Create a new Vix project in the current directory. |
| `vix build [name]`        | Build an existing Vix project or application.      |
| `vix run [name] [--args]` | Run a Vix project or service.                      |
| `vix version`             | Display the current CLI version.                   |
| `vix help`                | Show this help message.                            |
```

# Examples

```bash
# Create a new project
vix new blog

# Build in Debug mode
vix build blog --config Debug

# Run with arguments
vix run blog -- --port 8080

```

# 🧱 Architecture

The CLI module is built around a command registry pattern:

```cpp
std::unordered_map<std::string, std::function<int(const std::vector<std::string>&)>> commands_;
```

Each command (e.g., new, build, run) registers a handler function.
You can easily extend the CLI by adding new handlers inside src/commands/.

```markdown
| File                        | Description                                                       |
| --------------------------- | ----------------------------------------------------------------- |
| `include/vix/cli/CLI.hpp`   | Main CLI class definition.                                        |
| `src/CLI.cpp`               | Core implementation and command dispatcher.                       |
| `commands/NewCommand.cpp`   | Project scaffolding command.                                      |
| `commands/RunCommand.cpp`   | Runtime launcher for Vix apps.                                    |
| `commands/BuildCommand.cpp` | Project builder logic.                                            |
| `CMakeLists.txt`            | Build configuration (supports sanitizers, LTO, standalone build). |
```

# 🧩 Extending the CLI

You can add your own commands by following this pattern:

```cpp
// In CLI constructor
commands_["mycmd"] = [](auto args)
{
    auto &logger = Logger::getInstance();
    logger.logModule("CLI", Logger::Level::INFO, "Executing custom command!");
    return 0;
};
```

For larger commands, implement a new handler module under src/commands/.

# 🧰 Development Notes

Optional CMake Flags

```markdown
| Flag                    | Default | Description                                      |
| ----------------------- | ------- | ------------------------------------------------ |
| `VIX_ENABLE_SANITIZERS` | OFF     | Enables AddressSanitizer + UBSan.                |
| `VIX_ENABLE_LTO`        | OFF     | Enables Link-Time Optimization (Release builds). |
```

To enable sanitizers:

```bash
cmake -B build -S . -DVIX_ENABLE_SANITIZERS=ON

📦 Output:
  build/vix
🧠 Run:
  ./vix --help
✨ Enjoy building with Vix.cpp!
```

## License

MIT License – see [LICENSE](./LICENSE) for details.
