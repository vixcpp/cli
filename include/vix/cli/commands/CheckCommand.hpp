#pragma once
#include <string>
#include <vector>

namespace vix::commands::CheckCommand
{
    int run(const std::vector<std::string> &args);
    int help();
} // namespace vix::commands::CheckCommand
