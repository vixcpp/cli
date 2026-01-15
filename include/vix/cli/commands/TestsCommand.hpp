#ifndef TESTS_COMMAND_HPP
#define TESTS_COMMAND_HPP

#include <vector>
#include <string>

namespace vix::commands::TestsCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#endif
