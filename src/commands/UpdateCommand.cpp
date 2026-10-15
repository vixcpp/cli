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
        << "vix update\n"
        << "Re-resolve all native root requirements using fresh online metadata.\n";
    return 0;
  }
} // namespace vix::commands
