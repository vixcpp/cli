/**
 *
 *  @file DependencyFile.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  GCC/Clang dependency file parser
 *
 */

#include <vix/cli/build/DependencyFile.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace vix::cli::build
{
  namespace
  {
    static std::string read_text_file_or_empty(const fs::path &path)
    {
      std::ifstream in(path);
      if (!in)
        return {};

      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }

    static bool is_space(char c)
    {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    static std::string join_continued_lines(const std::string &text)
    {
      std::string out;
      out.reserve(text.size());

      for (std::size_t i = 0; i < text.size(); ++i)
      {
        const char c = text[i];

        if (c == '\\')
        {
          if (i + 1 < text.size() && text[i + 1] == '\n')
          {
            out.push_back(' ');
            ++i;
            continue;
          }

          if (i + 2 < text.size() &&
              text[i + 1] == '\r' &&
              text[i + 2] == '\n')
          {
            out.push_back(' ');
            i += 2;
            continue;
          }
        }

        out.push_back(c);
      }

      return out;
    }

    static std::vector<std::string> tokenize_make_rule(const std::string &line)
    {
      std::vector<std::string> tokens;
      std::string current;

      bool escaped = false;

      for (char c : line)
      {
        if (escaped)
        {
          current.push_back(c);
          escaped = false;
          continue;
        }

        if (c == '\\')
        {
          escaped = true;
          continue;
        }

        if (is_space(c))
        {
          if (!current.empty())
          {
            tokens.push_back(current);
            current.clear();
          }
          continue;
        }

        current.push_back(c);
      }

      if (escaped)
        current.push_back('\\');

      if (!current.empty())
        tokens.push_back(current);

      return tokens;
    }

    static std::size_t find_unescaped_colon(const std::string &line)
    {
      bool escaped = false;

      for (std::size_t i = 0; i < line.size(); ++i)
      {
        const char c = line[i];

        if (escaped)
        {
          escaped = false;
          continue;
        }

        if (c == '\\')
        {
          escaped = true;
          continue;
        }

        if (c == ':')
          return i;
      }

      return std::string::npos;
    }

    static std::string trim_copy(const std::string &value)
    {
      std::size_t begin = 0;
      while (begin < value.size() && is_space(value[begin]))
        ++begin;

      std::size_t end = value.size();
      while (end > begin && is_space(value[end - 1]))
        --end;

      return value.substr(begin, end - begin);
    }

    static bool looks_like_phony_rule(
        const std::string &left,
        const std::string &right)
    {
      return !left.empty() && trim_copy(right).empty();
    }

    static fs::path unescape_make_path(const std::string &token)
    {
      std::string out;
      out.reserve(token.size());

      bool escaped = false;

      for (char c : token)
      {
        if (escaped)
        {
          out.push_back(c);
          escaped = false;
          continue;
        }

        if (c == '\\')
        {
          escaped = true;
          continue;
        }

        out.push_back(c);
      }

      if (escaped)
        out.push_back('\\');

      return fs::path(out);
    }
  } // namespace

  bool DependencyFile::valid() const
  {
    return !target.empty() && !dependencies.empty();
  }

  bool DependencyFile::has_dependency(const fs::path &dependency) const
  {
    const fs::path normalized = normalize_dependency_path(dependency);

    return std::find(
               dependencies.begin(),
               dependencies.end(),
               normalized) != dependencies.end();
  }

  void DependencyFile::add_dependency(const fs::path &dependency)
  {
    if (dependency.empty())
      return;

    const fs::path normalized = normalize_dependency_path(dependency);

    if (normalized.empty())
      return;

    if (has_dependency(normalized))
      return;

    dependencies.push_back(normalized);
  }

  fs::path normalize_dependency_path(const fs::path &path)
  {
    if (path.empty())
      return {};

    return path.lexically_normal();
  }

  std::optional<DependencyFile> parse_dependency_file_text(
      const std::string &text,
      const fs::path &dependencyFilePath)
  {
    const std::string flattened = join_continued_lines(text);

    DependencyFile result;
    result.path = dependencyFilePath;

    std::istringstream in(flattened);
    std::string line;

    while (std::getline(in, line))
    {
      line = trim_copy(line);

      if (line.empty())
        continue;

      const std::size_t colon = find_unescaped_colon(line);
      if (colon == std::string::npos)
        continue;

      const std::string left = trim_copy(line.substr(0, colon));
      const std::string right = line.substr(colon + 1);

      if (left.empty())
        continue;

      if (looks_like_phony_rule(left, right))
        continue;

      if (result.target.empty())
      {
        const auto targets = tokenize_make_rule(left);
        if (!targets.empty())
          result.target = normalize_dependency_path(unescape_make_path(targets.front()));
      }

      const auto deps = tokenize_make_rule(right);
      for (const auto &dep : deps)
      {
        const fs::path depPath = unescape_make_path(dep);

        if (depPath.empty())
          continue;

        result.add_dependency(depPath);
      }
    }

    if (!result.valid())
      return std::nullopt;

    return result;
  }

  std::optional<DependencyFile> read_dependency_file(const fs::path &path)
  {
    if (path.empty())
      return std::nullopt;

    const std::string text = read_text_file_or_empty(path);
    if (text.empty())
      return std::nullopt;

    return parse_dependency_file_text(text, path);
  }

  fs::path dependency_file_for_object(const fs::path &objectPath)
  {
    fs::path out = objectPath;
    out.replace_extension(".d");
    return out;
  }

} // namespace vix::cli::build
