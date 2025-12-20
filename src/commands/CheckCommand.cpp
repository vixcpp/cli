#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/Style.hpp>

#include "vix/cli/commands/check/CheckDetail.hpp"

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

        // 1) --dir (prioritaire)
        if (!opt.dir.empty())
        {
            fs::path d = fs::path(opt.dir);
            if (exists_cmake_project(d))
                return d;
            return {}; // fourni mais invalide
        }

        // 2) cwd si c'est un projet CMake
        if (exists_cmake_project(cwd))
            return cwd;

        // 3) fallback : remonter parents (utile si on est dans build/, src/, etc.)
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

        // 1) Script mode
        if (opt.singleCpp)
            return detail::check_single_cpp(opt);

        // 2) Project mode
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
        out << "  Validate a Vix/CMake project or a single-file C++ script.\n";
        out << "\n";
        out << "  Project mode:\n";
        out << "    - Configure (via CMake presets)\n";
        out << "    - Build the project\n";
        out << "    - Optional: run tests (CTest)\n";
        out << "    - Optional: run the built binary (runtime check)\n";
        out << "\n";
        out << "  Script mode (.cpp file):\n";
        out << "    - Configure a temporary project\n";
        out << "    - Compile the file\n";
        out << "    - No execution (use `vix run` for execution)\n\n";

        out << "Options:\n";
        out << "  -d, --dir <path>          Explicit project directory\n";
        out << "  --preset <name>          Configure preset (default: dev-ninja)\n";
        out << "  --build-preset <name>    Build preset override (optional)\n";
        out << "  -j, --jobs <n>           Number of parallel build jobs\n";
        out << "\n";
        out << "  --tests                  Run CTest after build (project mode)\n";
        out << "  --ctest-preset <name>    CTest preset to use (optional)\n";
        out << "  --run                    Run the built binary after build\n\n";

        out << "Sanitizers:\n";
        out << "  --san                    Enable AddressSanitizer + UBSan\n";
        out << "  --ubsan                  Enable UBSan only\n";
        out << "\n";
        out << "  Notes:\n";
        out << "    - In project mode, sanitizers are applied via presets\n";
        out << "      (e.g. dev-ninja-san, dev-ninja-ubsan)\n";
        out << "    - In script mode, sanitizers affect compilation only\n\n";

        out << "Examples:\n";
        out << "  vix check\n";
        out << "      Check current project (configure + build)\n\n";
        out << "  vix check --tests\n";
        out << "      Build project and run CTest\n\n";
        out << "  vix check --run\n";
        out << "      Build project and run the compiled binary\n\n";
        out << "  vix check --tests --run\n";
        out << "      Full validation: build, tests, runtime\n\n";
        out << "  vix check --san --tests --run\n";
        out << "      Full validation with ASan + UBSan enabled\n\n";
        out << "  vix check --dir ./examples/api\n";
        out << "      Check a project located in a specific directory\n\n";
        out << "  vix check main.cpp\n";
        out << "      Compile a single C++ file (script mode)\n\n";
        out << "  vix check main.cpp --san\n";
        out << "      Compile a script with ASan + UBSan\n\n";
        out << "  vix check main.cpp --ubsan\n";
        out << "      Compile a script with UBSan only\n";

        return 0;
    }

} // namespace vix::commands::CheckCommand
