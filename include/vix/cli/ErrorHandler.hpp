/**
 *
 *  @file ErrorHandler.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_ERROR_HANDLER_HPP
#define VIX_ERROR_HANDLER_HPP

#include <filesystem>
#include <string>

namespace vix::cli
{
  namespace fs = std::filesystem;

  class ErrorHandler
  {
  public:
    static void printBuildErrors(
        const std::string &buildLog,
        const fs::path &sourceFile,
        const std::string &contextMessage = "Script build failed");
  };
} // namespace vix::cli

#endif
