#pragma once
#include <string>
#include <vector>

namespace vix::commands::VerifyCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}
