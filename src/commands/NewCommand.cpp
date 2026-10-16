/** @file NewCommand.cpp
 * Canonical native-Vix project creation.
 */

#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/commands/new/NewGenerator.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace gen = vix::commands::new_cmd::generator;

namespace vix::commands::NewCommand
{
  int run(const std::vector<std::string> &arguments)
  {
    if (arguments.size() != 1 || arguments.front().empty() || arguments.front().front() == '-')
    {
      std::cerr << "Usage: vix new <path>\n";
      return 2;
    }

    fs::path destination = arguments.front();
    if (!destination.is_absolute()) destination = fs::current_path() / destination;
    destination = destination.lexically_normal();
    const std::string projectName = destination.filename().string();

    std::error_code error;
    if (fs::exists(destination, error) && !error)
    {
      if (!fs::is_directory(destination, error) || error)
      {
        std::cerr << "vix new: destination is not a directory: " << destination << '\n';
        return 1;
      }
      if (!gen::dir_is_empty(destination))
      {
        std::cerr << "vix new: destination is not empty: " << destination << '\n';
        return 1;
      }
    }

    std::string diagnostic;
    if (!gen::ensure_dir(destination, diagnostic) ||
        !gen::generate_native_project(destination, projectName, diagnostic))
    {
      std::cerr << "vix new: project creation failed: " << diagnostic << '\n';
      return 1;
    }

    std::cout << "Created native C++ project " << projectName << " at " << destination << '\n'
              << "Next: cd " << destination.filename().string() << " && vix build\n";
    return 0;
  }

  int help()
  {
    std::cout
        << "Usage:\n  vix new <path>\n\n"
        << "Create a minimal native C++ project at the given path.\n";
    return 0;
  }
} // namespace vix::commands::NewCommand
