/**
 * @file ModulesContent.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/modules/ModulesContent.hpp>
#include <vix/cli/commands/modules/ModulesUtils.hpp>

#include <sstream>
#include <unordered_set>

namespace vix::commands::modules_cmd::content
{

  namespace fs = std::filesystem;
  using namespace vix::commands::modules_cmd::utils;

  // ------------------------------------------------------------------
  // Naming helpers
  // ------------------------------------------------------------------

  std::string normalize_module_id(std::string name)
  {
    name = trim(name);
    std::string out;
    out.reserve(name.size());
    for (char c : name)
      out.push_back(c == '-' ? '_' : c);
    return out;
  }

  std::string module_target_name(const std::string &project, const std::string &module)
  {
    return project + "_" + normalize_module_id(module);
  }

  std::string module_alias_name(const std::string &project, const std::string &module)
  {
    return project + "::" + normalize_module_id(module);
  }

  // ------------------------------------------------------------------
  // Validation
  // ------------------------------------------------------------------

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
          (c == '_' || c == '-');
      if (!ok)
        return false;
    }
    return true;
  }

  bool is_reserved_module_name(std::string name)
  {
    name = to_lower(normalize_module_id(name));
    static const std::unordered_set<std::string> reserved = {
        "modules", "module", "src", "source", "include", "cmake", "build", "dist",
        "test", "tests", "example", "examples", "vendor", "third_party", "thirdparty",
        "external", "internal", "internals", "detail", "details", "private", "public",
        "main", "app", "api", "core", "std", "vix", "vixcpp",
        "registry", "deps", "pack", "lock", "install", "add", "remove", "store", "gc",
        "fmt", "spdlog", "boost", "openssl", "zlib", "sqlite", "mysql", "postgres", "curl",
        "asio", "beast"};
    return reserved.find(name) != reserved.end();
  }

  // ------------------------------------------------------------------
  // CMake content generators
  // ------------------------------------------------------------------

  std::string cmake_vix_modules_cmake_app_first()
  {
    std::ostringstream o;

    o << "##\n";
    o << "## Vix Modules (app-first, opt-in)\n";
    o << "##\n";
    o << "## Contract (Go-like):\n";
    o << "## - modules/<m>/include/<m>/...  (public headers)\n";
    o << "## - modules/<m>/src/...          (private impl)\n";
    o << "## - Each module exports <project>::<m> as an ALIAS target\n";
    o << "## - Public headers must never include private sources (src/)\n";
    o << "## - Cross-module usage must be explicit via target_link_libraries\n";
    o << "##\n\n";

    o << "if(DEFINED VIX_MODULES_INCLUDED)\n";
    o << "  return()\n";
    o << "endif()\n";
    o << "set(VIX_MODULES_INCLUDED ON)\n\n";

    o << "set(VIX_MODULES_DIR \"${CMAKE_CURRENT_LIST_DIR}/../modules\")\n";
    o << "if(NOT EXISTS \"${VIX_MODULES_DIR}\")\n";
    o << "  return()\n";
    o << "endif()\n\n";

    o << "file(GLOB VIX_MODULE_DIRS RELATIVE \"${VIX_MODULES_DIR}\" \"${VIX_MODULES_DIR}/*\")\n";
    o << "foreach(_m ${VIX_MODULE_DIRS})\n";
    o << "  if(IS_DIRECTORY \"${VIX_MODULES_DIR}/${_m}\")\n";
    o << "    if(EXISTS \"${VIX_MODULES_DIR}/${_m}/CMakeLists.txt\")\n";
    o << "      add_subdirectory(\"${VIX_MODULES_DIR}/${_m}\" \"${CMAKE_BINARY_DIR}/vix_modules/${_m}\")\n";
    o << "    endif()\n";
    o << "  endif()\n";
    o << "endforeach()\n";

    return o.str();
  }

  std::string module_cmakelists_txt_app_first(const std::string &project, const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string target = module_target_name(project, module);
    const std::string alias = module_alias_name(project, module);

    std::ostringstream o;

    o << "cmake_minimum_required(VERSION 3.16)\n\n";
    o << "add_library(" << target << ")\n";
    o << "add_library(" << alias << " ALIAS " << target << ")\n\n";

    o << "target_sources(" << target << "\n";
    o << "  PRIVATE\n";
    o << "    src/" << normalized << ".cpp\n";
    o << ")\n\n";

    o << "target_include_directories(" << target << "\n";
    o << "  PUBLIC\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/include\n";
    o << "  PRIVATE\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/src\n";
    o << ")\n\n";

    o << "target_compile_features(" << target << " PUBLIC cxx_std_20)\n\n";

    o << "set_target_properties(" << target << " PROPERTIES\n";
    o << "  OUTPUT_NAME \"" << target << "\"\n";
    o << ")\n";

    return o.str();
  }

  // ------------------------------------------------------------------
  // C++ content generators
  // ------------------------------------------------------------------

  std::string module_public_header_app_first(const std::string &project, const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string guard =
        to_lower(normalize_module_id(project)) + "_" + to_lower(normalized) + "_api_hpp";

    std::ostringstream o;
    o << "#ifndef " << guard << "\n";
    o << "#define " << guard << "\n\n";
    o << "#include <string>\n\n";
    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";
    o << "  struct Api\n";
    o << "  {\n";
    o << "    static std::string name();\n";
    o << "  };\n";
    o << "}\n\n";
    o << "#endif\n";
    return o.str();
  }

  std::string module_impl_cpp_app_first(const std::string &project, const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    std::ostringstream o;
    o << "#include <" << normalized << "/api.hpp>\n\n";
    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";
    o << "  std::string Api::name()\n";
    o << "  {\n";
    o << "    return \"" << project << "::" << normalized << "\";\n";
    o << "  }\n";
    o << "}\n";
    return o.str();
  }

  // ------------------------------------------------------------------
  // CMakeLists.txt patching
  // ------------------------------------------------------------------

  bool patch_root_cmakelists_include(const fs::path &root)
  {
    const fs::path cm = root / "CMakeLists.txt";
    auto contentOpt = read_file(cm);
    if (!contentOpt)
      return false;

    std::string content = *contentOpt;

    const std::string beginMark = "# VIX_MODULES_BEGIN";
    const std::string endMark = "# VIX_MODULES_END";
    const std::string block =
        beginMark + "\n"
                    "include(${CMAKE_CURRENT_LIST_DIR}/cmake/vix_modules.cmake)\n" +
        endMark + "\n";

    // Already patched — idempotent
    if (content.find(beginMark) != std::string::npos &&
        content.find(endMark) != std::string::npos)
      return true;

    std::istringstream in(content);
    std::ostringstream out;
    std::string line;
    bool inserted = false;

    while (std::getline(in, line))
    {
      out << line << "\n";
      if (!inserted && starts_with(to_lower(trim(line)), "project("))
      {
        out << "\n"
            << block << "\n";
        inserted = true;
      }
    }

    if (!inserted)
    {
      // No project() found — prepend
      content = block + "\n" + content;
    }
    else
    {
      content = out.str();
    }

    return write_file_overwrite(cm, content);
  }

  bool patch_root_cmakelists_link_module(
      const fs::path &root,
      const std::string &project,
      const std::string &module)
  {
    const fs::path cm = root / "CMakeLists.txt";
    auto contentOpt = read_file(cm);
    if (!contentOpt)
      return false;

    std::string content = *contentOpt;

    const std::string beginMark = "# VIX_MODULE_LINKS_BEGIN";
    const std::string endMark = "# VIX_MODULE_LINKS_END";

    // Ensure the links section exists
    if (content.find(beginMark) == std::string::npos ||
        content.find(endMark) == std::string::npos)
    {
      std::ostringstream section;
      section << "\n"
              << beginMark << "\n"
              << "# Auto-generated by: vix modules\n"
              << "# NOTE: This links modules into the main target named like project(" << project << ").\n"
              << "#       If your main target differs, remove this section and link targets manually.\n"
              << endMark << "\n";
      content += section.str();
    }

    const std::string alias = module_alias_name(project, module);
    const std::string modNorm = normalize_module_id(module);
    const std::string perBegin = "# VIX_MODULE_LINK_BEGIN " + modNorm;
    const std::string perEnd = "# VIX_MODULE_LINK_END " + modNorm;

    // Already patched for this module — idempotent
    if (content.find(perBegin) != std::string::npos &&
        content.find(perEnd) != std::string::npos)
      return true;

    std::ostringstream block;
    block << perBegin << "\n"
          << "if (TARGET " << alias << ")\n"
          << "  if (TARGET " << project << ")\n"
          << "    target_link_libraries(" << project << " PRIVATE " << alias << ")\n"
          << "  endif()\n"
          << "endif()\n"
          << perEnd << "\n";

    const size_t endPos = content.find(endMark);
    if (endPos == std::string::npos)
      return false;

    content.insert(endPos, block.str());
    return write_file_overwrite(cm, content);
  }

  // ------------------------------------------------------------------
  // Static analysis helpers
  // ------------------------------------------------------------------

  std::unordered_set<std::string> parse_declared_deps_from_module_cmake(
      const fs::path &moduleCmake,
      const std::string &project)
  {
    std::unordered_set<std::string> deps;

    auto content = read_file(moduleCmake);
    if (!content)
      return deps;

    const std::string &s = *content;
    const std::string needle = project + "::";
    size_t pos = 0;

    while ((pos = s.find(needle, pos)) != std::string::npos)
    {
      size_t start = pos + needle.size();
      size_t end = start;
      while (end < s.size())
      {
        char c = s[end];
        if (!std::isalnum((unsigned char)c) && c != '_' && c != '-')
          break;
        ++end;
      }
      if (end > start)
        deps.insert(s.substr(start, end - start));
      pos = end;
    }

    return deps;
  }

  std::set<std::string> parse_public_includes_for_cross_module(
      const fs::path &publicHeader,
      const fs::path &modulesDir)
  {
    std::set<std::string> used;

    auto content = read_file(publicHeader);
    if (!content)
      return used;

    std::istringstream in(*content);
    std::string line;
    while (std::getline(in, line))
    {
      std::string s = trim(line);
      if (!starts_with(s, "#include"))
        continue;

      auto lt = s.find('<');
      auto gt = s.find('>', lt == std::string::npos ? 0 : lt + 1);
      if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1)
        continue;

      const std::string inside = s.substr(lt + 1, gt - (lt + 1));
      auto slash = inside.find('/');
      if (slash == std::string::npos)
        continue;

      const std::string first = inside.substr(0, slash);
      if (!first.empty() && exists_dir(modulesDir / first))
        used.insert(first);
    }

    return used;
  }

  bool header_includes_private_impl(
      const fs::path &publicHeader,
      const fs::path &moduleDir)
  {
    auto content = read_file(publicHeader);
    if (!content)
      return false;

    const std::string &s = *content;
    const std::string mod = moduleDir.filename().string();

    return s.find("\"src/") != std::string::npos ||
           s.find("../src/") != std::string::npos ||
           s.find("/src/") != std::string::npos ||
           s.find("modules/" + mod + "/src/") != std::string::npos;
  }

} // namespace vix::commands::modules_cmd::content
