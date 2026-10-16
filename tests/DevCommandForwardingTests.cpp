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
  int run_watched(const std::vector<std::string> &arguments)
  {
    observedArguments = arguments;
    return 23;
  }
}

int main()
{
  const std::vector<std::string> input{
      "app", "--dir", "project", "--cwd", "run-dir", "--san", "--", "two words"};
  const int result = vix::commands::DevCommand::run(input);

  assert(result == 23);
  assert((observedArguments == std::vector<std::string>{
      "app", "--dir", "project", "--cwd", "run-dir", "--san", "--", "two words"}));

  observedArguments.clear();
  assert(vix::commands::DevCommand::run({"app", "--", "two words"}) == 23);
  assert((observedArguments == std::vector<std::string>{"app", "--", "two words"}));
}
