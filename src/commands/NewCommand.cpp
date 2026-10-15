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
    std::string projectArg;
    fs::path base = fs::current_path();
    bool force = false;

    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
      const auto &argument = arguments[index];
      if (argument == "--force")
      {
        force = true;
      }
      else if (argument == "--dir" || argument == "-d")
      {
        if (++index >= arguments.size())
        {
          std::cerr << "vix new: --dir requires a path\n";
          return 2;
        }
        base = arguments[index];
      }
      else if (argument.rfind("--dir=", 0) == 0)
      {
        base = argument.substr(6);
      }
      else if (!argument.empty() && argument.front() == '-')
      {
        std::cerr << "vix new: unsupported option: " << argument << '\n';
        return 2;
      }
      else if (projectArg.empty())
      {
        projectArg = argument;
      }
      else
      {
        std::cerr << "vix new: expected one project name or path\n";
        return 2;
      }
    }

    if (projectArg.empty())
    {
      std::cerr << "vix new: missing project name or path\n";
      return 2;
    }

    const bool inPlace = projectArg == "." || projectArg == "./" || projectArg == ".\\";
    fs::path destination = projectArg;
    if (!destination.is_absolute())
      destination = base / destination;
    destination = destination.lexically_normal();
    const std::string projectName = inPlace
        ? gen::current_dir_name()
        : destination.filename().string();

    std::error_code error;
    if (fs::exists(destination, error) && !error)
    {
      if (!fs::is_directory(destination, error) || error)
      {
        std::cerr << "vix new: destination is not a directory: " << destination << '\n';
        return 1;
      }
      if (!gen::dir_is_empty(destination) && !force)
      {
        std::cerr << "vix new: destination is not empty; pass --force to add the native scaffold\n";
        return 1;
      }
      for (const char *marker : {"CMakeLists.txt", "vix.json", "vix.app", "vix.lock"})
      {
        if (fs::exists(destination / marker, error) && !error)
        {
          std::cerr << "vix new: refusing to overlay conflicting project authority: " << marker << '\n';
          return 1;
        }
        error.clear();
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
        << "Usage:\n"
        << "  vix new <name|path> [-d|--dir <base-dir>] [--force]\n\n"
        << "Create a native C++ project with vix.toml and one executable target.\n";
    return 0;
  }
} // namespace vix::commands::NewCommand
