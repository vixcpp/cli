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
#include <unordered_set>

namespace vix::cli::app
{
  namespace
  {
    // ---------------------------------------------------------------
    // String helpers
    // ---------------------------------------------------------------

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

    static std::vector<std::string> split_by_comma(const std::string &value)
    {
      std::vector<std::string> out;
      std::string current;

      for (char c : value)
      {
        if (c == ',')
        {
          const std::string item = trim_copy(current);
          if (!item.empty())
            out.push_back(item);
          current.clear();
        }
        else
        {
          current.push_back(c);
        }
      }

      const std::string item = trim_copy(current);
      if (!item.empty())
        out.push_back(item);

      return out;
    }

    static bool is_valid_module_name(const std::string &name)
    {
      if (name.empty())
        return false;

      for (char c : name)
      {
        const unsigned char uc = static_cast<unsigned char>(c);

        if (!std::isalnum(uc) && c != '_' && c != '-')
          return false;
      }

      return true;
    }

    static std::string normalize_module_id(std::string name)
    {
      name = trim_copy(name);

      for (char &c : name)
      {
        if (c == '-')
          c = '_';
      }

      return name;
    }

    static bool parse_bool_value(
        const std::string &raw,
        bool &out)
    {
      const std::string value = lower_copy(strip_quotes(raw));

      if (value == "true" || value == "yes" || value == "on" || value == "1")
      {
        out = true;
        return true;
      }

      if (value == "false" || value == "no" || value == "off" || value == "0")
      {
        out = false;
        return true;
      }

      return false;
    }

    static bool parse_module_section_header(
        const std::string &line,
        std::string &moduleName)
    {
      const std::string value = trim_copy(line);

      if (value.size() <= std::string("[module.]").size())
        return false;

      if (!starts_with(value, "[module."))
        return false;

      if (!ends_with(value, "]"))
        return false;

      const std::string rawName =
          value.substr(
              std::string("[module.").size(),
              value.size() - std::string("[module.").size() - 1);

      moduleName = normalize_module_id(rawName);

      return is_valid_module_name(moduleName);
    }

    static AppModule *find_app_module(
        AppManifest &manifest,
        const std::string &name)
    {
      for (AppModule &module : manifest.appModules)
      {
        if (module.name == name)
          return &module;
      }

      return nullptr;
    }

    static AppModule &ensure_app_module(
        AppManifest &manifest,
        const std::string &name)
    {
      if (AppModule *existing = find_app_module(manifest, name))
        return *existing;

      AppModule module;
      module.name = name;
      module.enabled = true;
      module.path = "modules/" + name;
      module.kind.clear();

      manifest.appModules.push_back(std::move(module));

      return manifest.appModules.back();
    }

    static bool parse_git_dependency_section_header(
        const std::string &line,
        std::string &dependencyName,
        bool &cmakeOptionsSection)
    {
      const std::string value = trim_copy(line);
      cmakeOptionsSection = false;

      if (!starts_with(value, "[dependencies.") || !ends_with(value, "]"))
        return false;

      std::string inner = value.substr(
          std::string("[dependencies.").size(),
          value.size() - std::string("[dependencies.").size() - 1);

      if (ends_with(inner, ".cmake"))
      {
        cmakeOptionsSection = true;
        inner = inner.substr(0, inner.size() - std::string(".cmake").size());
      }

      dependencyName = trim_copy(inner);
      return is_valid_module_name(dependencyName);
    }

    static AppGitDependency *find_git_dependency(
        AppManifest &manifest,
        const std::string &name)
    {
      for (AppGitDependency &dependency : manifest.gitDependencies)
      {
        if (dependency.name == name)
          return &dependency;
      }

      return nullptr;
    }

    static AppGitDependency &ensure_git_dependency(
        AppManifest &manifest,
        const std::string &name)
    {
      if (AppGitDependency *existing = find_git_dependency(manifest, name))
        return *existing;

      AppGitDependency dependency;
      dependency.name = name;
      manifest.gitDependencies.push_back(std::move(dependency));
      return manifest.gitDependencies.back();
    }

    // ---------------------------------------------------------------
    // Line preprocessing
    // ---------------------------------------------------------------

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

    // ---------------------------------------------------------------
    // Target name validation
    //
    // Accepts identifiers composed of letters, digits, '_' and '-'.
    // The first character must be a letter or '_' to stay compatible
    // with both CMake target names and typical executable/file names.
    // ---------------------------------------------------------------

