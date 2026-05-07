/**
 *
 *  @file CompileCommands.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  compile_commands.json parser
 *
 */

#include <vix/cli/build/CompileCommands.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

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

    static fs::path normalize_path(const fs::path &path)
    {
      return path.lexically_normal();
    }

    static fs::path compile_database_directory(const fs::path &sourcePath)
    {
      if (sourcePath.empty())
        return fs::current_path();

      const fs::path parent = sourcePath.parent_path();

      if (parent.empty())
        return fs::current_path();

      return fs::absolute(parent).lexically_normal();
    }

    static bool json_string_value(
        const nlohmann::json &object,
        const char *key,
        std::string &out)
    {
      if (!object.contains(key))
        return false;

      const auto &value = object.at(key);

      if (!value.is_string())
        return false;

      out = value.get<std::string>();
      return true;
    }

    static bool json_arguments_value(
        const nlohmann::json &object,
        std::vector<std::string> &out)
    {
      if (!object.contains("arguments"))
        return false;

      const auto &value = object.at("arguments");

      if (!value.is_array())
        return false;

      std::vector<std::string> args;
      args.reserve(value.size());

      for (const auto &item : value)
      {
        if (!item.is_string())
          return false;

        args.push_back(item.get<std::string>());
      }

      out = std::move(args);
      return true;
    }

    static bool is_space(char c)
    {
      return c == ' ' ||
             c == '\t' ||
             c == '\n' ||
             c == '\r' ||
             c == '\f' ||
             c == '\v';
    }

    static void push_token_if_not_empty(
        std::vector<std::string> &tokens,
        std::string &current)
    {
      if (current.empty())
        return;

      tokens.push_back(current);
      current.clear();
    }

    static fs::path source_path_from_entry(
        const nlohmann::json &object,
        const fs::path &directory)
    {
      std::string file;

      if (!json_string_value(object, "file", file))
        return {};

      if (file.empty())
        return {};

      return resolve_compile_command_path(directory, fs::path(file));
    }

    static fs::path output_path_from_entry(
        const nlohmann::json &object,
        const std::vector<std::string> &arguments,
        const fs::path &directory)
    {
      std::string output;

      if (json_string_value(object, "output", output) && !output.empty())
        return resolve_compile_command_path(directory, fs::path(output));

      return extract_compile_output_path(arguments, directory);
    }

    static CompileCommandEntry parse_one_entry(
        const nlohmann::json &object,
        const fs::path &databaseDir)
    {
      CompileCommandEntry entry;

      if (!object.is_object())
        return entry;

      std::string directory;

      if (json_string_value(object, "directory", directory) && !directory.empty())
        entry.directory = resolve_compile_command_path(databaseDir, fs::path(directory));
      else
        entry.directory = databaseDir;

      if (!json_arguments_value(object, entry.arguments))
      {
        std::string command;

        if (!json_string_value(object, "command", command))
          return entry;

        entry.rawCommand = command;
        entry.arguments = split_compile_command(command);
      }

      entry.source = source_path_from_entry(object, entry.directory);
      entry.output = output_path_from_entry(object, entry.arguments, entry.directory);

      if (!entry.output.empty())
        entry.output = resolve_compile_command_path(entry.directory, entry.output);

      return entry;
    }
  } // namespace

  bool CompileCommandEntry::valid() const
  {
    return !directory.empty() &&
           !source.empty() &&
           !arguments.empty();
  }

  bool CompileCommandEntry::has_output() const
  {
    return !output.empty();
  }

  fs::path default_compile_commands_path(const fs::path &buildDir)
  {
    return buildDir / "compile_commands.json";
  }

  std::vector<std::string> split_compile_command(const std::string &command)
  {
    std::vector<std::string> tokens;
    std::string current;

    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool escaped = false;

    for (char c : command)
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

      if (inSingleQuote)
      {
        if (c == '\'')
          inSingleQuote = false;
        else
          current.push_back(c);

        continue;
      }

      if (inDoubleQuote)
      {
        if (c == '"')
          inDoubleQuote = false;
        else
          current.push_back(c);

        continue;
      }

      if (c == '\'')
      {
        inSingleQuote = true;
        continue;
      }

      if (c == '"')
      {
        inDoubleQuote = true;
        continue;
      }

      if (is_space(c))
      {
        push_token_if_not_empty(tokens, current);
        continue;
      }

      current.push_back(c);
    }

    if (escaped)
      current.push_back('\\');

    push_token_if_not_empty(tokens, current);

    return tokens;
  }

  std::optional<std::vector<CompileCommandEntry>> parse_compile_commands_text(
      const std::string &text,
      const fs::path &sourcePath)
  {
    if (text.empty())
      return std::nullopt;

    nlohmann::json root;

    try
    {
      root = nlohmann::json::parse(text);
    }
    catch (...)
    {
      return std::nullopt;
    }

    if (!root.is_array())
      return std::nullopt;

    const fs::path databaseDir = compile_database_directory(sourcePath);

    std::vector<CompileCommandEntry> entries;
    entries.reserve(root.size());

    for (const auto &item : root)
    {
      CompileCommandEntry entry = parse_one_entry(item, databaseDir);

      if (!entry.valid())
        continue;

      entries.push_back(std::move(entry));
    }

    return entries;
  }

  std::optional<std::vector<CompileCommandEntry>> read_compile_commands(
      const fs::path &path)
  {
    if (!file_exists_regular(path))
      return std::nullopt;

    return parse_compile_commands_text(
        read_text_file_or_empty(path),
        path);
  }

  fs::path resolve_compile_command_path(
      const fs::path &base,
      const fs::path &path)
  {
    if (path.empty())
      return {};

    if (path.is_absolute())
      return normalize_path(path);

    fs::path root = base;

    if (root.empty())
      root = fs::current_path();

    return fs::absolute(root / path).lexically_normal();
  }

  fs::path extract_compile_output_path(
      const std::vector<std::string> &arguments,
      const fs::path &workingDirectory)
  {
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
      const std::string &arg = arguments[i];

      if (arg == "-o")
      {
        if (i + 1 >= arguments.size())
          return {};

        return resolve_compile_command_path(
            workingDirectory,
            fs::path(arguments[i + 1]));
      }

      if (arg.rfind("-o", 0) == 0 && arg.size() > 2)
      {
        return resolve_compile_command_path(
            workingDirectory,
            fs::path(arg.substr(2)));
      }
    }

    return {};
  }

} // namespace vix::cli::build
