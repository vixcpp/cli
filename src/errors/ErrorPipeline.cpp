#include "vix/cli/errors/ErrorPipeline.hpp"
#include "vix/cli/errors/RulesFactory.hpp"

#include <string>

namespace vix::cli::errors
{
    ErrorPipeline::ErrorPipeline()
    {
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
        return p.rfind("/usr/include/", 0) == 0 ||
               p.rfind("/usr/local/include/", 0) == 0 ||
               p.rfind("/usr/lib/", 0) == 0;
    }

    static bool isUserFirst(const CompilerError &e, const ErrorContext &ctx)
    {
        if (!ctx.sourceFile.empty() && e.file == ctx.sourceFile.string())
            return true;

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

            for (const auto &rule : rules_)
            {
                if (rule && rule->match(err))
                    return rule->handle(err, ctx);
            }
        }

        for (const auto &err : errors)
        {
            for (const auto &rule : rules_)
            {
                if (rule && rule->match(err))
                    return rule->handle(err, ctx);
            }
        }

        return false;
    }
} // namespace vix::cli::errors
