#ifndef VIX_RAW_LOG_DETECTORS_HPP
#define VIX_RAW_LOG_DETECTORS_HPP

#include <filesystem>
#include <string>

namespace vix::cli::errors
{
  class RawLogDetectors
  {
  public:
    static bool handleLinkerOrSanitizer(
        const std::string &buildLog,
        const std::filesystem::path &sourceFile,
        const std::string &contextMessage);

    static bool handleRuntimeCrash(
        const std::string &runtimeLog,
        const std::filesystem::path &sourceFile,
        const std::string &contextMessage);
  };
} // namespace vix::cli::errors

#endif
