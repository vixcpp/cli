/**
 *
 *  @file ITemplateErrorRule.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_I_TEMPLATE_ERROR_RULE_HPP
#define VIX_I_TEMPLATE_ERROR_RULE_HPP

#include <memory>

#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/ErrorContext.hpp>

namespace vix::cli::errors::template_rules
{
  /// A single friendly template error rule.
  /// - match(): decides if this rule applies to a compiler error
  /// - handle(): prints a custom friendly message and returns true if handled
  class ITemplateErrorRule
  {
  public:
    virtual ~ITemplateErrorRule() = default;

    virtual bool match(const vix::cli::errors::CompilerError &err) const = 0;

    virtual bool handle(
        const vix::cli::errors::CompilerError &err,
        const vix::cli::errors::ErrorContext &ctx) const = 0;
  };

  std::unique_ptr<ITemplateErrorRule> makeDependentTypenameRule();
  std::unique_ptr<ITemplateErrorRule> makeNoTypeNamedRule();
  std::unique_ptr<ITemplateErrorRule> makeTemplateArgumentMismatchRule();
  std::unique_ptr<ITemplateErrorRule> makeSubstitutionFailureRule();
  std::unique_ptr<ITemplateErrorRule> makeConceptConstraintFailureRule();
  std::unique_ptr<ITemplateErrorRule> makeRequiresExpressionFailureRule();
  std::unique_ptr<ITemplateErrorRule> makeNoMatchingOverloadWithConstraintsRule();
  std::unique_ptr<ITemplateErrorRule> makeLambdaCaptureLifetimeRule();
  std::unique_ptr<ITemplateErrorRule> makeCoroutineReturnTypeRule();
  std::unique_ptr<ITemplateErrorRule> makeMissingCoReturnRule();
  std::unique_ptr<ITemplateErrorRule> makeInvalidAwaitableRule();
  std::unique_ptr<ITemplateErrorRule> makeInvalidPromiseTypeRule();
  std::unique_ptr<ITemplateErrorRule> makeNoMemberAwaitReadyRule();
  std::unique_ptr<ITemplateErrorRule> makeNoMemberAwaitSuspendRule();
  std::unique_ptr<ITemplateErrorRule> makeNoMemberAwaitResumeRule();
} // namespace vix::cli::errors::template_rules

#endif
