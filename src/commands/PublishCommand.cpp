#include <vix/cli/commands/PublishCommand.hpp>

#include <vix/cli/project/native/PackagePublication.hpp>

#include <iostream>

namespace vix::commands
{
  int PublishCommand::run(const std::vector<std::string> &args)
  {
    return vix::cli::project::native::run_native_publish_command(args);
  }

  int PublishCommand::help()
  {
    std::cout
      << "Usage:\n"
      << "  vix publish\n\n"
      << "Publish the current Registry package described by vix.package.toml.\n"
      << "Configure its Registry in publishing.toml and provide the configured token environment variable.\n";
    return 0;
  }
}
