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
        << "vix add\n"
        << "Add and resolve a native package requirement.\n\n"
        << "  vix add <publisher>/<package> [--registry <id>] [--version exact=<version>|compatible=<version>]\n"
        << "  vix add <git-url>\n"
        << "  vix add <git-url> --branch <name>|--tag <name>|--commit <full-object-id>\n"
        << "Registry defaults to the latest stable release; Git defaults to the remote HEAD branch.\n";
    return 0;
  }
} // namespace vix::commands
