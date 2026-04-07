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
}
