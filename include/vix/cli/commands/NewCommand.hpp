#ifndef VIX_NEW_COMMAND_HPP
#define VIX_NEW_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::NewCommand
{

    /**
     * @brief Create a new minimal Vix project.
     *
     * Generates:
     *   - src/main.cpp     (tiny HTTP server example)
     *   - CMakeLists.txt   (portable; uses local Vix if provided, otherwise find_package)
     *   - README.md
     *   - .gitignore
     *
     * Location rules:
     *   - By default, creates the project in the current working directory (./<name>).
     *   - If the environment variable VIX_PROJECTS_DIR is set to a valid directory,
     *     the project is created under that directory instead (VIX_PROJECTS_DIR/<name>).
     *
     * Local-Vix rules:
     *   - If the environment variable VIX_REPO_ROOT points to a Vix source tree
     *     (contains CMakeLists.txt and modules/), the generated CMake uses add_subdirectory().
     *   - Otherwise, the generated CMake requires an installed Vix (find_package(Vix CONFIG REQUIRED)).
     *
     * @param args Command-line args; expects args[0] = project name (ASCII/UTF-8).
     * @return int Exit code (0 = success; non-zero = failure)
     */
    int run(const std::vector<std::string> &args);
    int help();

} // namespace Vix::Commands::NewCommand

#endif // VIX_NEW_COMMAND_HPP
