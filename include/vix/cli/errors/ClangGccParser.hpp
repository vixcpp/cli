#ifndef VIX_CLANG_GCC_PARSER_HPP
#define VIX_CLANG_GCC_PARSER_HPP

#include <string>
#include <vector>

#include "vix/cli/errors/CompilerError.hpp"

namespace vix::cli::errors
{
  class ClangGccParser
  {
  public:
    /// Parse Clang/GCC-style errors from the build log.
    /// Only "error" and "fatal error" entries are returned (warnings ignored).
    static std::vector<CompilerError> parse(const std::string &buildLog);
  };
} // namespace vix::cli::errors

#endif