    static bool is_valid_target_name(const std::string &name)
    {
      if (name.empty())
        return false;

      const unsigned char first = static_cast<unsigned char>(name.front());

      if (!std::isalpha(first) && first != '_')
        return false;

      for (char c : name)
      {
        const unsigned char uc = static_cast<unsigned char>(c);

        if (!std::isalnum(uc) && c != '_' && c != '-')
          return false;
      }

      return true;
    }

    // ---------------------------------------------------------------
    // Standard validation
    // ---------------------------------------------------------------

    static bool is_known_standard(const std::string &value)
    {
      static const std::unordered_set<std::string> known = {
          "c++11", "cpp11", "11",
          "c++14", "cpp14", "14",
          "c++17", "cpp17", "17",
          "c++20", "cpp20", "20",
          "c++23", "cpp23", "23",
          "c++26", "cpp26", "26"};

      return known.find(lower_copy(value)) != known.end();
    }

    // ---------------------------------------------------------------
    // Package parsing
    //
    // Accepted forms (case-insensitive for the option keywords):
    //   "fmt"
    //   "fmt:REQUIRED"
    //   "Boost:COMPONENTS=system,filesystem"
    //   "Boost:COMPONENTS=system,filesystem:REQUIRED"
    //   "Boost:REQUIRED:COMPONENTS=system"
    // ---------------------------------------------------------------

    static bool parse_package_spec(
        const std::string &raw,
        AppPackage &out,
        std::string &error)
    {
      const std::string spec = trim_copy(raw);

      if (spec.empty())
      {
        error = "empty package specification";
        return false;
      }

      const std::vector<std::string> parts = [&spec]()
      {
        std::vector<std::string> result;
        std::string current;

        for (char c : spec)
        {
          if (c == ':')
          {
            result.push_back(current);
            current.clear();
          }
          else
          {
            current.push_back(c);
          }
        }

        result.push_back(current);
        return result;
      }();

      out.name = trim_copy(parts.front());

      if (out.name.empty())
      {
        error = "package specification has an empty name: " + raw;
        return false;
      }

      for (std::size_t i = 1; i < parts.size(); ++i)
      {
        const std::string segment = trim_copy(parts[i]);

        if (segment.empty())
        {
          error = "empty option in package specification: " + raw;
          return false;
        }

        const std::string lowered = lower_copy(segment);

        if (lowered == "required")
        {
          out.required = true;
          continue;
        }

        if (starts_with(lowered, "components"))
        {
          const auto eq = segment.find('=');

          if (eq == std::string::npos)
          {
            error = "invalid 'components' clause in package specification: " + raw;
            return false;
          }

          const std::string list = segment.substr(eq + 1);
          out.components = split_by_comma(list);

          if (out.components.empty())
          {
            error = "package '" + out.name +
                    "' declares an empty components list";
            return false;
          }

          continue;
        }

        error = "unknown package option '" + segment +
                "' in: " + raw;
        return false;
      }

      return true;
    }

    static bool parse_packages(
        const std::vector<std::string> &values,
        std::vector<AppPackage> &out,
        std::string &error)
    {
      out.clear();
      out.reserve(values.size());

      for (const std::string &value : values)
      {
        AppPackage pkg;

        if (!parse_package_spec(value, pkg, error))
          return false;

        out.push_back(std::move(pkg));
      }

      return true;
    }

    // ---------------------------------------------------------------
    // Resource parsing
    //
    // Accepted forms:
    //   "assets"               -> source = assets,      destination = ""
    //   "data/icon.png=icon"   -> source = data/icon.png, destination = icon
    // ---------------------------------------------------------------

    static bool parse_resource_spec(
        const std::string &raw,
        AppResource &out,
        std::string &error)
    {
      const std::string spec = trim_copy(raw);

      if (spec.empty())
      {
        error = "empty resource specification";
        return false;
      }

      const auto eq = spec.find('=');

      if (eq == std::string::npos)
      {
        out.source = spec;
        out.destination.clear();
        return true;
      }

      out.source = trim_copy(spec.substr(0, eq));
      out.destination = trim_copy(spec.substr(eq + 1));

      if (out.source.empty())
      {
        error = "resource specification has an empty source: " + raw;
        return false;
      }

      return true;
    }

