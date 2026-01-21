/**
 *
 *  @file ModulesCommand.cpp
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
#include <vix/cli/commands/ModulesCommand.hpp>

#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vix::commands::ModulesCommand
{
  namespace fs = std::filesystem;
  namespace ui = vix::cli::util;
  using namespace vix::cli::style;

  static bool starts_with(std::string_view s, std::string_view pfx)
  {
    return s.size() >= pfx.size() && s.substr(0, pfx.size()) == pfx;
  }

  static bool ends_with(std::string_view s, std::string_view suf)
  {
    return s.size() >= suf.size() && s.substr(s.size() - suf.size()) == suf;
  }

  static std::string trim(std::string s)
  {
    auto is_space = [](unsigned char c)
    { return std::isspace(c) != 0; };

    while (!s.empty() && is_space((unsigned char)s.front()))
      s.erase(s.begin());
    while (!s.empty() && is_space((unsigned char)s.back()))
      s.pop_back();
    return s;
  }

  static std::string to_lower(std::string s)
  {
    for (auto &c : s)
      c = (char)std::tolower((unsigned char)c);
    return s;
  }

  static bool exists_dir(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && fs::is_directory(p, ec) && !ec;
  }

  static bool exists_file(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && fs::is_regular_file(p, ec) && !ec;
  }

  static bool ensure_dir(const fs::path &p)
  {
    std::error_code ec{};
    if (fs::exists(p, ec))
      return true;
    return fs::create_directories(p, ec) && !ec;
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

  static bool write_file_if_missing(const fs::path &p, const std::string &content)
  {
    if (exists_file(p))
      return true;
    std::ofstream out(p.string(), std::ios::out | std::ios::binary);
    if (!out)
      return false;
    out << content;
    return true;
  }

  static bool write_file_overwrite(const fs::path &p, const std::string &content)
  {
    std::ofstream out(p.string(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    out << content;
    return true;
  }

  static fs::path resolve_root(const std::string &dirOpt)
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

  static std::string detect_project_name_from_cmake(const fs::path &root)
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

      if (name.size() >= 2 && ((name.front() == '"' && name.back() == '"') || (name.front() == '\'' && name.back() == '\'')))
        name = name.substr(1, name.size() - 2);

      if (!name.empty())
        return name;
    }

    return "myproj";
  }

  static bool is_valid_module_name(const std::string &name)
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

  static std::string normalize_module_id(std::string name)
  {
    name = trim(name);
    std::string out;
    out.reserve(name.size());
    for (char c : name)
      out.push_back(c == '-' ? '_' : c);
    return out;
  }

  static bool is_reserved_module_name(std::string name)
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

  static std::string module_target_name(const std::string &project, const std::string &module)
  {
    return project + "_" + normalize_module_id(module);
  }

  static std::string module_alias_name(const std::string &project, const std::string &module)
  {
    return project + "::" + normalize_module_id(module);
  }

  static std::string cmake_vix_modules_cmake_app_first()
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

  static bool patch_root_cmakelists_include(const fs::path &root)
  {
    const fs::path cm = root / "CMakeLists.txt";
    auto contentOpt = read_file(cm);
    if (!contentOpt)
      return false;

    std::string content = *contentOpt;

    const std::string beginMark = "# VIX_MODULES_BEGIN";
    const std::string endMark = "# VIX_MODULES_END";
    const std::string block =
        beginMark + "\n" +
        "include(${CMAKE_CURRENT_LIST_DIR}/cmake/vix_modules.cmake)\n" +
        endMark + "\n";

    if (content.find(beginMark) != std::string::npos && content.find(endMark) != std::string::npos)
      return true;

    std::istringstream in(content);
    std::ostringstream out;
    std::string line;
    bool inserted = false;

    while (std::getline(in, line))
    {
      out << line << "\n";

      if (!inserted)
      {
        std::string s = trim(line);
        if (starts_with(to_lower(s), "project("))
        {
          out << "\n"
              << block << "\n";
          inserted = true;
        }
      }
    }

    if (!inserted)
    {
      std::ostringstream out2;
      out2 << block << "\n"
           << content;
      content = out2.str();
    }
    else
    {
      content = out.str();
    }

    return write_file_overwrite(cm, content);
  }

  static bool patch_root_cmakelists_link_module(
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

    if (content.find(beginMark) == std::string::npos || content.find(endMark) == std::string::npos)
    {
      std::ostringstream section;
      section << "\n"
              << beginMark << "\n"
              << "# Auto-generated by: vix modules\n"
              << "# NOTE: This links modules into the main target named like project(<name>).\n"
              << "#       If your main target differs, remove this section and link targets manually.\n"
              << endMark << "\n";

      content += section.str();
    }

    const std::string alias = module_alias_name(project, module);

    const std::string modNorm = normalize_module_id(module);
    const std::string perBegin = "# VIX_MODULE_LINK_BEGIN " + modNorm;
    const std::string perEnd = "# VIX_MODULE_LINK_END " + modNorm;

    if (content.find(perBegin) != std::string::npos && content.find(perEnd) != std::string::npos)
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

  static std::string module_cmakelists_txt_app_first(const std::string &project, const std::string &module)
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

  static std::string module_public_header_app_first(const std::string &project, const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string guard = to_lower(normalize_module_id(project)) + "_" + to_lower(normalized) + "_api_hpp";

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

  static std::string module_impl_cpp_app_first(const std::string &project, const std::string &module)
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

  static bool cmd_init(const fs::path &root, bool patchRoot)
  {
    const fs::path modulesDir = root / "modules";
    const fs::path cmakeDir = root / "cmake";
    const fs::path vixCmake = cmakeDir / "vix_modules.cmake";

    if (!ensure_dir(modulesDir))
    {
      ui::err_line(std::cout, "Failed to create modules/ directory.");
      return false;
    }

    if (!ensure_dir(cmakeDir))
    {
      ui::err_line(std::cout, "Failed to create cmake/ directory.");
      return false;
    }

    if (!write_file_if_missing(vixCmake, cmake_vix_modules_cmake_app_first()))
    {
      ui::err_line(std::cout, "Failed to write cmake/vix_modules.cmake.");
      return false;
    }

    if (patchRoot)
    {
      if (!patch_root_cmakelists_include(root))
      {
        ui::err_line(std::cout, "Failed to patch root CMakeLists.txt.");
        return false;
      }
    }

    ui::ok_line(std::cout, "Modules mode initialized");
    ui::kv(std::cout, "modules", (root / "modules").string(), 10);
    ui::kv(std::cout, "cmake", (root / "cmake" / "vix_modules.cmake").string(), 10);
    if (patchRoot)
      ui::kv(std::cout, "patch", "root CMakeLists.txt updated (idempotent markers)", 10);
    ui::warn_line(std::cout, "Next: vix modules add <name>");

    return true;
  }

  static bool cmd_add(const fs::path &root, const std::string &project, const std::string &module, bool patchRootLink)
  {
    if (!is_valid_module_name(module))
    {
      ui::err_line(std::cout, "Invalid module name: " + module);
      ui::warn_line(std::cout, "Allowed: [A-Za-z0-9_-]");
      return false;
    }

    if (is_reserved_module_name(module))
    {
      ui::err_line(std::cout, "Reserved module name: " + module);
      ui::warn_line(std::cout, "Pick a domain name (auth, orders, billing, ...). Avoid tool/library names.");
      return false;
    }

    const std::string normalized = normalize_module_id(module);

    const fs::path modulesDir = root / "modules";
    const fs::path moduleDir = modulesDir / normalized;

    const fs::path includeDir = moduleDir / "include" / normalized;
    const fs::path srcDir = moduleDir / "src";

    if (!exists_dir(modulesDir))
    {
      ui::err_line(std::cout, "modules/ folder not found.");
      ui::warn_line(std::cout, "Run: vix modules init");
      return false;
    }

    if (exists_dir(moduleDir))
    {
      ui::err_line(std::cout, "Module already exists: modules/" + normalized);
      return false;
    }

    if (!ensure_dir(includeDir))
    {
      ui::err_line(std::cout, "Failed to create module include directory.");
      return false;
    }

    if (!ensure_dir(srcDir))
    {
      ui::err_line(std::cout, "Failed to create module src directory.");
      return false;
    }

    const fs::path cmakeLists = moduleDir / "CMakeLists.txt";
    const fs::path header = includeDir / "api.hpp";
    const fs::path impl = srcDir / (normalized + ".cpp");

    if (!write_file_if_missing(cmakeLists, module_cmakelists_txt_app_first(project, module)))
    {
      ui::err_line(std::cout, "Failed to write module CMakeLists.txt.");
      return false;
    }

    if (!write_file_if_missing(header, module_public_header_app_first(project, module)))
    {
      ui::err_line(std::cout, "Failed to write public header.");
      return false;
    }

    if (!write_file_if_missing(impl, module_impl_cpp_app_first(project, module)))
    {
      ui::err_line(std::cout, "Failed to write module implementation.");
      return false;
    }

    if (patchRootLink)
    {
      if (!patch_root_cmakelists_link_module(root, project, module))
      {
        ui::err_line(std::cout, "Failed to patch root CMakeLists.txt with module link.");
        return false;
      }
    }

    ui::ok_line(std::cout, "Module created");
    ui::kv(std::cout, "name", normalized, 10);
    ui::kv(std::cout, "target", module_alias_name(project, module), 10);
    ui::kv(std::cout, "public", ("modules/" + normalized + "/include/" + normalized + "/api.hpp"), 10);
    ui::kv(std::cout, "impl", ("modules/" + normalized + "/src/" + normalized + ".cpp"), 10);
    ui::warn_line(std::cout, "Next steps (CMake):");
    std::cout << "    " << GRAY << "• " << RESET << "Include: " << YELLOW << BOLD << "#include <" << normalized << "/api.hpp>" << RESET << "\n";
    if (patchRootLink)
      std::cout << "    " << GRAY << "• " << RESET << "Root: " << GRAY << "(auto-linked if main target is named like project(" << project << "))" << RESET << "\n";

    return true;
  }

  static std::vector<fs::path> list_files_recursive(const fs::path &dir)
  {
    std::vector<fs::path> out;
    std::error_code ec{};
    if (!fs::exists(dir, ec) || ec)
      return out;

    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); ++it)
    {
      std::error_code ec2{};
      if (it->is_regular_file(ec2) && !ec2)
        out.push_back(it->path());
    }
    return out;
  }

  static std::unordered_set<std::string> parse_declared_deps_from_module_cmake(
      const fs::path &moduleCmake,
      const std::string &project)
  {
    std::unordered_set<std::string> deps;

    auto content = read_file(moduleCmake);
    if (!content)
      return deps;

    std::string s = *content;

    const std::string needle = project + "::";
    size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos)
    {
      size_t start = pos + needle.size();
      size_t end = start;
      while (end < s.size())
      {
        char c = s[end];
        bool ok = std::isalnum((unsigned char)c) || c == '_' || c == '-';
        if (!ok)
          break;
        ++end;
      }

      if (end > start)
      {
        std::string mod = s.substr(start, end - start);
        deps.insert(mod);
      }

      pos = end;
    }

    return deps;
  }

  static std::set<std::string> parse_public_includes_for_cross_module(
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
      if (first.empty())
        continue;

      if (exists_dir(modulesDir / first))
        used.insert(first);
    }

    return used;
  }

  static bool header_includes_private_impl(const fs::path &publicHeader, const fs::path &moduleDir)
  {
    auto content = read_file(publicHeader);
    if (!content)
      return false;

    const std::string s = *content;

    if (s.find("\"src/") != std::string::npos)
      return true;
    if (s.find("../src/") != std::string::npos)
      return true;
    if (s.find("/src/") != std::string::npos)
      return true;

    const std::string mod = moduleDir.filename().string();
    if (s.find("modules/" + mod + "/src/") != std::string::npos)
      return true;

    return false;
  }

  static bool cmd_check(const fs::path &root, const std::string &project)
  {
    const fs::path modulesDir = root / "modules";
    if (!exists_dir(modulesDir))
    {
      ui::err_line(std::cout, "modules/ folder not found.");
      ui::warn_line(std::cout, "Run: vix modules init");
      return false;
    }

    std::vector<fs::path> moduleDirs;
    std::error_code ec{};
    for (auto it = fs::directory_iterator(modulesDir, ec); !ec && it != fs::directory_iterator(); ++it)
    {
      std::error_code ec2{};
      if (!it->is_directory(ec2) || ec2)
        continue;

      fs::path dir = it->path();
      if (exists_file(dir / "CMakeLists.txt"))
        moduleDirs.push_back(dir);
    }

    if (moduleDirs.empty())
    {
      ui::warn_line(std::cout, "No modules found in modules/* (no CMakeLists.txt).");
      return true;
    }

    bool ok = true;
    std::size_t scannedHeaders = 0;
    std::size_t violations = 0;

    std::unordered_map<std::string, std::unordered_set<std::string>> declaredDeps;
    for (const auto &dir : moduleDirs)
    {
      std::string mod = dir.filename().string();
      declaredDeps[mod] = parse_declared_deps_from_module_cmake(dir / "CMakeLists.txt", project);
    }

    for (const auto &dir : moduleDirs)
    {
      const std::string mod = dir.filename().string();
      const fs::path includeRoot = dir / "include" / mod;

      if (!exists_dir(includeRoot))
        continue;

      auto files = list_files_recursive(includeRoot);
      for (const auto &f : files)
      {
        if (!ends_with(f.string(), ".hpp") && !ends_with(f.string(), ".h") && !ends_with(f.string(), ".hh"))
          continue;

        ++scannedHeaders;

        if (header_includes_private_impl(f, dir))
        {
          ok = false;
          ++violations;
          ui::err_line(std::cout, "Illegal include (public header includes private impl)");
          ui::kv(std::cout, "module", mod, 10);
          ui::kv(std::cout, "file", f.string(), 10);
        }

        auto used = parse_public_includes_for_cross_module(f, modulesDir);
        for (const auto &otherRaw : used)
        {
          const std::string other = normalize_module_id(otherRaw);
          if (other == normalize_module_id(mod))
            continue;

          const auto it = declaredDeps.find(mod);
          const bool declared = (it != declaredDeps.end() && it->second.find(other) != it->second.end());

          if (!declared)
          {
            ok = false;
            ++violations;
            ui::err_line(std::cout, "Missing explicit module dependency (include without link)");
            ui::kv(std::cout, "module", mod, 10);
            ui::kv(std::cout, "header", f.string(), 10);
            ui::kv(std::cout, "uses", "<" + other + "/...>", 10);
            ui::warn_line(std::cout, "Fix (module CMakeLists.txt):");
            std::cout << "\n"
                      << "    " << GRAY << "target_link_libraries(" << RESET
                      << YELLOW << module_target_name(project, mod) << RESET
                      << GRAY << " PUBLIC " << RESET
                      << YELLOW << project << "::" << other << RESET
                      << GRAY << ")" << RESET << "\n\n";
          }
        }
      }
    }

    if (ok)
    {
      ui::ok_line(std::cout, "Modules check passed");
      ui::kv(std::cout, "modules", std::to_string(moduleDirs.size()), 10);
      ui::kv(std::cout, "headers", std::to_string(scannedHeaders), 10);
      return true;
    }

    ui::err_line(std::cout, "Modules check failed");
    ui::kv(std::cout, "modules", std::to_string(moduleDirs.size()), 10);
    ui::kv(std::cout, "headers", std::to_string(scannedHeaders), 10);
    ui::kv(std::cout, "issues", std::to_string(violations), 10);
    return false;
  }

  struct Options
  {
    std::string subcmd;
    std::string dir;
    std::string project;
    std::string module;

    bool patchRoot = true;
    bool patchLink = true;
    bool showHelp = false;
  };

  static Options parse_args(const std::vector<std::string> &args)
  {
    Options o;

    if (!args.empty())
      o.subcmd = args[0];

    auto is_opt = [](const std::string &s)
    { return !s.empty() && s[0] == '-'; };

    for (size_t i = 0; i < args.size(); ++i)
    {
      const auto &a = args[i];

      if (a == "-h" || a == "--help")
      {
        o.showHelp = true;
      }
      else if (a == "-d" || a == "--dir")
      {
        if (i + 1 < args.size() && !is_opt(args[i + 1]))
          o.dir = args[++i];
      }
      else if (starts_with(a, "--dir="))
      {
        o.dir = a.substr(std::string("--dir=").size());
      }
      else if (a == "--project")
      {
        if (i + 1 < args.size() && !is_opt(args[i + 1]))
          o.project = args[++i];
      }
      else if (starts_with(a, "--project="))
      {
        o.project = a.substr(std::string("--project=").size());
      }
      else if (a == "--no-patch")
      {
        o.patchRoot = false;
      }
      else if (a == "--patch")
      {
        o.patchRoot = true;
      }
      else if (a == "--no-link")
      {
        o.patchLink = false;
      }
      else if (a == "--link")
      {
        o.patchLink = true;
      }
    }

    if (o.subcmd == "add")
    {
      if (args.size() >= 2 && !is_opt(args[1]))
        o.module = args[1];
    }

    return o;
  }

  int run(const std::vector<std::string> &args)
  {
    Options opt = parse_args(args);

    if (opt.showHelp || opt.subcmd.empty() || opt.subcmd == "help")
      return help();

    const fs::path root = resolve_root(opt.dir);

    std::string project = opt.project;
    if (project.empty())
      project = detect_project_name_from_cmake(root);

    if (opt.subcmd == "init")
    {
      ui::section(std::cout, "Modules");
      const bool ok = cmd_init(root, opt.patchRoot);
      return ok ? 0 : 1;
    }

    if (opt.subcmd == "add")
    {
      if (opt.module.empty())
      {
        ui::err_line(std::cout, "Missing module name.");
        ui::warn_line(std::cout, "Usage: vix modules add <name>");
        return 1;
      }

      ui::section(std::cout, "Modules");
      const bool ok = cmd_add(root, project, opt.module, opt.patchLink);
      return ok ? 0 : 1;
    }

    if (opt.subcmd == "check")
    {
      ui::section(std::cout, "Modules");
      const bool ok = cmd_check(root, project);
      return ok ? 0 : 1;
    }

    ui::err_line(std::cout, "Unknown subcommand: " + opt.subcmd);
    ui::warn_line(std::cout, "Run: vix modules --help");
    return 1;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix modules <subcommand> [options]\n\n";

    out << "Goal:\n";
    out << "  Enable an app-first, Go-like modules organization layer for any existing CMake project.\n";
    out << "  Modules are enforced through strict PUBLIC/PRIVATE boundaries, explicit deps, and safety checks.\n\n";

    out << "Subcommands:\n";
    out << "  init                 Initialize modules mode (creates modules/ + cmake/vix_modules.cmake)\n";
    out << "  add <name>           Create a module skeleton and a target <project>::<name>\n";
    out << "  check                Validate module safety rules (public/private + explicit deps)\n\n";

    out << "Options:\n";
    out << "  -d, --dir <path>         Project root (default: current)\n";
    out << "  --project <name>         Override project name (default: parsed from root CMakeLists.txt)\n";
    out << "  --no-patch               Do not patch root CMakeLists.txt (init only)\n";
    out << "  --patch                  Patch root CMakeLists.txt (default)\n";
    out << "  --no-link                Do not auto-link module target into main project target\n";
    out << "  --link                   Auto-link module target into main project target (default)\n";
    out << "  -h, --help               Show help\n\n";

    out << "Contract (Go-like):\n";
    out << "  modules/<m>/include/<m>/...  (public headers)\n";
    out << "  modules/<m>/src/...          (private impl)\n";
    out << "  Public include style: #include <m/api.hpp>\n";
    out << "  Cross-module usage must be explicit: target_link_libraries(<proj>_<m> PUBLIC <proj>::<other>)\n\n";

    out << "Examples:\n";
    out << "  vix modules init\n";
    out << "  vix modules add auth\n";
    out << "  vix modules add products\n";
    out << "  vix modules check\n\n";

    return 0;
  }

} // namespace vix::commands::ModulesCommand
