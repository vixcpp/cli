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
  std::unique_ptr<IRuntimeErrorRule> makeDataRaceRule();
  std::unique_ptr<IRuntimeErrorRule> makeDeadlockRule();
  std::unique_ptr<IRuntimeErrorRule> makeConditionVariableMisuseRule();
  std::unique_ptr<IRuntimeErrorRule> makeMutexMisuseRule();
  std::unique_ptr<IRuntimeErrorRule> makeFuturePromiseRule();
  std::unique_ptr<IRuntimeErrorRule> makeThreadCreationFailureRule();
  std::unique_ptr<IRuntimeErrorRule> makeDetachedThreadLifetimeRule();
  std::unique_ptr<IRuntimeErrorRule> makeEmptyContainerFrontBackRule();
  std::unique_ptr<IRuntimeErrorRule> makeOutOfRangeAccessRule();
  std::unique_ptr<IRuntimeErrorRule> makeInvalidIteratorDereferenceRule();
  std::unique_ptr<IRuntimeErrorRule> makeIteratorInvalidationRule();
  std::unique_ptr<IRuntimeErrorRule> makeStringViewDanglingRuntimeRule();
  std::unique_ptr<IRuntimeErrorRule> makeSpanLifetimeRule();

  std::unique_ptr<IRuntimeErrorRule> makeDoubleFreeRule();
  std::unique_ptr<IRuntimeErrorRule> makeInvalidFreeRule();
  std::unique_ptr<IRuntimeErrorRule> makeUseAfterFreeRule();
  std::unique_ptr<IRuntimeErrorRule> makeMemoryLeakRule();
  std::unique_ptr<IRuntimeErrorRule> makeBufferOverflowRule();
  std::unique_ptr<IRuntimeErrorRule> makeStackOverflowRule();

  std::unique_ptr<IRuntimeErrorRule> makeNullPointerRule();
  std::unique_ptr<IRuntimeErrorRule> makeDivisionByZeroRule();
  std::unique_ptr<IRuntimeErrorRule> makeIntegerOverflowRule();
  std::unique_ptr<IRuntimeErrorRule> makeUninitializedMemoryRule();
  std::unique_ptr<IRuntimeErrorRule> makeMisalignedAccessRule();
  std::unique_ptr<IRuntimeErrorRule> makeInvalidCastRule();
  std::unique_ptr<IRuntimeErrorRule> makePureVirtualCallRule();

  std::unique_ptr<IRuntimeErrorRule> makeFilesystemRuntimeRule();
  std::unique_ptr<IRuntimeErrorRule> makePermissionDeniedRule();
  std::unique_ptr<IRuntimeErrorRule> makeResourceNotFoundRule();
  std::unique_ptr<IRuntimeErrorRule> makeAddressAlreadyInUseRule();
  std::unique_ptr<IRuntimeErrorRule> makeBrokenPipeRule();
  std::unique_ptr<IRuntimeErrorRule> makeTimeoutRuntimeRule();

  std::unique_ptr<IRuntimeErrorRule> makeJsonParseRuntimeRule();
  std::unique_ptr<IRuntimeErrorRule> makeConfigParseRuntimeRule();
  std::unique_ptr<IRuntimeErrorRule> makeUncaughtExceptionRuntimeRule();

  std::unique_ptr<IRuntimeErrorRule> makeSegfaultRule();
  std::unique_ptr<IRuntimeErrorRule> makeAbortRule();

  std::unique_ptr<IRuntimeErrorRule> makeBadVariantAccessRule();
} // namespace vix::cli::errors::runtime

#endif
