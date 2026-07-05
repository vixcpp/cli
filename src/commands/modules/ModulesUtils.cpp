/**
 * @file ModulesUtils.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/modules/ModulesUtils.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace vix::commands::modules_cmd::utils
{

  namespace fs = std::filesystem;

  // ------------------------------------------------------------------
  // String helpers
  // ------------------------------------------------------------------

  bool starts_with(std::string_view s, std::string_view pfx)
  {
    return s.size() >= pfx.size() && s.substr(0, pfx.size()) == pfx;
  }

  bool ends_with(std::string_view s, std::string_view suf)
  {
    return s.size() >= suf.size() && s.substr(s.size() - suf.size()) == suf;
  }

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
    for (auto &c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  // ------------------------------------------------------------------
  // Filesystem helpers
  // ------------------------------------------------------------------

  bool exists_dir(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && fs::is_directory(p, ec) && !ec;
  }

  bool exists_file(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && fs::is_regular_file(p, ec) && !ec;
  }

  bool ensure_dir(const fs::path &p)
  {
    std::error_code ec{};
    if (fs::exists(p, ec))
      return true;
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

  bool write_file_if_missing(const fs::path &p, const std::string &content)
  {
    if (exists_file(p))
      return true;
    std::ofstream out(p.string(), std::ios::out | std::ios::binary);
    if (!out)
      return false;
    out << content;
    return true;
  }

  bool write_file_overwrite(const fs::path &p, const std::string &content)
  {
    std::ofstream out(p.string(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    out << content;
    return true;
  }

  std::vector<fs::path> list_files_recursive(const fs::path &dir)
  {
    std::vector<fs::path> out;
    std::error_code ec{};
    if (!fs::exists(dir, ec) || ec)
      return out;

    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      std::error_code ec2{};
      if (it->is_regular_file(ec2) && !ec2)
        out.push_back(it->path());
    }
    return out;
  }

  // ------------------------------------------------------------------
  // Project resolution
  // ------------------------------------------------------------------

  fs::path resolve_root(const std::string &dirOpt)
  {
    std::error_code ec{};
    fs::path root = dirOpt.empty() ? fs::current_path(ec) : fs::path(dirOpt);
    if (ec)
      return fs::current_path();
    root = fs::absolute(root, ec);
    if (ec)
      return fs::current_path();
    return root;
  }

  std::string detect_project_name_from_cmake(const fs::path &root)
  {
    const fs::path cm = root / "CMakeLists.txt";
    auto content = read_file(cm);
    if (!content)
      return "myproj";

    std::istringstream in(*content);
    std::string line;
    while (std::getline(in, line))
    {
      std::string s = trim(line);
      if (s.empty())
        continue;

      auto pos = to_lower(s).find("project(");
      if (pos == std::string::npos)
        continue;

      auto open = s.find('(', pos);
      auto close = s.find(')', open == std::string::npos ? 0 : open + 1);
      if (open == std::string::npos || close == std::string::npos || close <= open + 1)
        continue;

      std::string inside = trim(s.substr(open + 1, close - (open + 1)));
      if (inside.empty())
        continue;

      auto sp = inside.find_first_of(" \t\r\n");
      std::string name = (sp == std::string::npos) ? inside : inside.substr(0, sp);

      if (name.size() >= 2 &&
          ((name.front() == '"' && name.back() == '"') ||
           (name.front() == '\'' && name.back() == '\'')))
        name = name.substr(1, name.size() - 2);

      if (!name.empty())
        return name;
    }

    return "myproj";
  }

  std::string detect_project_name_from_vix_app(const fs::path &root)
  {
    const fs::path app = root / "vix.app";
    auto content = read_file(app);

    if (!content)
      return "myproj";

    std::istringstream in(*content);
    std::string line;

    while (std::getline(in, line))
    {
      std::string s = trim(line);

      if (s.empty())
        continue;

      if (starts_with(s, "#"))
        continue;

      auto eq = s.find('=');

      if (eq == std::string::npos)
        continue;

      std::string key = to_lower(trim(s.substr(0, eq)));
      std::string value = trim(s.substr(eq + 1));

      if (key != "name")
        continue;

      auto comment = value.find('#');

      if (comment != std::string::npos)
        value = trim(value.substr(0, comment));

      if (value.size() >= 2 &&
          ((value.front() == '"' && value.back() == '"') ||
           (value.front() == '\'' && value.back() == '\'')))
      {
        value = value.substr(1, value.size() - 2);
      }

      value = trim(value);

      if (!value.empty())
        return value;
    }

    return "myproj";
  }

  std::string detect_project_name(const fs::path &root)
  {
    if (exists_file(root / "CMakeLists.txt"))
      return detect_project_name_from_cmake(root);

    if (exists_file(root / "vix.app"))
      return detect_project_name_from_vix_app(root);

    return "myproj";
  }

} // namespace vix::commands::modules_cmd::utils
