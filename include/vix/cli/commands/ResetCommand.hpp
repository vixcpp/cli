#pragma once

#include <vector>
#include <string>

namespace vix::commands
{
  class ResetCommand
  {
  public:
    static int run(const std::vector<std::string> &args);
    static int help();
  };
}