    static bool parse_resources(
        const std::vector<std::string> &values,
        std::vector<AppResource> &out,
        std::string &error)
    {
      out.clear();
      out.reserve(values.size());

      for (const std::string &value : values)
      {
        AppResource res;

        if (!parse_resource_spec(value, res, error))
          return false;

        out.push_back(std::move(res));
      }

      return true;
    }

    // ---------------------------------------------------------------
    // Field assignment
    // ---------------------------------------------------------------

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
        if (!is_valid_target_name(normalizedValue))
        {
          error = "Invalid vix.app target name: '" + normalizedValue +
                  "'. Allowed characters: letters, digits, '_' and '-'. "
                  "The first character must be a letter or '_'.";
          return false;
        }

        manifest.name = normalizedValue;
        return true;
      }

      if (normalizedKey == "type")
      {
        const std::string loweredValue = lower_copy(normalizedValue);

        if (loweredValue == "backend")
        {
          manifest.appKind = "backend";
          manifest.type = AppTargetType::Executable;
          return true;
        }

        const auto parsed = app_target_type_from_string(normalizedValue);

        if (!parsed)
        {
          error = "Invalid vix.app target type: '" + normalizedValue +
                  "'. Expected one of: executable, static, shared, library, backend.";
          return false;
        }

        manifest.type = *parsed;
        return true;
      }

      if (normalizedKey == "kind" ||
          normalizedKey == "app_kind" ||
          normalizedKey == "appkind")
      {
        if (normalizedValue.empty())
        {
          error = "Invalid empty app kind in vix.app.";
          return false;
        }

        manifest.appKind = normalizedValue;
        return true;
      }

      if (normalizedKey == "standard")
      {
        if (!is_known_standard(normalizedValue))
        {
          error = "Invalid or unsupported C++ standard in vix.app: '" +
                  normalizedValue +
                  "'. Expected one of: c++11, c++14, c++17, c++20, c++23, c++26.";
          return false;
        }

        manifest.standard = normalizedValue;
        return true;
      }

      if (normalizedKey == "output_dir" ||
          normalizedKey == "outputdir" ||
          normalizedKey == "output")
      {
        manifest.outputDir = normalizedValue;
        return true;
      }

      error = "Unknown scalar field in vix.app: '" + key + "'";
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

      if (normalizedKey == "modules")
      {
        manifest.modules = values;
        return true;
      }

      if (normalizedKey == "deps" ||
          normalizedKey == "dependencies")
      {
        manifest.deps = values;
        return true;
      }

      if (normalizedKey == "compile_options" ||
          normalizedKey == "compileoptions" ||
          normalizedKey == "cxxflags")
      {
        manifest.compileOptions = values;
        return true;
      }

      if (normalizedKey == "link_options" ||
          normalizedKey == "linkoptions" ||
          normalizedKey == "ldflags")
      {
        manifest.linkOptions = values;
        return true;
      }

      if (normalizedKey == "compile_features" ||
          normalizedKey == "compilefeatures" ||
          normalizedKey == "features")
      {
        manifest.compileFeatures = values;
        return true;
      }

      if (normalizedKey == "packages")
      {
        std::string parseError;

        if (!parse_packages(values, manifest.packages, parseError))
        {
          error = "Invalid package syntax in vix.app: " + parseError;
          return false;
        }

        return true;
      }

      if (normalizedKey == "resources")
      {
        std::string parseError;

        if (!parse_resources(values, manifest.resources, parseError))
        {
          error = "Invalid resource syntax in vix.app: " + parseError;
          return false;
        }

        return true;
      }

