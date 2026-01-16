/**
 *
 *  @file TextHelpers.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_TEXT_HELPERS_HPP
#define VIX_TEXT_HELPERS_HPP

#include <filesystem>
#include <string>

namespace vix::cli::commands::helpers
{
  namespace fs = std::filesystem;

  // generic helpers used across commands (run/check/dev/...)
  std::string bool01(bool v);
  std::string read_text_file_or_empty(const fs::path &p);
  bool write_text_file(const fs::path &p, const std::string &text);

} // namespace vix::cli::commands::helpers

#endif
