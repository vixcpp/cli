#ifndef RULES_FACTORY_HPP
#define RULES_FACTORY_HPP

#include <memory>

#include <vix/cli/errors/IErrorRule.hpp>

namespace vix::cli::errors
{
    // Basic / language-level rules
    std::unique_ptr<IErrorRule> makeCoutNotDeclaredRule();
    std::unique_ptr<IErrorRule> makeHeaderNotFoundRule();
    std::unique_ptr<IErrorRule> makeVectorOstreamRule();
    std::unique_ptr<IErrorRule> makeProcessNullptrAmbiguityRule();
    std::unique_ptr<IErrorRule> makeUseAfterMoveRule();
    std::unique_ptr<IErrorRule> makeMissingSemicolonRule();

    // Ownership & memory-related rules (compile-time patterns)
    std::unique_ptr<IErrorRule> makeUniquePtrCopyRule();
    std::unique_ptr<IErrorRule> makeSharedPtrRawPtrMisuseRule();
    std::unique_ptr<IErrorRule> makeDeleteMismatchRule();
    std::unique_ptr<IErrorRule> makeDanglingStringViewRule();
    std::unique_ptr<IErrorRule> makeReturnLocalRefRule();
    std::unique_ptr<IErrorRule> makeUseOfUninitializedRule();

} // namespace vix::cli::errors

#endif
