#pragma once

#include <vector>
#include <string>

namespace vix::commands::TestsCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}
