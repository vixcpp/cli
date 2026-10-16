#include <vix/cli/commands/InstallCommand.hpp>
#include <vix/cli/project/native/NativePackageInstall.hpp>

#include <iostream>

namespace vix::commands
{
  int InstallCommand::run(const std::vector<std::string> &args)
  {
    return vix::cli::project::native::run_native_install_command(args);
  }

  int InstallCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix install [--offline]\n\n"
        << "Materialize the exact package graph in vix.lock.\n"
        << "  --offline   Use verified local locked content only; never access package sources.\n";
    return 0;
  }
} // namespace vix::commands
