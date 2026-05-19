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
#include <vix/cli/commands/check/CheckDetail.hpp>
#include <vix/cli/app/AppProjectResolver.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>

#include <filesystem>
#include <iostream>
#include <system_error>
#include <vector>

namespace vix::commands::CheckCommand
{
  namespace fs = std::filesystem;
  namespace ui = vix::cli::util;
  namespace style = vix::cli::style;
  namespace app = vix::cli::app;

  using namespace detail;

  namespace
  {
    static app::AppProjectResolveResult resolve_project_or_empty(const Options &opt)
    {
      fs::path base = fs::current_path();

      if (!opt.dir.empty())
        base = fs::absolute(fs::path(opt.dir));

      return app::resolve_app_project(base);
    }

    struct ScopedCurrentPath
    {
      fs::path previous;
      bool changed{false};

      explicit ScopedCurrentPath(const fs::path &next)
      {
        std::error_code ec;

        previous = fs::current_path(ec);
        if (ec)
          return;

        fs::current_path(next, ec);
        changed = !ec;
      }

      ~ScopedCurrentPath()
      {
        if (!changed)
          return;

        std::error_code ec;
        fs::current_path(previous, ec);
      }
    };

    static int check_vix_app_project(
        const Options &opt,
        const app::AppProjectResolveResult &project)
    {
      std::vector<std::string> buildArgs;

      if (!opt.preset.empty())
      {
        buildArgs.push_back("--preset");
        buildArgs.push_back(opt.preset);
      }

      if (opt.jobs > 0)
      {
        buildArgs.push_back("--jobs");
        buildArgs.push_back(std::to_string(opt.jobs));
      }

      if (opt.quiet)
        buildArgs.push_back("--quiet");

      if (opt.verbose)
        buildArgs.push_back("--verbose");

      if (opt.withSqlite)
        buildArgs.push_back("--with-sqlite");

      if (opt.withMySql)
        buildArgs.push_back("--with-mysql");

      buildArgs.push_back("--dir");
      buildArgs.push_back(project.userProjectDir.string());

      const int buildCode = vix::commands::BuildCommand::run(buildArgs);
      if (buildCode != 0)
        return buildCode;

      if (opt.tests)
      {
        std::vector<std::string> testArgs;

        testArgs.push_back(project.userProjectDir.string());

        if (!opt.preset.empty())
        {
          testArgs.push_back("--preset");
          testArgs.push_back(opt.preset);
        }

        const int testCode = vix::commands::TestsCommand::run(testArgs);
        if (testCode != 0)
          return testCode;
      }

      if (opt.runAfterBuild)
      {
        std::vector<std::string> runArgs;

        if (!opt.preset.empty())
        {
          runArgs.push_back("--preset");
          runArgs.push_back(opt.preset);
        }

        ScopedCurrentPath cwd(project.userProjectDir);
        return vix::commands::RunCommand::run(runArgs);
      }

      return 0;
    }

    static void print_project_resolution(const Options &opt, const fs::path &projectDir)
    {
      if (opt.quiet || !opt.verbose)
        return;

      ui::section(std::cout, "Check resolution");
      ui::kv(std::cout, "mode", "project");
      ui::kv(std::cout, "project dir", projectDir.string());
      ui::one_line_spacer(std::cout);
    }

    static void print_script_resolution(const Options &opt, const fs::path &scriptPath)
    {
      if (opt.quiet || !opt.verbose)
        return;

      ui::section(std::cout, "Check resolution");
      ui::kv(std::cout, "mode", "script");
      ui::kv(std::cout, "script", scriptPath.string());
      ui::one_line_spacer(std::cout);
    }

    static void print_help_section_header(std::ostream &out, const std::string &title)
    {
      out << title << ":\n";
    }
  } // namespace

