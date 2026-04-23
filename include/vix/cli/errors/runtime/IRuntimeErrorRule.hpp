/**
 *
 *  @file IRuntimeErrorRule.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_I_RUNTIME_ERROR_RULE_HPP
#define VIX_I_RUNTIME_ERROR_RULE_HPP

#include <filesystem>
#include <memory>
#include <string>

namespace vix::cli::errors::runtime
{
  /// A single friendly runtime error rule.
  /// - match(): decides if this rule applies to a runtime log
  /// - handle(): prints a custom friendly message and returns true if handled
  class IRuntimeErrorRule
  {
  public:
    virtual ~IRuntimeErrorRule() = default;

    virtual bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const = 0;

    virtual bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const = 0;
  };

  std::unique_ptr<IRuntimeErrorRule> makeThreadJoinableRule();
  std::unique_ptr<IRuntimeErrorRule> makeSegfaultRule();
  std::unique_ptr<IRuntimeErrorRule> makeAbortRule();
  std::unique_ptr<IRuntimeErrorRule> makeDataRaceRule();
  std::unique_ptr<IRuntimeErrorRule> makeDeadlockRule();
  std::unique_ptr<IRuntimeErrorRule> makeMutexMisuseRule();
  std::unique_ptr<IRuntimeErrorRule> makeConditionVariableMisuseRule();
  std::unique_ptr<IRuntimeErrorRule> makeFuturePromiseRule();
  std::unique_ptr<IRuntimeErrorRule> makeThreadCreationFailureRule();
  std::unique_ptr<IRuntimeErrorRule> makeDetachedThreadLifetimeRule();
} // namespace vix::cli::errors::runtime

#endif
