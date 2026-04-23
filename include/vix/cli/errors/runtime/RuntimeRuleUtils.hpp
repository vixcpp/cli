/**
 *
 *  @file RuntimeRuleUtils.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RUNTIME_RULE_UTILS_HPP
#define VIX_RUNTIME_RULE_UTILS_HPP

#include <vix/cli/errors/CompilerError.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::errors::runtime
{
  bool icontains(const std::string &text, const std::string &needle);

  std::string strip_line_comment(const std::string &line);

  std::optional<std::vector<std::string>> read_file_lines(
      const std::filesystem::path &path);

  vix::cli::errors::CompilerError make_runtime_location(
      const std::filesystem::path &sourceFile,
      int line,
      int column,
      const std::string &message);

  void print_runtime_codeframe(
      const vix::cli::errors::CompilerError &err);

  void print_runtime_hints_and_at(
      const std::vector<std::string> &hints,
      const std::string &at);
} // namespace vix::cli::errors::runtime

#endif
