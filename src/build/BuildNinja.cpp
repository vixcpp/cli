/**
 *
 *  @file BuildNinja.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Ninja build graph parser
 *
 */

#include <vix/cli/build/BuildNinja.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::cli::build
{
  namespace
  {
    static std::string read_text_file_or_empty(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};

      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }

    static bool file_exists_regular(const fs::path &path)
    {
      std::error_code ec;
      return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

    static bool is_space(char c)
    {
      return c == ' ' ||
             c == '\t' ||
             c == '\r' ||
             c == '\n';
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

    static bool starts_with(const std::string &value, const std::string &prefix)
    {
      return value.rfind(prefix, 0) == 0;
    }

    static fs::path ninja_file_directory(const fs::path &path)
    {
      if (path.empty())
        return fs::current_path();

      const fs::path parent = path.parent_path();

      if (parent.empty())
        return fs::current_path();

      return fs::absolute(parent).lexically_normal();
    }

    static std::string join_continuations(const std::string &text)
    {
      std::string out;
      out.reserve(text.size());

      for (std::size_t i = 0; i < text.size(); ++i)
      {
        const char c = text[i];

        if (c == '$')
        {
          if (i + 1 < text.size() && text[i + 1] == '\n')
          {
            ++i;
            continue;
          }

          if (i + 2 < text.size() &&
              text[i + 1] == '\r' &&
              text[i + 2] == '\n')
          {
            i += 2;
            continue;
          }
        }

        out.push_back(c);
      }

      return out;
    }

    static std::vector<std::string> split_lines(const std::string &text)
    {
      std::vector<std::string> lines;
      std::istringstream in(text);
      std::string line;

      while (std::getline(in, line))
      {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();

        lines.push_back(line);
      }

      return lines;
    }

    static std::optional<std::pair<std::string, std::string>>
    parse_assignment(const std::string &line)
    {
      const auto pos = line.find('=');
      if (pos == std::string::npos)
        return std::nullopt;

      const std::string key = trim_copy(line.substr(0, pos));
      const std::string value = trim_copy(line.substr(pos + 1));

      if (key.empty())
        return std::nullopt;

      return std::make_pair(key, value);
    }

    static bool is_indented_binding(const std::string &line)
    {
      if (line.empty())
        return false;

      return line[0] == ' ' || line[0] == '\t';
    }

    static std::vector<std::string> tokenize_ninja_paths(const std::string &value)
    {
      std::vector<std::string> tokens;
      std::string current;

      bool escaped = false;

      for (char c : value)
      {
        if (escaped)
        {
          current.push_back(c);
          escaped = false;
          continue;
        }

        if (c == '$')
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
        current.push_back('$');

      if (!current.empty())
        tokens.push_back(current);

      return tokens;
    }

    static std::vector<fs::path> resolve_path_tokens(
        const fs::path &base,
        const std::vector<std::string> &tokens)
    {
      std::vector<fs::path> out;
      out.reserve(tokens.size());

      for (const std::string &token : tokens)
      {
        if (token.empty())
          continue;

        out.push_back(resolve_ninja_path(base, fs::path(token)));
      }

      return out;
    }

    static std::optional<NinjaRule> parse_rule(
        const std::vector<std::string> &lines,
        std::size_t &index)
    {
      std::string line = trim_copy(lines[index]);

      if (!starts_with(line, "rule "))
        return std::nullopt;

      NinjaRule rule;
      rule.name = trim_copy(line.substr(5));

      if (rule.name.empty())
        return std::nullopt;

      ++index;

      while (index < lines.size())
      {
        const std::string &raw = lines[index];

        if (!is_indented_binding(raw))
          break;

        const std::string binding = trim_copy(raw);
        const auto assignment = parse_assignment(binding);

        if (assignment)
          rule.variables[assignment->first] = assignment->second;

        ++index;
      }

      return rule;
    }

    static void append_paths(
        std::vector<fs::path> &out,
        const fs::path &base,
        const std::string &text)
    {
      const auto tokens = tokenize_ninja_paths(text);
      const auto paths = resolve_path_tokens(base, tokens);

      out.insert(out.end(), paths.begin(), paths.end());
    }

    static std::optional<NinjaEdge> parse_build_edge(
        const std::vector<std::string> &lines,
        std::size_t &index,
        const fs::path &base)
    {
      std::string line = trim_copy(lines[index]);

      if (!starts_with(line, "build "))
      {
        ++index;
        return std::nullopt;
      }

      line = trim_copy(line.substr(6));

      const auto colon = line.find(':');
      if (colon == std::string::npos)
      {
        ++index;
        return std::nullopt;
      }

      const std::string outputsPart = trim_copy(line.substr(0, colon));
      std::string rest = trim_copy(line.substr(colon + 1));

      const auto restTokens = tokenize_ninja_paths(rest);
      if (restTokens.empty())
      {
        ++index;
        return std::nullopt;
      }

      NinjaEdge edge;
      edge.outputs = resolve_path_tokens(base, tokenize_ninja_paths(outputsPart));
      edge.rule = restTokens.front();

      enum class Section
      {
        Explicit,
        Implicit,
        OrderOnly
      };

      Section section = Section::Explicit;

      for (std::size_t i = 1; i < restTokens.size(); ++i)
      {
        const std::string &token = restTokens[i];

        if (token == "|")
        {
          section = Section::Implicit;
          continue;
        }

        if (token == "||")
        {
          section = Section::OrderOnly;
          continue;
        }

        const fs::path resolved = resolve_ninja_path(base, fs::path(token));

        if (section == Section::Explicit)
          edge.explicitInputs.push_back(resolved);
        else if (section == Section::Implicit)
          edge.implicitInputs.push_back(resolved);
        else
          edge.orderOnlyInputs.push_back(resolved);
      }

      ++index;

      while (index < lines.size())
      {
        const std::string &raw = lines[index];

        if (!is_indented_binding(raw))
          break;

        const std::string binding = trim_copy(raw);
        const auto assignment = parse_assignment(binding);

        if (assignment)
          edge.variables[assignment->first] = assignment->second;

        ++index;
      }

      return edge;
    }

    static std::string lower_copy(std::string value)
    {
      for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return value;
    }

    static bool contains_any(
        const std::string &value,
        const std::vector<std::string> &needles)
    {
      for (const std::string &needle : needles)
      {
        if (value.find(needle) != std::string::npos)
          return true;
      }

      return false;
    }

    static std::string variable_value(
        const NinjaEdge &edge,
        const NinjaRule *rule,
        const std::string &name)
    {
      const auto edgeIt = edge.variables.find(name);
      if (edgeIt != edge.variables.end())
        return edgeIt->second;

      if (rule)
      {
        const auto ruleIt = rule->variables.find(name);
        if (ruleIt != rule->variables.end())
          return ruleIt->second;
      }

      return "";
    }

    static bool output_has_extension(
        const NinjaEdge &edge,
        const std::vector<std::string> &extensions)
    {
      for (const auto &output : edge.outputs)
      {
        const std::string ext = lower_copy(output.extension().string());

        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
          return true;
      }

      return false;
    }

    static bool output_looks_executable(const NinjaEdge &edge)
    {
      if (edge.outputs.empty())
        return false;

      for (const fs::path &output : edge.outputs)
      {
        const std::string name = output.filename().string();

        if (name.empty())
          continue;

        if (output.extension() == ".a" ||
            output.extension() == ".so" ||
            output.extension() == ".dylib" ||
            output.extension() == ".dll" ||
            output.extension() == ".o" ||
            output.extension() == ".obj")
        {
          continue;
        }

        return true;
      }

      return false;
    }
  } // namespace

  bool NinjaRule::valid() const
  {
    return !name.empty();
  }

  bool NinjaEdge::valid() const
  {
    return !outputs.empty() && !rule.empty();
  }

  fs::path NinjaEdge::primary_output() const
  {
    if (outputs.empty())
      return {};

    return outputs.front();
  }

  bool NinjaBuildFile::valid() const
  {
    return !path.empty() && !directory.empty();
  }

  std::string to_string(NinjaEdgeKind kind)
  {
    switch (kind)
    {
    case NinjaEdgeKind::Compile:
      return "compile";
    case NinjaEdgeKind::Archive:
      return "archive";
    case NinjaEdgeKind::Link:
      return "link";
    case NinjaEdgeKind::Copy:
      return "copy";
    case NinjaEdgeKind::Install:
      return "install";
    case NinjaEdgeKind::Utility:
      return "utility";
    case NinjaEdgeKind::Unknown:
    default:
      return "unknown";
    }
  }

  NinjaEdgeKind classify_ninja_edge(
      const NinjaEdge &edge,
      const NinjaRule *rule)
  {
    const std::string ruleName = lower_copy(edge.rule);
    const std::string command = lower_copy(variable_value(edge, rule, "command"));
    const std::string description = lower_copy(variable_value(edge, rule, "description"));

    const std::string joined = ruleName + " " + command + " " + description;

    if (contains_any(joined, {"cmake_symlink", "copy", "copy_file", "copy_directory"}))
      return NinjaEdgeKind::Copy;

    if (contains_any(joined, {"install"}))
      return NinjaEdgeKind::Install;

    if (output_has_extension(edge, {".o", ".obj"}))
      return NinjaEdgeKind::Compile;

    if (output_has_extension(edge, {".a", ".lib"}))
      return NinjaEdgeKind::Archive;

    if (output_has_extension(edge, {".so", ".dylib", ".dll"}))
      return NinjaEdgeKind::Link;

    if (output_looks_executable(edge) &&
        contains_any(joined, {"link", "cxx_executable", "c_executable"}))
      return NinjaEdgeKind::Link;

    if (contains_any(joined, {"phony", "utility"}))
      return NinjaEdgeKind::Utility;

    return NinjaEdgeKind::Unknown;
  }

  fs::path default_build_ninja_path(const fs::path &buildDir)
  {
    return buildDir / "build.ninja";
  }

  std::optional<NinjaBuildFile> parse_build_ninja_text(
      const std::string &text,
      const fs::path &path)
  {
    if (text.empty())
      return std::nullopt;

    NinjaBuildFile file;
    file.path = path.empty()
                    ? fs::path("build.ninja")
                    : fs::absolute(path).lexically_normal();
    file.directory = ninja_file_directory(file.path);

    const std::string joined = join_continuations(text);
    const std::vector<std::string> lines = split_lines(joined);

    std::size_t index = 0;

    while (index < lines.size())
    {
      const std::string raw = lines[index];
      const std::string line = trim_copy(raw);

      if (line.empty() || starts_with(line, "#"))
      {
        ++index;
        continue;
      }

      if (starts_with(line, "rule "))
      {
        const std::size_t before = index;
        const auto rule = parse_rule(lines, index);

        if (rule && rule->valid())
        {
          file.rules[rule->name] = *rule;
        }
        else if (index == before)
        {
          ++index;
        }

        continue;
      }

      if (starts_with(line, "build "))
      {
        const std::size_t before = index;
        const auto edge = parse_build_edge(lines, index, file.directory);

        if (edge && edge->valid())
        {
          NinjaEdge resolved = *edge;

          const auto ruleIt = file.rules.find(resolved.rule);
          const NinjaRule *rule =
              ruleIt == file.rules.end() ? nullptr : &ruleIt->second;

          resolved.kind = classify_ninja_edge(resolved, rule);
          file.edges.push_back(std::move(resolved));
        }
        else if (index == before)
        {
          ++index;
        }

        continue;
      }

      const auto assignment = parse_assignment(line);
      if (assignment)
        file.variables[assignment->first] = assignment->second;

      ++index;
    }

    return file;
  }

  std::optional<NinjaBuildFile> read_build_ninja(const fs::path &path)
  {
    if (!file_exists_regular(path))
      return std::nullopt;

    return parse_build_ninja_text(
        read_text_file_or_empty(path),
        path);
  }

  fs::path resolve_ninja_path(
      const fs::path &base,
      const fs::path &path)
  {
    if (path.empty())
      return {};

    if (path.is_absolute())
      return path.lexically_normal();

    fs::path root = base;

    if (root.empty())
      root = fs::current_path();

    return fs::absolute(root / path).lexically_normal();
  }

} // namespace vix::cli::build
