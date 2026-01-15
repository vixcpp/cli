/**
 *
 *  @file CheckCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/Style.hpp>

#include <vix/cli/commands/check/CheckDetail.hpp>

#include <filesystem>
#include <iostream>

using namespace vix::cli::style;
namespace fs = std::filesystem;

namespace vix::commands::CheckCommand
{
  using namespace detail;

  static bool exists_cmake_project(const fs::path &p)
  {
    std::error_code ec;
    return fs::exists(p / "CMakeLists.txt", ec);
  }

  static fs::path resolve_project_dir_or_empty(const Options &opt)
  {
    const fs::path cwd = fs::current_path();

    if (!opt.dir.empty())
    {
      fs::path d = fs::path(opt.dir);
      if (exists_cmake_project(d))
        return d;
      return {};
    }

    if (exists_cmake_project(cwd))
      return cwd;

    fs::path cur = cwd;
    for (int i = 0; i < 6; ++i)
    {
      if (exists_cmake_project(cur))
        return cur;

      if (!cur.has_parent_path())
        break;

      fs::path parent = cur.parent_path();
      if (parent == cur)
        break;

      cur = parent;
    }

    return {};
  }

  int run(const std::vector<std::string> &args)
  {
    const Options opt = parse(args);

    if (opt.singleCpp)
      return detail::check_single_cpp(opt);

    const fs::path projectDir = resolve_project_dir_or_empty(opt);

    if (projectDir.empty())
    {
      error("Unable to determine the project folder.");
      hint("Try: vix check --dir <path> or run from a CMake project directory.");
      return 1;
    }

    info("Using project directory:");
    step(projectDir.string());

    return detail::check_project(opt, projectDir);
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix check [path|file.cpp] [options]\n\n";

    out << "Description:\n";
    out << "  Validate a Vix/CMake project or a single-file C++ script.\n\n";

    out << "  Project mode:\n";
    out << "    - Configure (via CMake presets)\n";
    out << "    - Build the project\n";
    out << "    - Optional: run tests (CTest)\n";
    out << "    - Optional: run the built binary (runtime check)\n\n";

    out << "  Script mode (.cpp file):\n";
    out << "    - Configure a temporary project\n";
    out << "    - Compile the file\n";
    out << "    - No execution (use `vix run` for execution)\n\n";

    out << "Options:\n";
    out << "  -d, --dir <path>          Explicit project directory\n";
    out << "  --preset <name>          Configure preset (default: dev-ninja)\n";
    out << "  --build-preset <name>    Build preset override (optional)\n";
    out << "  -j, --jobs <n>           Number of parallel build jobs\n\n";

    out << "  --tests                  Run CTest after build (project mode)\n";
    out << "  --ctest-preset <name>    CTest preset to use (optional)\n";
    out << "  --run                    Run the built binary after build\n\n";

    out << "Sanitizers:\n";
    out << "  --san                    Enable AddressSanitizer + UBSan\n";
    out << "  --ubsan                  Enable UBSan only\n\n";

    out << "Notes:\n";
    out << "  - In project mode, sanitizers are applied via presets\n";
    out << "    (e.g. dev-ninja-san, dev-ninja-ubsan)\n";
    out << "  - In script mode, sanitizers affect compilation only\n\n";

    out << "Examples:\n";
    out << "  vix check                      Check current project (configure + build)\n";
    out << "  vix check --tests               Build project and run CTest\n";
    out << "  vix check --run                 Build project and run the compiled binary\n";
    out << "  vix check --tests --run          Full validation: build, tests, runtime\n";
    out << "  vix check --san --tests --run    Full validation with ASan + UBSan enabled\n\n";

    out << "  vix check --dir ./examples/api   Check a project located in a specific directory\n\n";

    out << "  vix check main.cpp               Compile a single C++ file (script mode)\n";
    out << "  vix check main.cpp --san          Compile a script with ASan + UBSan\n";
    out << "  vix check main.cpp --ubsan        Compile a script with UBSan only\n";

    return 0;
  }

} // namespace vix::commands::CheckCommand
