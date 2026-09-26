/**
 * @file VixcFrontend.hpp
 * @brief Experimental adapter from Vix application sources to the public VixC frontend.
 */

#ifndef VIX_CLI_APP_VIXC_FRONTEND_HPP
#define VIX_CLI_APP_VIXC_FRONTEND_HPP

#include <filesystem>
#include <string>

#include <vix/cli/app/AppManifest.hpp>

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

  /**
   * @brief Prepares application translation units for native compilation.
   *
   * Each manifest source is processed independently and replaced in this
   * prepared manifest copy by a deterministic path below .vix/generated/vixc.
   * Original source directories are retained as include directories so quoted
   * source-relative includes continue to resolve after generation.
   *
   * @param manifest Prepared copy of the application manifest.
   * @param project_directory Application project root.
   * @return Generated source result or frontend diagnostics on failure.
   */
  [[nodiscard]]
  VixcFrontendResult prepare_vixc_sources(
      AppManifest &manifest,
      const std::filesystem::path &project_directory);
}

#endif // VIX_CLI_APP_VIXC_FRONTEND_HPP
