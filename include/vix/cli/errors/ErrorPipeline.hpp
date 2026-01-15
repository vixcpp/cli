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
