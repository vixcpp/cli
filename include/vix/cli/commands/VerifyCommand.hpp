#ifndef VERIFY_COMMAND_HPP
#define VERIFY_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::VerifyCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#pragma once

#endif
