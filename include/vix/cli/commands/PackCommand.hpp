#pragma once
#include <string>
#include <vector>

namespace vix::commands::PackCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}
