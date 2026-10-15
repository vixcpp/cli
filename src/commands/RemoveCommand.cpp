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
        << "vix remove\n"
        << "Remove a native root package requirement and re-resolve remaining roots.\n\n"
        << "  vix remove <publisher>/<package> [--registry <id>]\n"
        << "  vix remove <git-url>\n";
    return 0;
  }
} // namespace vix::commands
