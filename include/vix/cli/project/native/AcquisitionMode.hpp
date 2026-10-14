/** @file AcquisitionMode.hpp
 *  Operation-level source acquisition policy.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_ACQUISITION_MODE_HPP
#define VIX_CLI_PROJECT_NATIVE_ACQUISITION_MODE_HPP

namespace vix::cli::project::native
{
  // Whether required state may be obtained from package/metadata sources.
  enum class AcquisitionMode { online, offline };
} // namespace vix::cli::project::native

#endif // VIX_CLI_PROJECT_NATIVE_ACQUISITION_MODE_HPP
