#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>

#include <iostream>
#include <vector>

namespace vix::commands::DevCommand
{
  int run(const std::vector<std::string> &arguments)
  {
    return detail::dispatch_watched_run(arguments, [](const auto &watchedArguments)
    {
      return vix::commands::RunCommand::run(watchedArguments);
    });
  }

  int help()
  {
    std::cout << "Usage:\n"
              << "  vix dev [run options]\n\n"
              << "Continuously run a C++ project while watching for changes.\n"
              << "Uses the same watched-run behavior as `vix run --watch`.\n";
    return 0;
  }
}