  int run(const std::vector<std::string> &args)
  {
    const Options opt = parse(args);

    if (opt.singleCpp)
    {
      print_script_resolution(opt, opt.cppFile);
      return detail::check_single_cpp(opt);
    }

    const app::AppProjectResolveResult project = resolve_project_or_empty(opt);

    if (!project.success())
    {
      style::error("Unable to determine the project folder.");
      style::hint("Try: vix check --dir <path> or run from a CMake/vix.app project directory.");
      return 1;
    }

    print_project_resolution(opt, project.userProjectDir);

    if (project.kind == app::AppProjectKind::VixApp)
      return check_vix_app_project(opt, project);

    return detail::check_project(opt, project.userProjectDir);
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix check [path|file.cpp] [options]\n\n";

    print_help_section_header(out, "Description");
    out << "  Validate a Vix/CMake project or a single C++ file.\n";
    out << "  Vix can check build health, test health, runtime health, and sanitizer safety.\n\n";

    print_help_section_header(out, "Project mode");
    out << "  - Detect and configure the project\n";
    out << "  - Build the selected check profile\n";
    out << "  - Optionally run tests with CTest\n";
    out << "  - Optionally run the built executable\n";
    out << "  - With sanitizers, use an isolated build profile\n\n";

    print_help_section_header(out, "Script mode");
    out << "  - Create a temporary CMake project around the file\n";
    out << "  - Compile the file\n";
    out << "  - With sanitizers enabled, also run the binary for runtime validation\n\n";

    print_help_section_header(out, "Options");
    out << "  -d, --dir <path>         Explicit project directory\n";
    out << "  --preset <name>          Configure preset (default: dev-ninja)\n";
    out << "  --build-preset <name>    Build preset override\n";
    out << "  -j, --jobs <n>           Number of parallel build jobs\n";
    out << "  --tests                  Run CTest after build\n";
    out << "  --ctest-preset <name>    CTest preset override\n";
    out << "  --ctest-arg <arg>        Extra argument forwarded to CTest (repeatable)\n";
    out << "  --run                    Run the built executable after build\n";
    out << "  --run-timeout <sec>      Runtime timeout in seconds\n";
    out << "  --quiet, -q              Minimal output\n";
    out << "  --verbose                More verbose logging\n";
    out << "  --log-level <level>      Set VIX_LOG_LEVEL for the check session\n\n";

    print_help_section_header(out, "Sanitizers");
    out << "  --san                    Enable AddressSanitizer + UBSan\n";
    out << "  --ubsan                  Enable UBSan only\n";
    out << "  --full                   Force the complete sanitizer check, including full project configure\n\n";

    print_help_section_header(out, "Sanitizer modes");
    out << "  Default sanitizer mode is smart.\n";
    out << "  - Small projects: Vix checks the full project normally.\n";
    out << "  - Large or umbrella projects: Vix may switch to a reduced sanitizer\n";
    out << "    configure to avoid unrelated packaging/export/install failures.\n";
    out << "  - Use --full to force the complete sanitizer configure.\n";
    out << "    This is useful when you want to detect real CMake/export issues.\n\n";

    print_help_section_header(out, "Notes");
    out << "  - Project checks use isolated build directories per profile.\n";
    out << "    Example: build-ninja, build-ninja-san, build-ninja-ubsan.\n";
    out << "  - If a dedicated sanitizer preset does not exist, Vix falls back to\n";
    out << "    manual configure/build for that check profile.\n";
    out << "  - In project mode, --san and --ubsan also enable runtime validation.\n";
    out << "  - In script mode, sanitizers validate both build and runtime.\n";
    out << "  - Global packages installed by Vix are integrated into project checks.\n";
    out << "  - --san is the recommended mode for day-to-day validation.\n";
    out << "  - --san --full is stricter and can reveal real project-level CMake issues.\n\n";

    print_help_section_header(out, "Examples");
    out << "  vix check\n";
    out << "  vix check --tests\n";
    out << "  vix check --run\n";
    out << "  vix check --tests --run\n";
    out << "  vix check --san\n";
    out << "  vix check --san --full\n";
    out << "  vix check --san --tests\n";
    out << "  vix check --san --tests --run-timeout 20\n";
    out << "  vix check --ubsan\n";
    out << "  vix check --dir ./examples/api\n";
    out << "  vix check main.cpp\n";
    out << "  vix check main.cpp --san\n";
    out << "  vix check main.cpp --ubsan\n";

    return 0;
  }

} // namespace vix::commands::CheckCommand
