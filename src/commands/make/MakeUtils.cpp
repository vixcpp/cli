/**
 *
 *  @file MakeUtils.cpp
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
#include <vix/cli/commands/make/MakeUtils.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace vix::cli::make
{
  namespace fs = std::filesystem;

  std::string trim(std::string s)
  {
    auto is_space = [](unsigned char c)
    { return std::isspace(c) != 0; };

    while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
      s.erase(s.begin());

    while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
      s.pop_back();

    return s;
  }

  std::string to_lower(std::string s)
  {
    for (char &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  std::string to_upper(std::string s)
  {
    for (char &c : s)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
  }

  bool starts_with(std::string_view s, std::string_view prefix)
  {
    return s.size() >= prefix.size() &&
           s.substr(0, prefix.size()) == prefix;
  }

  bool ends_with(std::string_view s, std::string_view suffix)
  {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
  }

  bool is_identifier_start(char c)
  {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
  }

  bool is_identifier_char(char c)
  {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
  }

  bool is_valid_cpp_identifier(const std::string &s)
  {
    if (s.empty())
      return false;

    if (!is_identifier_start(s.front()))
      return false;

    for (char c : s)
    {
      if (!is_identifier_char(c))
        return false;
    }

    return true;
  }

  bool is_reserved_cpp_keyword(const std::string &s)
  {
    static const std::unordered_set<std::string> keywords = {
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "auto",
        "bitand",
        "bitor",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "class",
        "compl",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "const_cast",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
        "xor_eq"};

    return keywords.find(s) != keywords.end();
  }

  bool is_valid_namespace_token(const std::string &s)
  {
    return is_valid_cpp_identifier(s) && !is_reserved_cpp_keyword(s);
  }

  bool is_valid_namespace_string(const std::string &ns)
  {
    if (ns.empty())
      return true;

    std::size_t start = 0;
    while (start < ns.size())
    {
      const std::size_t pos = ns.find("::", start);
      const std::string token =
          pos == std::string::npos
              ? ns.substr(start)
              : ns.substr(start, pos - start);

      if (token.empty() || !is_valid_namespace_token(token))
        return false;

      if (pos == std::string::npos)
        break;

      start = pos + 2;
    }

    return true;
  }

  std::vector<std::string> split_namespace(const std::string &ns)
  {
    std::vector<std::string> out;
    if (ns.empty())
      return out;

    std::size_t start = 0;
    while (start < ns.size())
    {
      const std::size_t pos = ns.find("::", start);
      if (pos == std::string::npos)
      {
        out.push_back(ns.substr(start));
        break;
      }

      out.push_back(ns.substr(start, pos - start));
      start = pos + 2;
    }

    return out;
  }

  std::string snake_case(std::string s)
  {
    std::string out;
    out.reserve(s.size() * 2);

    for (std::size_t i = 0; i < s.size(); ++i)
    {
      const char c = s[i];

      if (std::isalnum(static_cast<unsigned char>(c)) == 0)
      {
        if (!out.empty() && out.back() != '_')
          out.push_back('_');
        continue;
      }

      if (std::isupper(static_cast<unsigned char>(c)) != 0)
      {
        if (!out.empty() &&
            out.back() != '_' &&
            std::islower(static_cast<unsigned char>(out.back())) != 0)
        {
          out.push_back('_');
        }

        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      }
      else
      {
        out.push_back(c);
      }
    }

    while (!out.empty() && out.front() == '_')
      out.erase(out.begin());

    while (!out.empty() && out.back() == '_')
      out.pop_back();

    return out.empty() ? "item" : out;
  }

  std::string join_guard_parts(const std::vector<std::string> &parts)
  {
    std::string out;

    for (const auto &part : parts)
    {
      if (!out.empty())
        out += "_";

      for (char c : part)
      {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
        {
          out.push_back(
              static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        else
        {
          out.push_back('_');
        }
      }
    }

    return out.empty() ? "VIX_GENERATED_HPP" : out;
  }

  std::string make_include_guard(const fs::path &file)
  {
    std::vector<std::string> parts;

    for (const auto &part : file)
    {
      const std::string s = part.string();
      if (!s.empty())
        parts.push_back(s);
    }

    return join_guard_parts(parts);
  }

  std::string namespace_open(const std::string &ns)
  {
    if (ns.empty())
      return {};

    std::ostringstream out;
    const auto parts = split_namespace(ns);

    for (const auto &part : parts)
      out << "namespace " << part << "\n{\n";

    return out.str();
  }

  std::string namespace_close(const std::string &ns)
  {
    if (ns.empty())
      return {};

    std::ostringstream out;
    const auto parts = split_namespace(ns);

    for (std::size_t i = 0; i < parts.size(); ++i)
      out << "}\n";

    return out.str();
  }

  std::string qualified_name(const std::string &ns, const std::string &name)
  {
    return ns.empty() ? name : (ns + "::" + name);
  }

  bool exists_file(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && fs::is_regular_file(p, ec) && !ec;
  }

  bool exists_dir(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && fs::is_directory(p, ec) && !ec;
  }

  bool ensure_dir(const fs::path &p)
  {
    std::error_code ec{};
    if (fs::exists(p, ec))
      return !ec;

    return fs::create_directories(p, ec) && !ec;
  }

  std::optional<std::string> read_file(const fs::path &p)
  {
    std::ifstream in(p.string(), std::ios::in | std::ios::binary);
    if (!in)
      return std::nullopt;

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }

  bool write_file_overwrite(const fs::path &p, const std::string &content)
  {
    std::ofstream out(
        p.string(),
        std::ios::out | std::ios::binary | std::ios::trunc);

    if (!out)
      return false;

    out << content;
    return static_cast<bool>(out);
  }

} // namespace vix::cli::make
