/**
 *
 *  @file MakePaths.cpp
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
#include <vix/cli/commands/make/MakePaths.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace vix::cli::make
{
  namespace fs = std::filesystem;

  namespace
  {
    static std::string trim(std::string s)
    {
      auto is_space = [](unsigned char c)
      { return std::isspace(c) != 0; };

      while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());

      while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
        s.pop_back();

      return s;
    }

    static std::string to_lower(std::string s)
    {
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    static bool starts_with(std::string_view s, std::string_view pfx)
    {
      return s.size() >= pfx.size() && s.substr(0, pfx.size()) == pfx;
    }

    static std::optional<std::string> read_file(const fs::path &p)
    {
      std::ifstream in(p.string(), std::ios::in | std::ios::binary);
      if (!in)
        return std::nullopt;

      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    static bool is_identifier_start(char c)
    {
      return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    static std::string snake_case(std::string s)
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
          if (!out.empty() && out.back() != '_' &&
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

      return out.empty() ? "app" : out;
    }
  }

  fs::path resolve_root(const std::string &dir_opt)
  {
    std::error_code ec{};
    fs::path root = dir_opt.empty() ? fs::current_path(ec) : fs::path(dir_opt);

    if (ec)
      return fs::current_path();

    root = fs::absolute(root, ec);
    if (ec)
      return fs::current_path();

    return root;
  }

  std::string detect_project_name_from_cmake(const fs::path &root)
  {
    const fs::path cmake_file = root / "CMakeLists.txt";
    const auto content = read_file(cmake_file);
    if (!content)
      return "app";

    std::istringstream in(*content);
    std::string line;

    while (std::getline(in, line))
    {
      std::string s = trim(line);
      if (s.empty())
        continue;

      const auto pos = to_lower(s).find("project(");
      if (pos == std::string::npos)
        continue;

      const auto open = s.find('(', pos);
      const auto close = s.find(')', open == std::string::npos ? 0 : open + 1);

      if (open == std::string::npos || close == std::string::npos ||
          close <= open + 1)
      {
        continue;
      }

      std::string inside = trim(s.substr(open + 1, close - (open + 1)));
      if (inside.empty())
        continue;

      const auto sp = inside.find_first_of(" \t\r\n");
      std::string name =
          (sp == std::string::npos) ? inside : inside.substr(0, sp);

      if (name.size() >= 2 &&
          ((name.front() == '"' && name.back() == '"') ||
           (name.front() == '\'' && name.back() == '\'')))
      {
        name = name.substr(1, name.size() - 2);
      }

      if (!name.empty())
        return name;
    }

    return "app";
  }

  std::string guess_default_namespace(const std::string &project)
  {
    std::string ns = snake_case(project);

    ns.erase(std::remove(ns.begin(), ns.end(), '_'), ns.end());

    if (ns.empty())
      ns = "app";

    if (!is_identifier_start(ns.front()))
      ns.insert(ns.begin(), 'n');

    static const char *keywords[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
        "bitor", "bool", "break", "case", "catch", "char", "char8_t",
        "char16_t", "char32_t", "class", "compl", "concept", "const",
        "consteval", "constexpr", "constinit", "const_cast", "continue",
        "co_await", "co_return", "co_yield", "decltype", "default", "delete",
        "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
        "extern", "false", "float", "for", "friend", "goto", "if", "inline",
        "int", "long", "mutable", "namespace", "new", "noexcept", "not",
        "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "register", "reinterpret_cast", "requires",
        "return", "short", "signed", "sizeof", "static", "static_assert",
        "static_cast", "struct", "switch", "template", "this",
        "thread_local", "throw", "true", "try", "typedef", "typeid",
        "typename", "union", "unsigned", "using", "virtual", "void",
        "volatile", "wchar_t", "while", "xor", "xor_eq"};

    for (const char *kw : keywords)
    {
      if (ns == kw)
      {
        ns += "_ns";
        break;
      }
    }

    return ns;
  }

  MakeLayout resolve_layout(const fs::path &root, const std::string &in_path)
  {
    MakeLayout layout;
    layout.root = root;
    layout.project = detect_project_name_from_cmake(root);
    layout.default_namespace = guess_default_namespace(layout.project);

    std::error_code ec{};
    layout.base = in_path.empty() ? root : fs::absolute(root / in_path, ec);
    if (ec)
      layout.base = root;

    layout.include_dir = root / "include";
    layout.src_dir = root / "src";
    layout.tests_dir = root / "tests";

    const fs::path relative = fs::relative(layout.base, root, ec);
    if (ec)
      return layout;

    const std::string rel = relative.generic_string();

    if (starts_with(rel, "modules/"))
    {
      const std::size_t first_slash = rel.find('/');
      const std::size_t second_slash = rel.find('/', first_slash + 1);

      const std::string module =
          rel.substr(first_slash + 1,
                     second_slash == std::string::npos
                         ? std::string::npos
                         : second_slash - (first_slash + 1));

      if (!module.empty())
      {
        layout.in_module = true;
        layout.module_name = module;
        layout.include_dir = root / "modules" / module / "include" / module;
        layout.src_dir = root / "modules" / module / "src";
        layout.default_namespace += "::" + module;
      }
    }

    return layout;
  }

} // namespace vix::cli::make
