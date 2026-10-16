#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>

#include <iostream>
#include <filesystem>
#include <vector>

namespace vix::commands::DevCommand
{
  int run(const std::vector<std::string> &arguments)
  {
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
      if (arguments[index] == "--") break;
      const std::filesystem::path input{arguments[index]};
      if (input.extension() == ".cpp" || input.extension() == ".cc" || input.extension() == ".cxx")
      {
        std::cerr << "vix dev accepts a project target, not a single source file.\n";
        return 2;
      }
    }
    return vix::commands::RunCommand::run_watched(arguments);
  }

  int help()
  {
    std::cout << "Usage:\n"
              << "  vix dev [target] [options] [-- program-args...]\n\n"
              << "For native Vix projects, build and restart a target as project files change.\n"
              << "External CMake projects currently run once; automatic watching is not available.\n\n"
              << "Options:\n"
              << "  -d, --dir <path>          Project directory\n"
              << "  --release                 Build with release settings\n"
              << "  --cwd <path>              Runtime working directory\n"
              << "  --env <K=V>               Set a runtime environment variable\n"
              << "  --verbose                 Show additional output\n"
              << "  -q, --quiet               Reduce command output\n\n"
              << "Advanced:\n"
              << "  --cmake <arg>             One configure argument for external CMake projects\n\n"
              << "Diagnostics:\n"
              << "  Recommended diagnostics are enabled by default when Vix controls compilation.\n"
              << "  --san                     ASan + UBSan\n"
              << "  --asan                    Detect memory-safety errors\n"
              << "  --ubsan                   Detect undefined behavior\n"
              << "  --tsan                    Detect data races\n"
              << "  --diagnostics=off         Disable automatic diagnostics\n";
    return 0;
  }
}
