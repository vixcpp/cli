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
  struct RuntimeLocation
  {
    std::filesystem::path file{};
    int line = 0;
    int column = 1;

    [[nodiscard]] bool valid() const noexcept
    {
      return !file.empty() && line > 0;
    }
  };

  bool icontains(const std::string &text, const std::string &needle);

  std::string strip_line_comment(const std::string &line);

  std::optional<std::vector<std::string>> read_file_lines(
      const std::filesystem::path &path);

  RuntimeLocation find_best_runtime_location(
      const std::string &log,
      const std::filesystem::path &sourceFile);

  RuntimeLocation find_best_runtime_location_or_source_hint(
      const std::string &log,
      const std::filesystem::path &sourceFile,
      const std::vector<std::string> &sourcePatterns);

  std::string make_at_text(
      const RuntimeLocation &location,
      const std::filesystem::path &sourceFile);

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

  void print_runtime_log_excerpt(
      const std::string &log,
      std::size_t maxLines = 14);
} // namespace vix::cli::errors::runtime

#endif
