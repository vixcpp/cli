/**
 *
 *  @file AddCommand.cpp
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
#include <vix/cli/commands/AddCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/util/Lockfile.hpp>
#include <vix/cli/util/Manifest.hpp>
#include <vix/cli/util/Resolver.hpp>
#include <vix/cli/util/Semver.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    struct PkgSpec
    {
      std::string ns;
      std::string name;
      std::string requestedVersion;
      std::string resolvedVersion;

      std::string id() const
      {
        return ns + "/" + name;
      }
    };

    struct AddOptions
    {
      std::string packageSpec;
      std::string moduleName;
      std::string linkTarget;
    };

    std::string home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    fs::path vix_root()
    {
      const std::string home = home_dir();
      if (home.empty())
      {
        return fs::path(".vix");
      }

      return fs::path(home) / ".vix";
    }

    fs::path registry_dir()
    {
      return vix_root() / "registry" / "index";
    }

    fs::path registry_index_dir()
    {
      return registry_dir() / "index";
    }

    fs::path manifest_path()
    {
      return fs::current_path() / "vix.json";
    }

    fs::path lock_path()
    {
      return fs::current_path() / "vix.lock";
    }

    std::string trim_copy(std::string s)
    {
      const auto isws = [](unsigned char c)
      {
        return std::isspace(c) != 0;
      };

      while (!s.empty() && isws(static_cast<unsigned char>(s.front())))
      {
        s.erase(s.begin());
      }

      while (!s.empty() && isws(static_cast<unsigned char>(s.back())))
      {
        s.pop_back();
      }

      return s;
    }

    std::string to_lower(std::string s)
    {
      std::transform(
          s.begin(),
          s.end(),
          s.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return s;
    }

    bool parse_pkg_spec(const std::string &rawInput, PkgSpec &out)
    {
      const std::string raw = trim_copy(rawInput);
      const auto slash = raw.find('/');

      if (slash == std::string::npos)
      {
        return false;
      }

      if (!raw.empty() && raw[0] == '@')
      {
        if (slash <= 1)
        {
          return false;
        }

        out.ns = trim_copy(raw.substr(1, slash - 1));
      }
      else
      {
        out.ns = trim_copy(raw.substr(0, slash));
      }

      const auto atVersion = raw.find('@', slash + 1);

      if (atVersion == std::string::npos)
      {
        out.name = trim_copy(raw.substr(slash + 1));
        out.requestedVersion.clear();
      }
      else
      {
        out.name = trim_copy(raw.substr(slash + 1, atVersion - (slash + 1)));
        out.requestedVersion = trim_copy(raw.substr(atVersion + 1));
      }

      out.resolvedVersion.clear();

      if (out.ns.empty() || out.name.empty())
      {
        return false;
      }

      if (atVersion != std::string::npos && out.requestedVersion.empty())
      {
        return false;
      }

      return true;
    }

    std::string normalize_module_id(std::string name)
    {
      name = trim_copy(std::move(name));

      for (char &c : name)
      {
        if (c == '-')
          c = '_';
      }

      return name;
    }

    bool is_valid_module_name(const std::string &name)
    {
      if (name.empty())
        return false;

      for (char c : name)
      {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' ||
            c == '-';

        if (!ok)
          return false;
      }

      return true;
    }

    std::string default_link_target(const PkgSpec &spec)
    {
      std::string ns = spec.ns;
      std::string name = spec.name;

      for (char &c : ns)
      {
        if (c == '-')
          c = '_';
      }

      for (char &c : name)
      {
        if (c == '-')
          c = '_';
      }

      return ns + "::" + name;
    }

    bool parse_add_options(
        const std::vector<std::string> &args,
        AddOptions &out,
        std::string &error)
    {
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (arg == "--module" || arg == "-m")
        {
          if (i + 1 >= args.size())
          {
            error = "missing value after " + arg;
            return false;
          }

          out.moduleName = args[++i];
          continue;
        }

        if (arg.rfind("--module=", 0) == 0)
        {
          out.moduleName = arg.substr(std::string("--module=").size());
          continue;
        }

        if (arg == "--link")
        {
          if (i + 1 >= args.size())
          {
            error = "missing value after --link";
            return false;
          }

          out.linkTarget = args[++i];
          continue;
        }

        if (arg.rfind("--link=", 0) == 0)
        {
          out.linkTarget = arg.substr(std::string("--link=").size());
          continue;
        }

        if (!arg.empty() && arg[0] == '-')
        {
          error = "unknown option: " + arg;
          return false;
        }

        if (!out.packageSpec.empty())
        {
          error = "too many package arguments";
          return false;
        }

        out.packageSpec = arg;
      }

      if (out.packageSpec.empty())
      {
        error = "missing package spec";
        return false;
      }

      out.moduleName = normalize_module_id(out.moduleName);
      out.linkTarget = trim_copy(out.linkTarget);

      if (!out.moduleName.empty() && !is_valid_module_name(out.moduleName))
      {
        error = "invalid module name: " + out.moduleName;
        return false;
      }

      return true;
    }

    fs::path module_manifest_path(const std::string &moduleName)
    {
      return fs::current_path() / "modules" / moduleName / "vix.module";
    }

    struct EnabledModule
    {
      std::string name;
      std::string path;
    };

    std::string strip_quotes_local(const std::string &value)
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

    bool parse_bool_value(const std::string &raw, bool fallback)
    {
      const std::string value = to_lower(strip_quotes_local(trim_copy(raw)));

      if (value == "true" || value == "yes" || value == "on" || value == "1")
        return true;

      if (value == "false" || value == "no" || value == "off" || value == "0")
        return false;

      return fallback;
    }

    std::vector<EnabledModule> read_enabled_modules_from_vix_app()
    {
      std::vector<EnabledModule> modules;

      const fs::path appPath = fs::current_path() / "vix.app";

      std::ifstream in(appPath);
      if (!in)
        return modules;

      std::string line;
      std::string currentName;
      std::string currentPath;
      bool currentEnabled = true;
      bool inModule = false;

      auto flush_current = [&]()
      {
        if (!inModule || currentName.empty())
          return;

        if (currentEnabled)
        {
          EnabledModule module;
          module.name = currentName;
          module.path = currentPath.empty()
                            ? ("modules/" + currentName)
                            : currentPath;
          modules.push_back(std::move(module));
        }
      };

      while (std::getline(in, line))
      {
        std::string s = trim_copy(line);

        const std::size_t comment = s.find('#');
        if (comment != std::string::npos)
          s = trim_copy(s.substr(0, comment));

        if (s.empty())
          continue;

        if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        {
          flush_current();

          currentName.clear();
          currentPath.clear();
          currentEnabled = true;
          inModule = false;

          const std::string section =
              trim_copy(s.substr(1, s.size() - 2));

          const std::string prefix = "module.";

          if (section.rfind(prefix, 0) == 0)
          {
            currentName = normalize_module_id(section.substr(prefix.size()));
            currentPath = "modules/" + currentName;
            currentEnabled = true;
            inModule = true;
          }

          continue;
        }

        if (!inModule)
          continue;

        const std::size_t eq = s.find('=');
        if (eq == std::string::npos)
          continue;

        const std::string key =
            to_lower(trim_copy(s.substr(0, eq)));

        const std::string value =
            strip_quotes_local(trim_copy(s.substr(eq + 1)));

        if (key == "enabled")
          currentEnabled = parse_bool_value(value, true);
        else if (key == "path")
          currentPath = value;
      }

      flush_current();

      return modules;
    }

    fs::path enabled_module_manifest_path(const EnabledModule &module)
    {
      fs::path path(module.path);

      if (!path.is_absolute())
        path = fs::current_path() / path;

      return path / "vix.module";
    }

    bool contains_string(
        const std::vector<std::string> &values,
        const std::string &needle)
    {
      return std::find(values.begin(), values.end(), needle) != values.end();
    }

    std::vector<std::string> parse_ini_array(
        const std::string &content,
        const std::string &section,
        const std::string &key)
    {
      std::vector<std::string> out;

      std::istringstream in(content);
      std::string line;
      std::string activeSection;
      bool collecting = false;

      while (std::getline(in, line))
      {
        std::string s = trim_copy(line);

        const std::size_t comment = s.find('#');
        if (comment != std::string::npos)
          s = trim_copy(s.substr(0, comment));

        if (s.empty())
          continue;

        if (!collecting && s.size() >= 2 && s.front() == '[' && s.back() == ']')
        {
          activeSection = trim_copy(s.substr(1, s.size() - 2));
          continue;
        }

        if (activeSection != section)
          continue;

        if (!collecting)
        {
          const std::size_t eq = s.find('=');

          if (eq == std::string::npos)
            continue;

          const std::string currentKey =
              trim_copy(s.substr(0, eq));

          if (currentKey != key)
            continue;

          s = trim_copy(s.substr(eq + 1));

          const std::size_t open = s.find('[');

          if (open == std::string::npos)
            continue;

          collecting = true;
          s = s.substr(open + 1);
        }

        const std::size_t close = s.find(']');

        if (close != std::string::npos)
        {
          s = s.substr(0, close);
          collecting = false;
        }

        std::istringstream items(s);
        std::string item;

        while (std::getline(items, item, ','))
        {
          item = strip_quotes_local(trim_copy(item));

          if (!item.empty())
            out.push_back(item);
        }
      }

      return out;
    }

    std::string render_string_array(
        const std::string &key,
        const std::vector<std::string> &values)
    {
      std::ostringstream out;

      out << key << " = [\n";

      for (const std::string &value : values)
        out << "  \"" << value << "\",\n";

      out << "]\n";

      return out.str();
    }

    std::string remove_section(
        const std::string &content,
        const std::string &section)
    {
      std::istringstream in(content);
      std::ostringstream out;

      std::string line;
      bool skipping = false;

      while (std::getline(in, line))
      {
        const std::string s = trim_copy(line);

        if (s.size() >= 2 && s.front() == '[' && s.back() == ']')
        {
          const std::string current =
              trim_copy(s.substr(1, s.size() - 2));

          skipping = current == section;

          if (skipping)
            continue;
        }

        if (!skipping)
          out << line << "\n";
      }

      return out.str();
    }

    bool upsert_module_dependency(
        const fs::path &path,
        const std::string &dependency,
        const std::string &linkTarget,
        const std::vector<std::string> &replacedPackageIds,
        std::string &error)
    {
      if (!fs::exists(path))
      {
        error = "module manifest not found: " + path.string();
        return false;
      }

      std::ifstream in(path, std::ios::binary);

      if (!in)
      {
        error = "cannot open module manifest: " + path.string();
        return false;
      }

      std::ostringstream buffer;
      buffer << in.rdbuf();

      const std::string original = buffer.str();

      std::vector<std::string> registry =
          parse_ini_array(original, "deps", "registry");

      std::vector<std::string> links =
          parse_ini_array(original, "deps", "links");

      registry.erase(
          std::remove_if(
              registry.begin(),
              registry.end(),
              [&](const std::string &existingDependency)
              {
                PkgSpec existingSpec;

                if (!parse_pkg_spec(existingDependency, existingSpec))
                  return false;

                return contains_string(
                    replacedPackageIds,
                    existingSpec.id());
              }),
          registry.end());

      if (!contains_string(registry, dependency))
        registry.push_back(dependency);

      if (!contains_string(links, linkTarget))
        links.push_back(linkTarget);

      std::string updated = remove_section(original, "deps");

      while (!updated.empty() &&
             (updated.back() == '\n' || updated.back() == '\r'))
      {
        updated.pop_back();
      }

      updated += "\n\n";
      updated += "[deps]\n";
      updated += render_string_array("registry", registry);
      updated += "\n";
      updated += render_string_array("links", links);
      updated += "\n";

      std::ofstream out(path, std::ios::binary | std::ios::trunc);

      if (!out)
      {
        error = "cannot write module manifest: " + path.string();
        return false;
      }

      out << updated;

      if (!out.good())
      {
        error = "failed to write module manifest: " + path.string();
        return false;
      }

      return true;
    }

    void add_dependency_once(
        std::vector<vix::cli::util::manifest::Dependency> &deps,
        const std::string &spec)
    {
      PkgSpec parsed;

      if (!parse_pkg_spec(spec, parsed))
        return;

      const std::string packageId = parsed.id();

      const std::string requested =
          parsed.requestedVersion.empty()
              ? "*"
              : parsed.requestedVersion;

      const auto exists =
          std::find_if(
              deps.begin(),
              deps.end(),
              [&](const vix::cli::util::manifest::Dependency &dep)
              {
                return dep.id == packageId;
              });

      if (exists != deps.end())
        return;

      deps.push_back(
          vix::cli::util::manifest::Dependency{
              packageId,
              requested});
    }

    std::vector<vix::cli::util::manifest::Dependency>
    read_effective_project_dependencies()
    {
      std::vector<vix::cli::util::manifest::Dependency> deps =
          vix::cli::util::manifest::read_manifest_dependencies_or_throw(
              manifest_path());

      const std::vector<EnabledModule> modules =
          read_enabled_modules_from_vix_app();

      for (const EnabledModule &module : modules)
      {
        const fs::path manifestPath =
            enabled_module_manifest_path(module);

        if (!fs::exists(manifestPath))
          continue;

        std::ifstream in(manifestPath, std::ios::binary);
        if (!in)
          continue;

        std::ostringstream buffer;
        buffer << in.rdbuf();

        const std::vector<std::string> registryDeps =
            parse_ini_array(buffer.str(), "deps", "registry");

        for (const std::string &dep : registryDeps)
          add_dependency_once(deps, dep);
      }

      return deps;
    }

    std::size_t refresh_lockfile_from_effective_dependencies()
    {
      const auto effectiveDependencies =
          read_effective_project_dependencies();

      const auto lockedDependencies =
          vix::cli::util::resolver::resolve_project_dependencies_or_throw(
              effectiveDependencies);

      vix::cli::util::lockfile::write_lockfile_replace_all_or_throw(
          lock_path(),
          lockedDependencies);

      return lockedDependencies.size();
    }

    json read_json_file_or_throw(const fs::path &path)
    {
      std::ifstream in(path);
      if (!in)
      {
        throw std::runtime_error("cannot open file: " + path.string());
      }

      json j;
      in >> j;
      return j;
    }

    void write_json_file_or_throw(
        const fs::path &path,
        const json &value)
    {
      std::ofstream out(path);

      if (!out)
      {
        throw std::runtime_error(
            "cannot write file: " + path.string());
      }

      out << value.dump(2) << "\n";

      if (!out.good())
      {
        throw std::runtime_error(
            "failed to write file: " + path.string());
      }
    }

    std::vector<std::string> find_packages_moved_to(
        const std::string &targetPackageId)
    {
      std::vector<std::string> packageIds;
      const fs::path indexPath = registry_index_dir();

      if (!fs::exists(indexPath))
        return packageIds;

      for (const auto &item :
           fs::directory_iterator(indexPath))
      {
        if (!item.is_regular_file() ||
            item.path().extension() != ".json")
        {
          continue;
        }

        try
        {
          const json entry =
              read_json_file_or_throw(item.path());

          const std::string movedTo =
              entry.value("movedTo", std::string{});

          if (movedTo != targetPackageId)
            continue;

          const std::string ns =
              entry.value("namespace", std::string{});

          const std::string name =
              entry.value("name", std::string{});

          if (ns.empty() || name.empty())
            continue;

          const std::string packageId =
              ns + "/" + name;

          if (!contains_string(packageIds, packageId))
            packageIds.push_back(packageId);
        }
        catch (...)
        {
        }
      }

      return packageIds;
    }

    std::size_t remove_manifest_dependencies(
        const fs::path &path,
        const std::vector<std::string> &packageIds)
    {
      if (packageIds.empty())
        return 0;

      json manifest = read_json_file_or_throw(path);

      if (!manifest.contains("deps") ||
          !manifest["deps"].is_array())
      {
        return 0;
      }

      json dependencies = json::array();
      std::size_t removed = 0;

      for (const auto &dependency : manifest["deps"])
      {
        if (!dependency.is_object() ||
            !dependency.contains("id") ||
            !dependency["id"].is_string())
        {
          dependencies.push_back(dependency);
          continue;
        }

        const std::string packageId =
            dependency["id"].get<std::string>();

        if (contains_string(packageIds, packageId))
        {
          ++removed;
          continue;
        }

        dependencies.push_back(dependency);
      }

      if (removed > 0)
      {
        manifest["deps"] = std::move(dependencies);
        write_json_file_or_throw(path, manifest);
      }

      return removed;
    }

    fs::path entry_path(const std::string &ns, const std::string &name)
    {
      return registry_index_dir() / (ns + "." + name + ".json");
    }

    int ensure_registry_present()
    {
      if (fs::exists(registry_dir()) && fs::exists(registry_index_dir()))
      {
        return 0;
      }

      vix::cli::util::err_line(std::cerr, "registry not synced");
      vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
      return 1;
    }

    int resolve_version_v1(const json &entry, PkgSpec &spec)
    {
      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        vix::cli::util::err_line(
            std::cerr,
            "invalid registry entry: missing versions for " + spec.id());
        return 1;
      }

      std::vector<std::string> versions;
      versions.reserve(entry["versions"].size());

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        versions.push_back(it.key());
      }

      if (versions.empty())
      {
        vix::cli::util::err_line(
            std::cerr,
            "no versions available for: " + spec.id());
        return 1;
      }

      if (spec.requestedVersion.empty())
      {
        spec.resolvedVersion = vix::cli::util::semver::findLatest(versions);
        return 0;
      }

      const auto resolved =
          vix::cli::util::semver::resolveMaxSatisfying(
              versions,
              spec.requestedVersion);

      if (!resolved.has_value())
      {
        vix::cli::util::err_line(
            std::cerr,
            "no version matches range: " + spec.id() + "@" + spec.requestedVersion);
        return 1;
      }

      spec.resolvedVersion = *resolved;
      return 0;
    }

    bool contains_any_icase(std::string hay, const std::string &needleLower)
    {
      if (needleLower.empty())
      {
        return true;
      }

      hay = to_lower(std::move(hay));
      return hay.find(needleLower) != std::string::npos;
    }

    struct SearchHit
    {
      std::string id;
      std::string latest;
      std::string description;
      std::string repo;
    };

    std::vector<SearchHit> search_registry_local(const std::string &queryRaw)
    {
      std::vector<SearchHit> hits;
      const fs::path dir = registry_index_dir();

      if (!fs::exists(dir))
      {
        return hits;
      }

      const std::string query = to_lower(trim_copy(queryRaw));

      for (const auto &entryIt : fs::directory_iterator(dir))
      {
        if (!entryIt.is_regular_file())
        {
          continue;
        }

        const fs::path path = entryIt.path();
        if (path.extension() != ".json")
        {
          continue;
        }

        try
        {
          const json entry = read_json_file_or_throw(path);

          const std::string ns = entry.value("namespace", "");
          const std::string name = entry.value("name", "");
          const std::string id =
              (ns.empty() || name.empty()) ? "" : (ns + "/" + name);

          const std::string description = entry.value("description", "");
          const std::string repo =
              (entry.contains("repo") && entry["repo"].is_object())
                  ? entry["repo"].value("url", "")
                  : "";

          std::string latest;
          if (entry.contains("latest") && entry["latest"].is_string())
          {
            latest = entry["latest"].get<std::string>();
          }

          const std::string hay =
              id + " " + description + " " + repo + " " + path.filename().string();

          if (contains_any_icase(hay, query))
          {
            SearchHit hit;
            hit.id = id;
            hit.latest = latest;
            hit.description = description;
            hit.repo = repo;
            hits.push_back(std::move(hit));
          }
        }
        catch (...)
        {
        }
      }

      hits.erase(
          std::remove_if(
              hits.begin(),
              hits.end(),
              [](const SearchHit &hit)
              {
                return hit.id.empty();
              }),
          hits.end());

      std::sort(
          hits.begin(),
          hits.end(),
          [](const SearchHit &left, const SearchHit &right)
          {
            return left.id < right.id;
          });

      return hits;
    }

    void print_search_hits(const std::vector<SearchHit> &hits, std::size_t limit = 15U)
    {
      if (hits.empty())
      {
        vix::cli::util::warn_line(
            std::cout,
            "No matches found in the local registry index.");
        return;
      }

      const std::size_t count = std::min<std::size_t>(hits.size(), limit);

      for (std::size_t i = 0; i < count; ++i)
      {
        const auto &hit = hits[i];
        vix::cli::util::pkg_line(
            std::cout,
            hit.id,
            hit.latest,
            hit.description,
            hit.repo);
        std::cout << "\n";
      }

      if (hits.size() > limit)
      {
        vix::cli::util::ok_line(
            std::cout,
            "Showing " + std::to_string(count) + " of " +
                std::to_string(hits.size()) + " result(s).");
      }
      else
      {
        vix::cli::util::ok_line(
            std::cout,
            "Found " + std::to_string(hits.size()) + " result(s).");
      }
    }

    std::string find_latest_version(const json &entry)
    {
      if (entry.contains("latest") && entry["latest"].is_string())
      {
        return entry["latest"].get<std::string>();
      }

      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        return {};
      }

      std::vector<std::string> versions;
      versions.reserve(entry["versions"].size());

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        versions.push_back(it.key());
      }

      return vix::cli::util::semver::findLatest(versions);
    }

    std::vector<std::string> list_versions(const json &entry)
    {
      std::vector<std::string> out;

      if (!entry.contains("versions") || !entry["versions"].is_object())
      {
        return out;
      }

      for (auto it = entry["versions"].begin(); it != entry["versions"].end(); ++it)
      {
        out.push_back(it.key());
      }

      vix::cli::util::semver::sortAscending(out);
      return out;
    }
  }

  int AddCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty())
    {
      return help();
    }

    if (ensure_registry_present() != 0)
    {
      return 1;
    }

    AddOptions options;
    std::string optionsError;

    if (!parse_add_options(args, options, optionsError))
    {
      vix::cli::util::err_line(std::cerr, optionsError);
      vix::cli::util::warn_line(
          std::cerr,
          "Usage: vix add [@]namespace/name[@version] [--module <name>] [--link <target>]");
      return 1;
    }

    const std::string raw = options.packageSpec;

    PkgSpec spec;
    if (!parse_pkg_spec(raw, spec))
    {
      vix::cli::util::err_line(std::cerr, "invalid package spec");
      vix::cli::util::warn_line(
          std::cerr,
          "Expected: <namespace>/<name>[@<version>]");
      vix::cli::util::warn_line(
          std::cerr,
          "Example:  vix add gaspardkirira/tree");
      vix::cli::util::warn_line(
          std::cerr,
          "Example:  vix add gaspardkirira/tree@0.1.0");
      vix::cli::util::warn_line(
          std::cerr,
          "Example:  vix add gk/jwt@^1.0.0 --module auth");
      vix::cli::util::warn_line(
          std::cerr,
          std::string("Try search: vix search ") + raw);
      return 1;
    }

    fs::path packageEntryPath =
        entry_path(spec.ns, spec.name);

    if (!fs::exists(packageEntryPath))
    {
      vix::cli::util::err_line(std::cerr, "package not found: " + spec.id());

      vix::cli::util::section(std::cout, "Search");
      vix::cli::util::kv(
          std::cout,
          "query",
          vix::cli::util::quote(spec.name));

      const auto hits = search_registry_local(spec.name);
      print_search_hits(hits);

      vix::cli::util::warn_line(
          std::cerr,
          "If you just updated the registry, run: vix registry sync");
      return 1;
    }

    try
    {
      json entry =
          read_json_file_or_throw(packageEntryPath);

      const std::string originalPackageId = spec.id();

      if (entry.contains("movedTo") &&
          entry["movedTo"].is_string())
      {
        const std::string movedTo =
            trim_copy(entry["movedTo"].get<std::string>());

        if (!movedTo.empty())
        {
          PkgSpec movedSpec;

          if (!parse_pkg_spec(movedTo, movedSpec))
          {
            throw std::runtime_error(
                "invalid movedTo package id: " + movedTo);
          }

          movedSpec.requestedVersion =
              spec.requestedVersion;

          movedSpec.resolvedVersion.clear();

          spec = std::move(movedSpec);

          packageEntryPath =
              entry_path(spec.ns, spec.name);

          if (!fs::exists(packageEntryPath))
          {
            throw std::runtime_error(
                "moved package not found: " + spec.id());
          }

          entry =
              read_json_file_or_throw(packageEntryPath);

          vix::cli::util::warn_line(
              std::cout,
              "Package moved: " +
                  originalPackageId +
                  " -> " +
                  spec.id());
        }
      }

      const int resolveRc =
          resolve_version_v1(entry, spec);
      if (resolveRc != 0)
      {
        return resolveRc;
      }

      if (spec.requestedVersion.empty())
      {
        vix::cli::util::ok_line(
            std::cout,
            "resolved: " + spec.id() + "@" + spec.resolvedVersion);
      }

      const json versions = entry.at("versions");
      if (!versions.contains(spec.resolvedVersion))
      {
        vix::cli::util::err_line(
            std::cerr,
            "version not found: " + spec.id() + "@" + spec.resolvedVersion);

        const std::string latest = find_latest_version(entry);
        const auto allVersions = list_versions(entry);

        if (!allVersions.empty())
        {
          vix::cli::util::section(std::cout, "Available versions");
          for (const auto &version : allVersions)
          {
            std::cout
                << "  "
                << GRAY << "• " << RESET
                << BOLD << version << RESET
                << "\n";
          }
          std::cout << "\n";
        }

        if (!latest.empty())
        {
          vix::cli::util::warn_line(
              std::cerr,
              "Try: vix add " + spec.id() + "@" + latest);
        }

        return 1;
      }

      const json versionNode = versions.at(spec.resolvedVersion);
      const std::string tag = versionNode.at("tag").get<std::string>();
      const std::string commit = versionNode.at("commit").get<std::string>();

      const std::string packageId = spec.id();
      const std::string requested =
          spec.requestedVersion.empty()
              ? spec.resolvedVersion
              : spec.requestedVersion;

      const std::string dependencySpec =
          packageId + "@" + requested;

      const std::vector<std::string> replacedPackageIds =
          find_packages_moved_to(packageId);

      if (!options.moduleName.empty())
      {
        const std::string linkTarget =
            options.linkTarget.empty()
                ? default_link_target(spec)
                : options.linkTarget;

        std::string moduleError;

        if (!upsert_module_dependency(
                module_manifest_path(options.moduleName),
                dependencySpec,
                linkTarget,
                replacedPackageIds,
                moduleError))
        {
          vix::cli::util::err_line(std::cerr, moduleError);
          return 1;
        }

        vix::cli::util::section(std::cout, "Add");
        vix::cli::util::kv(std::cout, "id", packageId);
        vix::cli::util::kv(std::cout, "version", spec.resolvedVersion);
        vix::cli::util::kv(std::cout, "module", options.moduleName);
        vix::cli::util::kv(std::cout, "link", linkTarget);
        vix::cli::util::kv(
            std::cout,
            "file",
            module_manifest_path(options.moduleName).string());

        vix::cli::util::ok_line(
            std::cout,
            "added to module: " + options.moduleName);

        step("resolving project dependencies...");

        const std::size_t lockedCount =
            refresh_lockfile_from_effective_dependencies();

        vix::cli::util::ok_line(
            std::cout,
            "lock:  " + lock_path().string());

        vix::cli::util::ok_line(
            std::cout,
            "deps:  " + std::to_string(lockedCount));

        vix::cli::util::warn_line(
            std::cout,
            "Run: vix build");

        return 0;
      }

      const std::size_t replacedCount =
          remove_manifest_dependencies(
              manifest_path(),
              replacedPackageIds);

      vix::cli::util::manifest::upsert_manifest_dependency_or_throw(
          manifest_path(),
          vix::cli::util::manifest::Dependency{
              packageId,
              requested});

      if (replacedCount > 0)
      {
        for (const std::string &oldPackageId :
             replacedPackageIds)
        {
          vix::cli::util::kv(
              std::cout,
              "replaced",
              oldPackageId + " -> " + packageId);
        }
      }

      vix::cli::util::section(std::cout, "Add");
      vix::cli::util::kv(std::cout, "id", packageId);
      vix::cli::util::kv(std::cout, "version", spec.resolvedVersion);
      vix::cli::util::kv(std::cout, "tag", tag);
      vix::cli::util::kv(std::cout, "commit", commit);

      step("resolving project dependencies...");

      const auto manifestDependencies =
          vix::cli::util::manifest::read_manifest_dependencies_or_throw(
              manifest_path());

      const auto lockedDependencies =
          vix::cli::util::resolver::resolve_project_dependencies_or_throw(
              manifestDependencies);

      vix::cli::util::lockfile::write_lockfile_replace_all_or_throw(
          lock_path(),
          lockedDependencies);

      vix::cli::util::ok_line(
          std::cout,
          "added: " + packageId + "@" + spec.resolvedVersion);
      vix::cli::util::ok_line(
          std::cout,
          "lock:  " + lock_path().string());
      vix::cli::util::ok_line(
          std::cout,
          "deps:  " + std::to_string(lockedDependencies.size()));

      return 0;
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(
          std::cerr,
          std::string("add failed: ") + ex.what());
      return 1;
    }
  }

  int AddCommand::help()
  {
    std::cout
        << "vix add\n"
        << "Add a package to your project.\n\n"

        << "Usage\n"
        << "  vix add [@]namespace/name[@version]\n"
        << "  vix add [@]namespace/name[@version] --module <name>\n"
        << "  vix add [@]namespace/name[@version] -m <name>\n"
        << "  vix add [@]namespace/name[@version] --module <name> --link <target>\n\n"

        << "Examples\n"
        << "  vix add gk/jwt\n"
        << "  vix add gk/jwt@^1.0.0\n"
        << "  vix add @gk/jwt\n"
        << "  vix add @gk/jwt@~1.2.0\n"
        << "  vix add gk/jwt@^1.0.0 --module auth\n"
        << "  vix add gk/jwt@^1.0.0 -m auth\n"
        << "  vix add gk/jwt@^1.0.0 --module auth --link gk::jwt\n\n"

        << "What happens\n"
        << "  • Resolves the exact version (latest if not specified)\n"
        << "  • Fetches the package at a pinned commit\n"
        << "  • Updates vix.json with the declared dependency\n"
        << "  • Updates vix.lock with the exact resolved version\n"
        << "  • With --module, updates modules/<name>/vix.module instead of adding the dependency directly to vix.json\n"
        << "  • Module dependencies are resolved by the application build and kept in the root vix.lock\n"
        << "  • Installs all required dependencies\n\n"

        << "Notes\n"
        << "  • Use 'vix registry sync' if a package is not found\n"
        << "  • '@namespace/name' is supported (scoped packages)\n"
        << "  • vix.json stores declared dependency requirements\n"
        << "  • vix.lock stores exact resolved versions for reproducible installs\n";

    return 0;
  }
}
