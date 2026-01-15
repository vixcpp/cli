#ifndef VIX_VERIFY_COMMAND_HPP
#define VIX_VERIFY_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::VerifyCommand
{
  int run(const std::vector<std::string> &args);
  int help();
}

#pragma once

#endif
