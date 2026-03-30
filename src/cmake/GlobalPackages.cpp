/**
 *
 *  @file GlobalPackages.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/cmake/GlobalPackages.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vix::cli::build
{
  namespace
  {
    static std::optional<std::string> home_dir()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
#endif
      if (!home || std::string(home).empty())
        return std::nullopt;

      return std::string(home);
    }

    static fs::path vix_root()
    {
      if (const auto home = home_dir(); home)
        return fs::path(*home) / ".vix";

      return fs::path(".vix");
    }

    static fs::path global_manifest_path()
    {
      return vix_root() / "global" / "installed.json";
    }

    static fs::path artifact_cache_root()
    {
      return vix_root() / "cache" / "build";
    }

    static std::string dep_id_to_dir(std::string depId)
    {
      depId.erase(std::remove(depId.begin(), depId.end(), '@'), depId.end());
      std::replace(depId.begin(), depId.end(), '/', '.');
      return depId;
    }

    static std::string cmake_quote(const std::string &value)
    {
      std::string out;
      out.reserve(value.size() + 8);
      out.push_back('"');

      for (char c : value)
      {
        if (c == '\\')
          out += "\\\\";
        else if (c == '"')
          out += "\\\"";
        else
          out.push_back(c);
      }

      out.push_back('"');
      return out;
    }

    static bool is_header_only_package(const GlobalPackage &pkg)
    {
      return pkg.type == "header-only";
    }

    static fs::path package_include_path(const GlobalPackage &pkg)
    {
      return pkg.installedPath / pkg.includeDir;
    }

    static fs::path package_cmake_lists_path(const GlobalPackage &pkg)
    {
      return pkg.installedPath / "CMakeLists.txt";
    }

    static std::string package_binary_dir_name(const GlobalPackage &pkg)
    {
      std::string name = pkg.pkgDir;
      if (name.empty())
        name = "pkg";

      for (char &c : name)
      {
        if (c == '.' || c == '/' || c == '\\' || c == ':' || c == ' ')
          c = '_';
      }

      return name;
    }

    static std::string package_cache_leaf_name(const GlobalPackage &pkg)
    {
      std::string name = pkg.pkgDir;
      if (name.empty())
        name = dep_id_to_dir(pkg.id);

      for (char &c : name)
      {
        if (c == '.' || c == '/' || c == '\\' || c == ':' || c == ' ')
          c = '_';
      }

      return name;
    }

    static bool looks_like_artifact_prefix(const fs::path &root)
    {
      return fs::exists(root / "manifest.json") &&
             fs::exists(root / "include");
    }

    static std::vector<fs::path> find_cached_artifact_prefixes(const GlobalPackage &pkg)
    {
      std::vector<fs::path> prefixes;

      const fs::path cacheRoot = artifact_cache_root();
      if (!fs::exists(cacheRoot) || !fs::is_directory(cacheRoot))
        return prefixes;

      const std::string expectedLeaf = package_cache_leaf_name(pkg) + "@local";
      std::error_code ec;

      for (const auto &targetDir : fs::directory_iterator(cacheRoot, ec))
      {
        if (ec || !targetDir.is_directory())
          continue;

        for (const auto &compilerDir : fs::directory_iterator(targetDir.path(), ec))
        {
          if (ec || !compilerDir.is_directory())
            continue;

          for (const auto &buildTypeDir : fs::directory_iterator(compilerDir.path(), ec))
          {
            if (ec || !buildTypeDir.is_directory())
              continue;

            for (const auto &packageDir : fs::directory_iterator(buildTypeDir.path(), ec))
            {
              if (ec || !packageDir.is_directory())
                continue;

              const std::string dirName = packageDir.path().filename().string();

              if (dirName != expectedLeaf &&
                  dirName != (package_cache_leaf_name(pkg) + "@unknown") &&
                  dirName != package_cache_leaf_name(pkg))
              {
                continue;
              }

              for (const auto &fingerprintDir : fs::directory_iterator(packageDir.path(), ec))
              {
                if (ec || !fingerprintDir.is_directory())
                  continue;

                if (looks_like_artifact_prefix(fingerprintDir.path()))
                  prefixes.push_back(fingerprintDir.path());
              }
            }
          }
        }
      }

      std::sort(prefixes.begin(), prefixes.end());
      prefixes.erase(std::unique(prefixes.begin(), prefixes.end()), prefixes.end());
      return prefixes;
    }

    static std::string make_prefix_path_block(const GlobalPackage &pkg)
    {
      std::ostringstream out;
      const auto prefixes = find_cached_artifact_prefixes(pkg);

      if (prefixes.empty())
        return "";

      out << "# Cached artifact prefixes for " << pkg.id << "\n";
      for (const auto &prefix : prefixes)
      {
        out << "if (EXISTS " << cmake_quote(prefix.string()) << ")\n";
        out << "  list(PREPEND CMAKE_PREFIX_PATH "
            << cmake_quote(prefix.string()) << ")\n";
        out << "endif()\n";
      }
      out << "\n";

      return out.str();
    }

    static std::string make_header_only_block(const GlobalPackage &pkg)
    {
      std::ostringstream out;
      const fs::path includePath = package_include_path(pkg);

      if (!pkg.includeDir.empty())
      {
        out << "if (EXISTS " << cmake_quote(includePath.string()) << ")\n";
        out << "  include_directories(" << cmake_quote(includePath.string()) << ")\n";
        out << "endif()\n";
      }

      return out.str();
    }

    static std::string make_source_fallback_block(const GlobalPackage &pkg)
    {
      std::ostringstream out;
      const fs::path cmakeLists = package_cmake_lists_path(pkg);

      if (!fs::exists(cmakeLists))
        return "";

      const std::string binDir =
          "${CMAKE_BINARY_DIR}/_vix_global/" + package_binary_dir_name(pkg);

      out << "if (EXISTS " << cmake_quote(cmakeLists.string()) << ")\n";
      out << "  add_subdirectory(\n";
      out << "    " << cmake_quote(pkg.installedPath.string()) << "\n";
      out << "    " << cmake_quote(binDir) << "\n";
      out << "    EXCLUDE_FROM_ALL\n";
      out << "  )\n";
      out << "endif()\n";

      return out.str();
    }
  } // namespace

  std::vector<GlobalPackage> load_global_packages()
  {
    std::vector<GlobalPackage> out;

    const fs::path manifestPath = global_manifest_path();
    if (!fs::exists(manifestPath))
      return out;

    std::ifstream ifs(manifestPath);
    if (!ifs)
      return out;

    nlohmann::json root;
    ifs >> root;

    if (!root.is_object())
      return out;

    if (!root.contains("packages") || !root["packages"].is_array())
      return out;

    for (const auto &item : root["packages"])
    {
      if (!item.is_object())
        continue;

      if (!item.contains("id") || !item["id"].is_string())
        continue;

      if (!item.contains("installed_path") || !item["installed_path"].is_string())
        continue;

      GlobalPackage pkg;
      pkg.id = item["id"].get<std::string>();
      pkg.pkgDir = dep_id_to_dir(pkg.id);
      pkg.installedPath = fs::path(item["installed_path"].get<std::string>());

      if (item.contains("include") && item["include"].is_string())
        pkg.includeDir = item["include"].get<std::string>();

      if (item.contains("type") && item["type"].is_string())
        pkg.type = item["type"].get<std::string>();

      out.push_back(std::move(pkg));
    }

    return out;
  }

  std::string make_global_packages_cmake(const std::vector<GlobalPackage> &packages)
  {
    std::ostringstream out;

    out << "# Auto-generated by Vix\n";
    out << "# Global packages integration for vix build\n";
    out << "# Strategy:\n";
    out << "#   1. inject cached compiled artifact prefixes when available\n";
    out << "#   2. keep header-only include paths\n";
    out << "#   3. fallback to add_subdirectory for source packages\n\n";

    if (packages.empty())
    {
      out << "# No global packages installed\n";
      return out.str();
    }

    for (const auto &pkg : packages)
    {
      if (pkg.installedPath.empty())
        continue;

      out << "# Package: " << pkg.id << "\n";

      const std::string prefixBlock = make_prefix_path_block(pkg);
      if (!prefixBlock.empty())
        out << prefixBlock;

      if (is_header_only_package(pkg))
      {
        out << make_header_only_block(pkg);
      }
      else
      {
        const fs::path includePath = package_include_path(pkg);
        if (!pkg.includeDir.empty())
        {
          out << "if (EXISTS " << cmake_quote(includePath.string()) << ")\n";
          out << "  include_directories(" << cmake_quote(includePath.string()) << ")\n";
          out << "endif()\n";
        }

        out << make_source_fallback_block(pkg);
      }

      out << "\n";
    }

    return out.str();
  }

} // namespace vix::cli::build
