/**
 *
 *  @file BuildErrorDetectors.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/errors/build/BuildErrorDetectors.hpp>
#include <vix/cli/errors/build/CMakeBuildErrors.hpp>

namespace vix::cli::errors::build
{
  bool handleBuildErrors(std::string_view log)
  {
    if (handleCMakeBuildError(log))
      return true;

    return false;
  }
} // namespace vix::cli::errors::build
