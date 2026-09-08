/**
 *  @file CompilerDiagnosticPresentation.hpp
 *  @brief Presentation limits for parsed compiler diagnostics.
 */
#ifndef VIX_COMPILER_DIAGNOSTIC_PRESENTATION_HPP
#define VIX_COMPILER_DIAGNOSTIC_PRESENTATION_HPP

#include <cstddef>

namespace vix::cli::errors
{
  struct CompilerDiagnosticPresentation
  {
    std::size_t shown = 0;
    std::size_t hidden = 0;

    [[nodiscard]] bool show_verbose_hint() const noexcept
    {
      return hidden != 0;
    }
  };

  inline CompilerDiagnosticPresentation compiler_diagnostic_presentation(
      std::size_t total,
      bool verbose) noexcept
  {
    const std::size_t shown = verbose ? total : (total == 0 ? 0 : 1);
    return {shown, total - shown};
  }
} // namespace vix::cli::errors

#endif
