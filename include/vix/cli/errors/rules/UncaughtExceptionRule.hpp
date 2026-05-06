/**
 *
 *  @file UncaughtExceptionRule.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#pragma once

#include <filesystem>
#include <string>

namespace vix::cli::errors::rules
{
  // Returns true if it handled the runtime log (and printed a clean summary).
  bool handleUncaughtException(
      const std::string &runtimeLog,
      const std::filesystem::path &sourceFile);
} // namespace vix::cli::errors::rules
