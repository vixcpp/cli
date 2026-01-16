/**
 *
 *  @file CLI.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_HPP
#define VIX_CLI_HPP

#include <string>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <vector>

namespace vix
{
  class CLI
  {
  public:
    CLI();
    int run(int argc, char **argv);

  private:
    using CommandHandler = std::function<int(const std::vector<std::string> &)>;

    std::unordered_map<std::string, CommandHandler> commands_;
    int help(const std::vector<std::string> &args);
    int version(const std::vector<std::string> &args);
  };
}

#endif
