/**
 *
 *  @file ErrorContext.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_ERROR_CONTEXT_HPP
#define VIX_ERROR_CONTEXT_HPP

#include <filesystem>
#include <string>

namespace vix::cli::errors
{
  struct ErrorContext
  {
    std::filesystem::path sourceFile;
    std::string contextMessage;
    std::string buildLog;
  };
} // namespace vix::cli::errors

#endif
