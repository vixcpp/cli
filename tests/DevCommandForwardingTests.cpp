#include <vix/cli/commands/DevCommand.hpp>

#include <cassert>
#include <string>
#include <vector>

namespace
{
  std::vector<std::string> observedArguments;
}

namespace vix::commands::RunCommand
{
  int run(const std::vector<std::string> &arguments)
  {
    observedArguments = arguments;
    return 23;
  }
}

int main()
{
  const std::vector<std::string> input{
      "app", "--dir", "project", "--args", "two words"};
  const int result = vix::commands::DevCommand::run(input);

  assert(result == 23);
  assert((observedArguments == std::vector<std::string>{
      "--watch", "app", "--dir", "project", "--args", "two words"}));

  observedArguments.clear();
  assert(vix::commands::DevCommand::run({"--reload", "app"}) == 23);
  const auto &alreadyWatched = observedArguments;
  assert((alreadyWatched == std::vector<std::string>{"--watch", "--reload", "app"}));
}
