/**
 *
 *  @file IErrorRule.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_I_ERROR_RULE_HPP
#define VIX_I_ERROR_RULE_HPP

#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/ErrorContext.hpp>

namespace vix::cli::errors
{
  /// A single "friendly error" rule.
  /// - match(): decides if this rule applies to a compiler error
  /// - handle(): prints a custom friendly message and returns true if handled
  class IErrorRule
  {
  public:
    virtual ~IErrorRule() = default;

    virtual bool match(const CompilerError &err) const = 0;
    virtual bool handle(const CompilerError &err, const ErrorContext &ctx) const = 0;
  };
} // namespace vix::cli::errors

#endif
