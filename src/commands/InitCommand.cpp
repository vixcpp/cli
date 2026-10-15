/** @file InitCommand.cpp */
#include <vix/cli/commands/InitCommand.hpp>
#include <vix/cli/project/native/TomlParser.hpp>
#include <vix/cli/util/Ui.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace vix::commands
{
  namespace fs = std::filesystem;

  int InitCommand::run(const std::vector<std::string> &args)
  {
    if (!args.empty())
    {
      if (args.size() == 1 && (args.front() == "--help" || args.front() == "-h"))
        return help();
      vix::cli::util::err_line(std::cerr, "vix init takes no arguments; it adopts the current directory without rearranging files.");
      return 2;
    }

    std::error_code ec;
    const fs::path root = fs::current_path(ec);
    if (ec)
    {
      vix::cli::util::err_line(std::cerr, "cannot determine the current directory: " + ec.message());
      return 1;
    }

    if (fs::is_regular_file(root / "vix.toml", ec) && !ec)
    {
      vix::cli::util::err_line(std::cerr, "this directory is already initialized: vix.toml exists");
      return 1;
    }
    ec.clear();
    if (fs::is_regular_file(root / "CMakeLists.txt", ec) && !ec)
    {
      vix::cli::util::err_line(std::cerr, "this directory is already an external CMake project; vix init will not create ambiguous project authority");
      return 1;
    }

    std::string name = root.filename().string();
    if (name.empty())
      name = "project";

    vix::cli::project::native::NativeProject project;
    project.name = std::move(name);
    // Format 1 permits an empty target set. Initialization observes the
    // directory; it does not guess source files or invent target architecture.
    const auto written = vix::cli::project::native::write_native_manifest_atomic(
        root / "vix.toml", project);
    if (!written.valid())
    {
      vix::cli::util::err_line(std::cerr, "cannot initialize native project: " + written.diagnostic);
      return 1;
    }

    vix::cli::util::ok_line(std::cout, "Initialized native Vix project");
    std::cout << "  Created\n  vix.toml\n";
    return 0;
  }

  int InitCommand::help()
  {
    std::cout << "vix init\n"
              << "Adopt the current directory as a native Vix project without moving or guessing files.\n\n"
              << "Usage\n"
              << "  vix init\n";
    return 0;
  }
}
