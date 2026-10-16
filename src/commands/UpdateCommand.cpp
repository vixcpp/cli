#include <vix/cli/commands/UpdateCommand.hpp>
#include <vix/cli/project/native/PackageCommandRuntime.hpp>

#include <iostream>

namespace vix::commands
{
  int UpdateCommand::run(const std::vector<std::string> &args)
  {
    return vix::cli::project::native::run_native_update_command(args);
  }

  int UpdateCommand::help()
  {
    std::cout
        << "Usage:\n  vix update\n\n"
        << "Refresh the complete dependency graph declared by the current project.\n";
    return 0;
  }
} // namespace vix::commands
