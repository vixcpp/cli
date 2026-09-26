/**
 * @file VixcFrontend.hpp
 * @brief Experimental adapter from Vix application sources to the public VixC frontend.
 */

#ifndef VIX_CLI_APP_VIXC_FRONTEND_HPP
#define VIX_CLI_APP_VIXC_FRONTEND_HPP

#include <filesystem>
#include <string>

namespace vix::cli::app
{
  struct VixcFrontendResult final
  {
    bool success{false};
    std::filesystem::path generated_source;
    std::string diagnostics;
  };

  [[nodiscard]]
  VixcFrontendResult process_with_vixc(
      const std::filesystem::path &source);
}

#endif // VIX_CLI_APP_VIXC_FRONTEND_HPP
