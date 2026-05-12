/**
 *
 *  @file AppManifest.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Simple application manifest parser for vix.app.
 *
 */

#include <vix/cli/app/AppManifest.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::cli::app
{
  namespace
  {
    static std::string trim_copy(const std::string &value)
    {
      std::size_t begin = 0;
      while (begin < value.size() &&
             std::isspace(static_cast<unsigned char>(value[begin])))
      {
        ++begin;
      }

      std::size_t end = value.size();
      while (end > begin &&
             std::isspace(static_cast<unsigned char>(value[end - 1])))
      {
        --end;
      }

      return value.substr(begin, end - begin);
    }

    static std::string lower_copy(std::string value)
    {
      for (char &c : value)
      {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
      }

      return value;
    }

    static bool starts_with(
        const std::string &value,
        const std::string &prefix)
    {
      return value.rfind(prefix, 0) == 0;
    }

    static bool ends_with(
        const std::string &value,
        const std::string &suffix)
    {
      if (suffix.size() > value.size())
        return false;

      return std::equal(
          suffix.rbegin(),
          suffix.rend(),
          value.rbegin());
    }

    static std::string strip_quotes(const std::string &value)
    {
      const std::string s = trim_copy(value);

      if (s.size() >= 2 &&
          ((s.front() == '"' && s.back() == '"') ||
           (s.front() == '\'' && s.back() == '\'')))
      {
        return s.substr(1, s.size() - 2);
      }

      return s;
    }

    static std::string strip_inline_comment(const std::string &line)
    {
      bool inSingle = false;
      bool inDouble = false;
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

        if (c == '\'' && !inDouble)
        {
          inSingle = !inSingle;
          continue;
        }

        if (c == '"' && !inSingle)
        {
          inDouble = !inDouble;
          continue;
        }

        if (c == '#' && !inSingle && !inDouble)
          return line.substr(0, i);
      }

      return line;
    }

    static bool is_array_start(const std::string &value)
    {
      return trim_copy(value) == "[";
    }

    static bool is_array_end(const std::string &value)
    {
      return trim_copy(value) == "]";
    }

    static std::vector<std::string> parse_inline_array(const std::string &value)
    {
      std::vector<std::string> out;

      std::string s = trim_copy(value);

      if (s.size() < 2 || s.front() != '[' || s.back() != ']')
        return out;

      s = s.substr(1, s.size() - 2);

      std::string current;
      bool inSingle = false;
      bool inDouble = false;
      bool escaped = false;

      for (char c : s)
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
          current.push_back(c);
          continue;
        }

        if (c == '\'' && !inDouble)
        {
          inSingle = !inSingle;
          current.push_back(c);
          continue;
        }

        if (c == '"' && !inSingle)
        {
          inDouble = !inDouble;
          current.push_back(c);
          continue;
        }

        if (c == ',' && !inSingle && !inDouble)
        {
          const std::string item = strip_quotes(trim_copy(current));

          if (!item.empty())
            out.push_back(item);

          current.clear();
          continue;
        }

        current.push_back(c);
      }

      const std::string item = strip_quotes(trim_copy(current));

      if (!item.empty())
        out.push_back(item);

      return out;
    }

    static bool assign_scalar(
        AppManifest &manifest,
        const std::string &key,
        const std::string &value,
        std::string &error)
    {
      const std::string normalizedKey = lower_copy(trim_copy(key));
      const std::string normalizedValue = strip_quotes(value);

      if (normalizedKey == "name")
      {
        manifest.name = normalizedValue;
        return true;
      }

      if (normalizedKey == "type")
      {
        const auto parsed = app_target_type_from_string(normalizedValue);

        if (!parsed)
        {
          error = "Invalid vix.app target type: " + normalizedValue;
          return false;
        }

        manifest.type = *parsed;
        return true;
      }

      if (normalizedKey == "standard")
      {
        manifest.standard = normalizedValue;
        return true;
      }

      error = "Unknown scalar field in vix.app: " + key;
      return false;
    }

    static bool assign_array(
        AppManifest &manifest,
        const std::string &key,
        const std::vector<std::string> &values,
        std::string &error)
    {
      const std::string normalizedKey = lower_copy(trim_copy(key));

      if (normalizedKey == "sources")
      {
        manifest.sources = values;
        return true;
      }

      if (normalizedKey == "include_dirs" ||
          normalizedKey == "includedirs" ||
          normalizedKey == "includes")
      {
        manifest.includeDirs = values;
        return true;
      }

      if (normalizedKey == "defines")
      {
        manifest.defines = values;
        return true;
      }

      if (normalizedKey == "links" ||
          normalizedKey == "libraries" ||
          normalizedKey == "libs")
      {
        manifest.links = values;
        return true;
      }

      error = "Unknown array field in vix.app: " + key;
      return false;
    }

    static bool parse_manifest_text(
        const std::string &text,
        AppManifest &manifest,
        std::string &error)
    {
      std::istringstream in(text);
      std::string line;

      std::string activeArrayKey;
      std::vector<std::string> activeArrayValues;

      std::size_t lineNumber = 0;

      while (std::getline(in, line))
      {
        ++lineNumber;

        line = trim_copy(strip_inline_comment(line));

        if (line.empty())
          continue;

        if (!activeArrayKey.empty())
        {
          if (is_array_end(line))
          {
            if (!assign_array(
                    manifest,
                    activeArrayKey,
                    activeArrayValues,
                    error))
            {
              return false;
            }

            activeArrayKey.clear();
            activeArrayValues.clear();
            continue;
          }

          if (ends_with(line, ","))
            line.pop_back();

          const std::string item = strip_quotes(trim_copy(line));

          if (!item.empty())
            activeArrayValues.push_back(item);

          continue;
        }

        const auto pos = line.find('=');

        if (pos == std::string::npos)
        {
          error = "Invalid vix.app syntax at line " +
                  std::to_string(lineNumber) +
                  ": expected key = value";
          return false;
        }

        const std::string key = trim_copy(line.substr(0, pos));
        const std::string value = trim_copy(line.substr(pos + 1));

        if (key.empty())
        {
          error = "Invalid vix.app syntax at line " +
                  std::to_string(lineNumber) +
                  ": empty key";
          return false;
        }

        if (is_array_start(value))
        {
          activeArrayKey = key;
          activeArrayValues.clear();
          continue;
        }

        if (starts_with(value, "["))
        {
          if (!assign_array(
                  manifest,
                  key,
                  parse_inline_array(value),
                  error))
          {
            return false;
          }

          continue;
        }

        if (!assign_scalar(manifest, key, value, error))
          return false;
      }

      if (!activeArrayKey.empty())
      {
        error = "Invalid vix.app syntax: missing closing ] for field " +
                activeArrayKey;
        return false;
      }

      if (manifest.name.empty())
      {
        error = "Invalid vix.app: missing required field 'name'";
        return false;
      }

      if (manifest.sources.empty())
      {
        error = "Invalid vix.app: missing required field 'sources'";
        return false;
      }

      if (manifest.standard.empty())
        manifest.standard = "c++20";

      return true;
    }

    static std::string read_text_file_or_empty(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};

      std::ostringstream out;
      out << in.rdbuf();
      return out.str();
    }
  } // namespace

  std::string to_string(AppTargetType type)
  {
    switch (type)
    {
    case AppTargetType::Executable:
      return "executable";
    case AppTargetType::StaticLibrary:
      return "static-library";
    case AppTargetType::SharedLibrary:
      return "shared-library";
    default:
      return "executable";
    }
  }

  std::optional<AppTargetType> app_target_type_from_string(
      const std::string &value)
  {
    const std::string type = lower_copy(trim_copy(value));

    if (type == "executable" || type == "exe" || type == "app")
      return AppTargetType::Executable;

    if (type == "static" ||
        type == "static-library" ||
        type == "static_library")
    {
      return AppTargetType::StaticLibrary;
    }

    if (type == "shared" ||
        type == "shared-library" ||
        type == "shared_library")
    {
      return AppTargetType::SharedLibrary;
    }

    if (type == "library" || type == "lib")
      return AppTargetType::StaticLibrary;

    return std::nullopt;
  }

  bool AppManifest::valid() const
  {
    return !name.empty() && !sources.empty();
  }

  bool AppManifestLoadResult::success() const
  {
    return error.empty() && manifest.valid();
  }

  AppManifestLoadResult load_app_manifest(const fs::path &path)
  {
    AppManifestLoadResult result;

    std::error_code ec;

    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec))
    {
      result.error = "vix.app file not found: " + path.string();
      return result;
    }

    const std::string text = read_text_file_or_empty(path);

    if (text.empty())
    {
      result.error = "vix.app is empty: " + path.string();
      return result;
    }

    if (!parse_manifest_text(text, result.manifest, result.error))
      return result;

    return result;
  }

} // namespace vix::cli::app