      error = "Unknown array field in vix.app: '" + key + "'";
      return false;
    }

    static bool assign_module_scalar(
        AppModule &module,
        const std::string &key,
        const std::string &value,
        std::string &error)
    {
      const std::string normalizedKey = lower_copy(trim_copy(key));
      const std::string normalizedValue = strip_quotes(value);

      if (normalizedKey == "enabled")
      {
        bool parsed = true;

        if (!parse_bool_value(normalizedValue, parsed))
        {
          error = "Invalid boolean value for module '" +
                  module.name +
                  "': " +
                  normalizedValue;
          return false;
        }

        module.enabled = parsed;
        return true;
      }

      if (normalizedKey == "path")
      {
        if (normalizedValue.empty())
        {
          error = "Module '" + module.name + "' has an empty path";
          return false;
        }

        module.path = normalizedValue;
        return true;
      }

      if (normalizedKey == "kind")
      {
        if (normalizedValue.empty())
        {
          error = "Module '" + module.name + "' has an empty kind";
          return false;
        }

        module.kind = normalizedValue;
        return true;
      }

      if (normalizedKey == "depends" ||
          normalizedKey == "deps" ||
          normalizedKey == "dependencies")
      {
        module.depends.clear();

        for (std::string dep : split_by_comma(normalizedValue))
        {
          dep = normalize_module_id(dep);

          if (!is_valid_module_name(dep))
          {
            error = "Invalid dependency name in module '" +
                    module.name +
                    "': " +
                    dep;
            return false;
          }

          module.depends.push_back(dep);
        }

        return true;
      }

      error = "Unknown field in [module." +
              module.name +
              "]: '" +
              key +
              "'";

      return false;
    }

    static bool assign_module_array(
        AppModule &module,
        const std::string &key,
        const std::vector<std::string> &values,
        std::string &error)
    {
      const std::string normalizedKey = lower_copy(trim_copy(key));

      if (normalizedKey == "depends" ||
          normalizedKey == "deps" ||
          normalizedKey == "dependencies")
      {
        module.depends.clear();

        for (std::string dep : values)
        {
          dep = normalize_module_id(dep);

          if (!is_valid_module_name(dep))
          {
            error = "Invalid dependency name in module '" +
                    module.name +
                    "': " +
                    dep;
            return false;
          }

          module.depends.push_back(dep);
        }

        return true;
      }

      error = "Unknown array field in [module." +
              module.name +
              "]: '" +
              key +
              "'";

      return false;
    }

    static bool assign_git_dependency_scalar(
        AppGitDependency &dependency,
        const std::string &key,
        const std::string &value,
        std::string &error)
    {
      const std::string normalizedKey = lower_copy(trim_copy(key));
      const std::string normalizedValue = strip_quotes(value);

      if (normalizedKey == "git")
      {
        dependency.git = normalizedValue;
        return true;
      }
      if (normalizedKey == "tag")
      {
        dependency.tag = normalizedValue;
        return true;
      }
      if (normalizedKey == "branch")
      {
        dependency.branch = normalizedValue;
        return true;
      }
      if (normalizedKey == "rev" || normalizedKey == "commit")
      {
        dependency.rev = normalizedValue;
        return true;
      }
      if (normalizedKey == "subdirectory" || normalizedKey == "subdir")
      {
        dependency.subdirectory = normalizedValue;
        return true;
      }
      if (normalizedKey == "target")
      {
        dependency.target = normalizedValue;
        return true;
      }
      if (normalizedKey == "header_only" || normalizedKey == "header-only" || normalizedKey == "headers")
      {
        bool parsed = false;
        if (!parse_bool_value(normalizedValue, parsed))
        {
          error = "Invalid boolean value for dependency '" + dependency.name + "': " + normalizedValue;
          return false;
        }
        dependency.headerOnly = parsed;
        return true;
      }
      if (normalizedKey == "include")
      {
        dependency.include = normalizedValue;
        return true;
      }

      error = "Unknown field in [dependencies." + dependency.name + "]: '" + key + "'";
      return false;
    }

    static bool assign_git_dependency_array(
        AppGitDependency &dependency,
        const std::string &key,
        const std::vector<std::string> &values,
        std::string &error)
    {
      const std::string normalizedKey = lower_copy(trim_copy(key));

      if (normalizedKey == "targets")
      {
        dependency.targets = values;
        return true;
      }
      if (normalizedKey == "includes" || normalizedKey == "include_dirs")
      {
        dependency.includes = values;
        return true;
      }
      if (normalizedKey == "cmake_options" || normalizedKey == "cmake-options")
      {
        dependency.cmakeOptions.clear();
        for (const std::string &item : values)
        {
          const auto eq = item.find('=');
          if (eq == std::string::npos)
          {
            error = "Invalid cmake_options entry for dependency '" + dependency.name + "': " + item;
            return false;
          }
          dependency.cmakeOptions.emplace_back(trim_copy(item.substr(0, eq)), trim_copy(item.substr(eq + 1)));
        }
        return true;
      }

      error = "Unknown array field in [dependencies." + dependency.name + "]: '" + key + "'";
      return false;
    }

    static bool assign_git_cmake_option(
        AppGitDependency &dependency,
        const std::string &key,
        const std::string &value,
        std::string &error)
    {
      const std::string option = trim_copy(key);
      if (option.empty())
      {
        error = "Empty CMake option name in [dependencies." + dependency.name + ".cmake]";
        return false;
      }

      dependency.cmakeOptions.emplace_back(option, strip_quotes(value));
      return true;
    }

    // ---------------------------------------------------------------
    // Top-level parser
    // ---------------------------------------------------------------

    static std::string format_line_error(
        std::size_t lineNumber,
        const std::string &reason)
    {
      return "Invalid vix.app syntax at line " +
             std::to_string(lineNumber) + ": " + reason;
    }

    static bool finalize_git_dependencies(
        AppManifest &manifest,
        std::string &error)
    {
      std::unordered_set<std::string> seen;

      for (AppGitDependency &dependency : manifest.gitDependencies)
      {
        dependency.name = trim_copy(dependency.name);
        if (!is_valid_target_name(dependency.name))
        {
          error = "Invalid dependency name in vix.app: " + dependency.name;
          return false;
        }

        if (seen.find(dependency.name) != seen.end())
        {
          error = "Duplicate dependency declaration in vix.app: " + dependency.name;
          return false;
        }
        seen.insert(dependency.name);

        if (trim_copy(dependency.git).empty())
        {
          error = "Git dependency '" + dependency.name + "' is missing field 'git'";
          return false;
        }

        int revisionCount = 0;
        if (!trim_copy(dependency.tag).empty())
          ++revisionCount;
        if (!trim_copy(dependency.branch).empty())
          ++revisionCount;
        if (!trim_copy(dependency.rev).empty())
          ++revisionCount;

        if (revisionCount > 1)
        {
          error = "Git dependency '" + dependency.name + "' must use only one of tag, branch, or rev";
          return false;
        }

        if (!dependency.target.empty() &&
            std::find(dependency.targets.begin(), dependency.targets.end(), dependency.target) == dependency.targets.end())
          dependency.targets.insert(dependency.targets.begin(), dependency.target);

        if (!dependency.include.empty() &&
            std::find(dependency.includes.begin(), dependency.includes.end(), dependency.include) == dependency.includes.end())
          dependency.includes.insert(dependency.includes.begin(), dependency.include);

        if (dependency.headerOnly && dependency.includes.empty())
          dependency.includes.push_back("include");
      }

      return true;
    }

    static bool finalize_app_modules(
        AppManifest &manifest,
        std::string &error)
    {
      if (manifest.appModules.empty())
        return true;

      std::unordered_set<std::string> seen;

      manifest.modules.clear();

      for (AppModule &module : manifest.appModules)
      {
        module.name = normalize_module_id(module.name);

        if (!is_valid_module_name(module.name))
        {
          error = "Invalid module name in vix.app: " + module.name;
          return false;
        }

        if (seen.find(module.name) != seen.end())
        {
          error = "Duplicate module declaration in vix.app: " + module.name;
          return false;
        }

        seen.insert(module.name);

        if (module.path.empty())
          module.path = "modules/" + module.name;

        if (module.kind.empty())
          module.kind = manifest.appKind == "backend" ? "backend" : "module";

        for (std::string &dep : module.depends)
        {
          dep = normalize_module_id(dep);

          if (!is_valid_module_name(dep))
          {
            error = "Invalid dependency name in module '" +
                    module.name +
                    "': " +
                    dep;
            return false;
          }
        }

        if (module.enabled)
          manifest.modules.push_back(module.name);
      }

      return true;
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
      std::size_t activeArrayStartLine = 0;

      std::string activeModuleName;
      std::string activeGitDependencyName;
      bool activeGitCmakeOptions = false;

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
            if (!activeGitDependencyName.empty())
            {
              AppGitDependency &dependency =
                  ensure_git_dependency(manifest, activeGitDependencyName);

              if (activeGitCmakeOptions)
              {
                error = "Arrays are not supported in [dependencies." + activeGitDependencyName + ".cmake]";
                return false;
              }

              if (!assign_git_dependency_array(
                      dependency,
                      activeArrayKey,
                      activeArrayValues,
                      error))
              {
                return false;
              }
            }
            else if (!activeModuleName.empty())
            {
              AppModule &module =
                  ensure_app_module(manifest, activeModuleName);

              if (!assign_module_array(
                      module,
                      activeArrayKey,
                      activeArrayValues,
                      error))
              {
                return false;
              }
            }
            else
            {
              if (!assign_array(
                      manifest,
                      activeArrayKey,
                      activeArrayValues,
                      error))
              {
                return false;
              }
            }

            activeArrayKey.clear();
            activeArrayValues.clear();
            activeArrayStartLine = 0;
            continue;
          }

          if (ends_with(line, ","))
            line.pop_back();

          const std::string item = strip_quotes(trim_copy(line));

          if (!item.empty())
            activeArrayValues.push_back(item);

          continue;
        }

        std::string moduleSectionName;

        if (parse_module_section_header(line, moduleSectionName))
        {
          activeModuleName = moduleSectionName;
          activeGitDependencyName.clear();
          activeGitCmakeOptions = false;
          ensure_app_module(manifest, activeModuleName);
          continue;
        }

        std::string gitDependencySectionName;
        bool gitCmakeOptionsSection = false;

        if (parse_git_dependency_section_header(line, gitDependencySectionName, gitCmakeOptionsSection))
        {
          activeModuleName.clear();
          activeGitDependencyName = gitDependencySectionName;
          activeGitCmakeOptions = gitCmakeOptionsSection;
          ensure_git_dependency(manifest, activeGitDependencyName);
          continue;
        }

        if (starts_with(line, "[") && ends_with(line, "]"))
        {
          error = format_line_error(
              lineNumber,
              "unknown section '" + line + "'");
          return false;
        }

        const auto pos = line.find('=');

        if (pos == std::string::npos)
        {
          error = format_line_error(lineNumber, "expected 'key = value'");
          return false;
        }

        const std::string key = trim_copy(line.substr(0, pos));
        const std::string value = trim_copy(line.substr(pos + 1));

        if (key.empty())
        {
          error = format_line_error(lineNumber, "empty key");
          return false;
        }

        if (is_array_start(value))
        {
          activeArrayKey = key;
          activeArrayValues.clear();
          activeArrayStartLine = lineNumber;
          continue;
        }

        if (starts_with(value, "["))
        {
          if (!ends_with(value, "]"))
          {
            error = format_line_error(
                lineNumber,
                "malformed inline array, missing closing ']'");
            return false;
          }

          if (!activeGitDependencyName.empty())
          {
            AppGitDependency &dependency =
                ensure_git_dependency(manifest, activeGitDependencyName);

            if (activeGitCmakeOptions)
            {
              error = "Arrays are not supported in [dependencies." + activeGitDependencyName + ".cmake]";
              return false;
            }

            if (!assign_git_dependency_array(
                    dependency,
                    key,
                    parse_inline_array(value),
                    error))
            {
              return false;
            }
          }
          else if (!activeModuleName.empty())
          {
            AppModule &module =
                ensure_app_module(manifest, activeModuleName);

            if (!assign_module_array(
                    module,
                    key,
                    parse_inline_array(value),
                    error))
            {
              return false;
            }
          }
          else
          {
            if (!assign_array(
                    manifest,
                    key,
                    parse_inline_array(value),
                    error))
            {
              return false;
            }
          }

          continue;
        }

        if (!activeGitDependencyName.empty())
        {
          AppGitDependency &dependency =
              ensure_git_dependency(manifest, activeGitDependencyName);

          if (activeGitCmakeOptions)
          {
            if (!assign_git_cmake_option(dependency, key, value, error))
              return false;
          }
          else if (!assign_git_dependency_scalar(dependency, key, value, error))
          {
            return false;
          }
        }
        else if (!activeModuleName.empty())
        {
          AppModule &module =
              ensure_app_module(manifest, activeModuleName);

          if (!assign_module_scalar(module, key, value, error))
            return false;
        }
        else
        {
          if (!assign_scalar(manifest, key, value, error))
            return false;
        }
      }

      if (!activeArrayKey.empty())
      {
        error = "Invalid vix.app syntax: missing closing ']' for field '" +
                activeArrayKey + "' (opened at line " +
                std::to_string(activeArrayStartLine) + ")";
        return false;
      }

      if (manifest.name.empty())
      {
        error = "Invalid vix.app: missing required field 'name'";
        return false;
      }

      if (manifest.sources.empty())
      {
        error = "Invalid vix.app: missing required field 'sources' "
                "(at least one source file is required)";
        return false;
      }

      if (manifest.standard.empty())
        manifest.standard = "c++20";

      if (!finalize_git_dependencies(manifest, error))
        return false;

      if (!finalize_app_modules(manifest, error))
        return false;

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

  // -----------------------------------------------------------------
  // Public API
  // -----------------------------------------------------------------

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
