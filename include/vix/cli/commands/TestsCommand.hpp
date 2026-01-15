#ifndef VIX_TESTS_COMMAND_HPP
#define VIX_TESTS_COMMAND_HPP

#include <vector>
#include <string>

namespace vix::commands::TestsCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#endif
