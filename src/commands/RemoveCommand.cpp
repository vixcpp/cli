#include <vix/cli/commands/RemoveCommand.hpp>
#include <vix/cli/project/native/PackageCommandRuntime.hpp>

#include <iostream>

namespace vix::commands
{
  int RemoveCommand::run(const std::vector<std::string> &args)
  {
    return vix::cli::project::native::run_native_remove_command(args);
  }

  int RemoveCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix remove <publisher>/<package> [--registry <id>]\n"
        << "  vix remove <git-url>\n\n"
        << "Remove a root package requirement from the current project.\n";
    return 0;
  }
} // namespace vix::commands
