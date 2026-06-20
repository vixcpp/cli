/**
 *
 *  @file ErrorPipeline.cpp
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
#include <vix/cli/errors/ErrorPipeline.hpp>
#include <vix/cli/errors/RulesFactory.hpp>
#include <vix/cli/errors/template/ITemplateErrorRule.hpp>

#include <string>

namespace vix::cli::errors
{
  ErrorPipeline::ErrorPipeline()
  {
    // Template-specific compile errors

    // Template parameter / deduction family (most specific first)
    templateRules_.push_back(vix::cli::errors::template_rules::makeDependentTypenameRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNoTypeNamedRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeTemplateArgumentMismatchRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInvalidTemplateTemplateArgumentRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeCtadFailureRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeRequiresExpressionFailureRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeConceptConstraintFailureRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNoMatchingOverloadWithConstraintsRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeSubstitutionFailureRule());

    // Compile-time evaluation family
    templateRules_.push_back(vix::cli::errors::template_rules::makeAllocatorValueTypeMismatchRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeStaticAssertFailureRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeConstexprEvaluationFailureRule());

    // Ownership / access / conversion family (specific before generic)
    templateRules_.push_back(vix::cli::errors::template_rules::makeMoveOnlyCopyRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeDeletedFunctionRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makePrivateConstructorRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInaccessibleMemberRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeIncompleteTypeRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNoViableConversionRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeConstQualifierRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInvalidReferenceBindingRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInvalidInitializerListRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNarrowingConversionRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeMissingBeginEndRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeAmbiguousOverloadRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeOperatorNotFoundRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeTupleVariantAccessRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInvalidUseOfVoidRule());

    // Lifetime / OOP / slicing family
    templateRules_.push_back(vix::cli::errors::template_rules::makeLambdaCaptureLifetimeRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNonVirtualDestructorDeleteRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeObjectSlicingRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeBadOverrideRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInvalidDowncastRule());

    // Coroutines family (always late, very specific signals)
    templateRules_.push_back(vix::cli::errors::template_rules::makeCoroutineReturnTypeRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeMissingCoReturnRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInvalidAwaitableRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNoMemberAwaitReadyRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNoMemberAwaitSuspendRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeNoMemberAwaitResumeRule());
    templateRules_.push_back(vix::cli::errors::template_rules::makeInvalidPromiseTypeRule());

    // Beginner / syntax / common mistakes
    rules_.push_back(makeCoutNotDeclaredRule());
    rules_.push_back(makeHeaderNotFoundRule());
    rules_.push_back(makeMissingSemicolonRule());

    // API / overload / misuse patterns
    rules_.push_back(makeVectorOstreamRule());
    rules_.push_back(makeProcessNullptrAmbiguityRule());

    // Ownership & memory safety (compile-time)
    rules_.push_back(makeUniquePtrCopyRule());
    rules_.push_back(makeSharedPtrRawPtrMisuseRule());
    rules_.push_back(makeDeleteMismatchRule());
    rules_.push_back(makeUseAfterMoveRule());
    rules_.push_back(makeDanglingStringViewRule());
    rules_.push_back(makeReturnLocalRefRule());
    rules_.push_back(makeUseOfUninitializedRule());
  }

  static bool isSystemPath(const std::string &p)
  {
    return p.find("/usr/include/") != std::string::npos ||
           p.find("/usr/local/include/") != std::string::npos ||
           p.find("/usr/lib/") != std::string::npos ||
           p.find("/usr/lib/gcc/") != std::string::npos ||
           p.find("/include/c++/") != std::string::npos ||
           p.find("/Library/Developer/CommandLineTools/") != std::string::npos ||
           p.find("/Applications/Xcode.app/") != std::string::npos ||
           p.find("\\include\\c++\\") != std::string::npos ||
           p.find("\\Microsoft Visual Studio\\") != std::string::npos ||
           p.find("\\Windows Kits\\") != std::string::npos;
  }

  static bool isUserFirst(const CompilerError &e, const ErrorContext &ctx)
  {
    if (!ctx.sourceFile.empty())
    {
      const std::string source = ctx.sourceFile.string();

      if (e.file == source)
        return true;

      if (!source.empty() &&
          e.file.size() >= source.size() &&
          e.file.compare(e.file.size() - source.size(), source.size(), source) == 0)
      {
        return true;
      }

      const std::string filename = ctx.sourceFile.filename().string();

      if (!filename.empty() && e.file.find(filename) != std::string::npos)
        return true;
    }

    if (isSystemPath(e.file))
      return false;

    return true;
  }

  bool ErrorPipeline::tryHandle(const std::vector<CompilerError> &errors, const ErrorContext &ctx) const
  {
    for (const auto &err : errors)
    {
      if (!isUserFirst(err, ctx))
        continue;

      for (const auto &rule : templateRules_)
      {
        if (rule && rule->match(err))
          return rule->handle(err, ctx);
      }

      for (const auto &rule : rules_)
      {
        if (rule && rule->match(err))
          return rule->handle(err, ctx);
      }
    }

    for (const auto &err : errors)
    {
      for (const auto &rule : templateRules_)
      {
        if (rule && rule->match(err))
          return rule->handle(err, ctx);
      }

      for (const auto &rule : rules_)
      {
        if (rule && rule->match(err))
          return rule->handle(err, ctx);
      }
    }

    return false;
  }
} // namespace vix::cli::errors
