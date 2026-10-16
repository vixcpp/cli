#include <vix/cli/commands/AddCommand.hpp>
#include <vix/cli/project/native/PackageCommandRuntime.hpp>

#include <iostream>

namespace vix::commands
{
  int AddCommand::run(const std::vector<std::string> &args)
  {
    return vix::cli::project::native::run_native_add_command(args);
  }

  int AddCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix add <publisher>/<package> [options]\n"
        << "  vix add <git-url> [revision]\n\n"
        << "Add a package requirement to the current project.\n\n"
        << "Options:\n"
        << "  --registry <id>          Select a logical Registry\n"
        << "  --version <constraint>   Select exact= or compatible= version\n"
        << "  --branch <name>          Select a Git branch\n"
        << "  --tag <name>             Select a Git tag\n"
        << "  --commit <object-id>     Select a full Git commit ID\n";
    return 0;
  }
} // namespace vix::commands
