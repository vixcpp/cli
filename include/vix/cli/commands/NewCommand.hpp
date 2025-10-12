#ifndef VIX_NEW_COMMAND_HPP
#define VIX_NEW_COMMAND_HPP

#include <string>
#include <vector>

namespace Vix::Commands::NewCommand
{
    /**
     * @brief Creates a new minimal Vix project.
     *
     * This command scaffolds a ready-to-build C++ project using the Vix framework.
     * It automatically generates:
     *   - `src/main.cpp` — a minimal "Hello World" HTTP server example
     *   - `CMakeLists.txt` — a portable build script (supports both local repo and installed Vix)
     *
     * The project is created in the repository root by default,
     * or under `$VIX_PROJECTS_DIR` if that environment variable is defined.
     *
     * **Usage Example:**
     * @code
     * vix new myapp
     * @endcode
     *
     * **Supported Environment Variables:**
     *   - `VIX_REPO_ROOT` — explicitly set the Vix repository root (otherwise auto-detected)
     *   - `VIX_PROJECTS_DIR` — custom base directory where the project will be created
     *
     * @param args Command-line arguments (expects the project name as the first argument)
     * @return int Exit code (0 = success, non-zero = failure)
     */
    int run(const std::vector<std::string> &args);
}

#endif // VIX_NEW_COMMAND_HPP
