/**
 *
 *  @file MakeUtils.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#ifndef VIX_MAKE_UTILS_HPP
#define VIX_MAKE_UTILS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vix::cli::make
{
  [[nodiscard]] std::string trim(std::string s);
  [[nodiscard]] std::string to_lower(std::string s);
  [[nodiscard]] std::string to_upper(std::string s);

  [[nodiscard]] bool starts_with(std::string_view s, std::string_view prefix);
  [[nodiscard]] bool ends_with(std::string_view s, std::string_view suffix);

  [[nodiscard]] bool is_identifier_start(char c);
  [[nodiscard]] bool is_identifier_char(char c);

  [[nodiscard]] bool is_valid_cpp_identifier(const std::string &s);
  [[nodiscard]] bool is_reserved_cpp_keyword(const std::string &s);

  [[nodiscard]] bool is_valid_namespace_token(const std::string &s);
  [[nodiscard]] bool is_valid_namespace_string(const std::string &ns);

  [[nodiscard]] std::vector<std::string> split_namespace(const std::string &ns);

  [[nodiscard]] std::string snake_case(std::string s);
  [[nodiscard]] std::string join_guard_parts(const std::vector<std::string> &parts);
  [[nodiscard]] std::string make_include_guard(const std::filesystem::path &file);

  [[nodiscard]] std::string namespace_open(const std::string &ns);
  [[nodiscard]] std::string namespace_close(const std::string &ns);
  [[nodiscard]] std::string qualified_name(
      const std::string &ns,
      const std::string &name);

  [[nodiscard]] bool exists_file(const std::filesystem::path &p);
  [[nodiscard]] bool exists_dir(const std::filesystem::path &p);
  [[nodiscard]] bool ensure_dir(const std::filesystem::path &p);

  [[nodiscard]] std::optional<std::string> read_file(
      const std::filesystem::path &p);

  [[nodiscard]] bool write_file_overwrite(
      const std::filesystem::path &p,
      const std::string &content);
}

#endif
