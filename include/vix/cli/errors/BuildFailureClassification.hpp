/**
 *  @file BuildFailureClassification.hpp
 *  @brief Small, log-only guards used before selecting a build failure class.
 */
#ifndef VIX_BUILD_FAILURE_CLASSIFICATION_HPP
#define VIX_BUILD_FAILURE_CLASSIFICATION_HPP

#include <cctype>
#include <string_view>

namespace vix::cli::errors
{
  inline bool contains_source_compiler_diagnostic(std::string_view log) noexcept
  {
    std::size_t lineStart = 0;

    while (lineStart < log.size())
    {
      const std::size_t lineEnd = log.find('\n', lineStart);
      const std::string_view line = log.substr(
          lineStart,
          (lineEnd == std::string_view::npos ? log.size() : lineEnd) - lineStart);
      const std::size_t error = line.find(": error:");
      const std::size_t fatal = line.find(": fatal error:");
      const std::size_t marker = error != std::string_view::npos ? error : fatal;

      if (marker != std::string_view::npos)
      {
        const std::string_view location = line.substr(0, marker);
        const std::size_t columnColon = location.rfind(':');
        const std::size_t lineColon =
            columnColon == std::string_view::npos
                ? std::string_view::npos
                : location.rfind(':', columnColon == 0 ? 0 : columnColon - 1);

        const auto is_number = [](std::string_view value)
        {
          return !value.empty() &&
                 value.find_first_not_of("0123456789") == std::string_view::npos;
        };

        if (columnColon != std::string_view::npos &&
            lineColon != std::string_view::npos &&
            is_number(location.substr(columnColon + 1)) &&
            is_number(location.substr(lineColon + 1, columnColon - lineColon - 1)))
        {
          return true;
        }
      }

      if (lineEnd == std::string_view::npos)
        break;
      lineStart = lineEnd + 1;
    }

    return false;
  }
} // namespace vix::cli::errors

#endif
