/**
 *
 *  @file ErrorPipeline.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_ERROR_PIPELINE_HPP
#define VIX_ERROR_PIPELINE_HPP

#include <memory>
#include <vector>

#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/ErrorContext.hpp>
#include <vix/cli/errors/IErrorRule.hpp>

namespace vix::cli::errors
{
  class ErrorPipeline
  {
  public:
    ErrorPipeline();
    bool tryHandle(const std::vector<CompilerError> &errors, const ErrorContext &ctx) const;

  private:
    std::vector<std::unique_ptr<IErrorRule>> rules_;
  };
} // namespace vix::cli::errors

#endif
