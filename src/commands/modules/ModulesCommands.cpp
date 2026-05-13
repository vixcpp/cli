/**
 * @file ModulesCommands.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/modules/ModulesCommands.hpp>
#include <vix/cli/commands/modules/ModulesContent.hpp>
#include <vix/cli/commands/modules/ModulesUtils.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

namespace vix::commands::modules_cmd::commands
{

  namespace fs = std::filesystem;
  namespace cnt = vix::commands::modules_cmd::content;
  namespace utils = vix::commands::modules_cmd::utils;
  namespace ui = vix::cli::util;
  using namespace vix::cli::style;

  static constexpr const char *TEAL = "\033[38;5;35m";

  static void sep()
  {
    std::cout << PAD << GRAY << "─────────────────────────────────────" << RESET << "\n";
  }

  static void section(const std::string &title)
  {
    std::cout << PAD << GRAY << title << RESET << "\n";
  }

  static void print_command_step(
      int index,
      const std::string &cmd,
      const std::string &hint = "")
  {
    std::cout << PAD
              << GRAY << index << RESET
              << "  "
              << CYAN << BOLD << cmd << RESET;

    if (!hint.empty())
      std::cout << "  " << GRAY << hint << RESET;

    std::cout << "\n";
  }

  static void print_modules_banner(
      const std::string &name,
      const std::string &kind)
  {
    std::cout << PAD << TEAL << BOLD << "✔" << RESET
              << "  " << TEAL << BOLD << name << RESET
              << "  " << GRAY << kind << RESET
              << "\n";
  }

  bool cmd_init(const fs::path &root, bool patchRoot)
  {
    const fs::path modulesDir = root / "modules";
    const fs::path cmakeDir = root / "cmake";
    const fs::path vixCmake = cmakeDir / "vix_modules.cmake";

    if (!utils::ensure_dir(modulesDir))
    {
      ui::err_line(std::cout, "Failed to create modules/ directory.");
      return false;
    }

    if (!utils::ensure_dir(cmakeDir))
    {
      ui::err_line(std::cout, "Failed to create cmake/ directory.");
      return false;
    }

    if (!utils::write_file_if_missing(vixCmake, cnt::cmake_vix_modules_cmake_app_first()))
    {
      ui::err_line(std::cout, "Failed to write cmake/vix_modules.cmake.");
      return false;
    }

    if (patchRoot && !cnt::patch_root_cmakelists_include(root))
    {
      ui::err_line(std::cout, "Failed to patch root CMakeLists.txt.");
      return false;
    }

    print_modules_banner("modules", "initialized");
    sep();

    section("files");
    print_command_step(1, "modules/", "module directory");
    print_command_step(2, "cmake/vix_modules.cmake", "module loader");

    if (patchRoot)
      print_command_step(3, "CMakeLists.txt", "patched");

    sep();
    section("next");
    print_command_step(1, "vix modules add <name>", "create module");

    return true;
  }

  bool cmd_add(
      const fs::path &root,
      const std::string &project,
      const std::string &module,
      bool patchRootLink)
  {
    if (!cnt::is_valid_module_name(module))
    {
      ui::err_line(std::cout, "Invalid module name: " + module);
      ui::warn_line(std::cout, "Allowed: [A-Za-z0-9_-]");
      return false;
    }

    if (cnt::is_reserved_module_name(module))
    {
      ui::err_line(std::cout, "Reserved module name: " + module);
      ui::warn_line(std::cout, "Pick a domain name (auth, orders, billing, ...). Avoid tool/library names.");
      return false;
    }

    const std::string normalized = cnt::normalize_module_id(module);

    const fs::path modulesDir = root / "modules";
    const fs::path moduleDir = modulesDir / normalized;
    const fs::path includeDir = moduleDir / "include" / normalized;
    const fs::path srcDir = moduleDir / "src";

    if (!utils::exists_dir(modulesDir))
    {
      ui::err_line(std::cout, "modules/ folder not found.");
      ui::warn_line(std::cout, "Run: vix modules init");
      return false;
    }

    if (utils::exists_dir(moduleDir))
    {
      ui::err_line(std::cout, "Module already exists: modules/" + normalized);
      return false;
    }

    if (!utils::ensure_dir(includeDir))
    {
      ui::err_line(std::cout, "Failed to create module include directory.");
      return false;
    }

    if (!utils::ensure_dir(srcDir))
    {
      ui::err_line(std::cout, "Failed to create module src directory.");
      return false;
    }

    const fs::path cmakeLists = moduleDir / "CMakeLists.txt";
    const fs::path header = includeDir / "api.hpp";
    const fs::path impl = srcDir / (normalized + ".cpp");

    if (!utils::write_file_if_missing(cmakeLists, cnt::module_cmakelists_txt_app_first(project, module)))
    {
      ui::err_line(std::cout, "Failed to write module CMakeLists.txt.");
      return false;
    }

    if (!utils::write_file_if_missing(header, cnt::module_public_header_app_first(project, module)))
    {
      ui::err_line(std::cout, "Failed to write public header.");
      return false;
    }

    if (!utils::write_file_if_missing(impl, cnt::module_impl_cpp_app_first(project, module)))
    {
      ui::err_line(std::cout, "Failed to write module implementation.");
      return false;
    }

    if (patchRootLink && !cnt::patch_root_cmakelists_link_module(root, project, module))
    {
      ui::err_line(std::cout, "Failed to patch root CMakeLists.txt with module link.");
      return false;
    }

    print_modules_banner(normalized, "module");
    sep();

    section("files");
    print_command_step(
        1,
        "modules/" + normalized + "/include/" + normalized + "/api.hpp",
        "public header");

    print_command_step(
        2,
        "modules/" + normalized + "/src/" + normalized + ".cpp",
        "implementation");

    print_command_step(
        3,
        "modules/" + normalized + "/CMakeLists.txt",
        "target");

    sep();
    section("target");
    print_command_step(1, cnt::module_alias_name(project, module), "CMake alias");

    sep();
    section("next");
    print_command_step(
        1,
        "#include <" + normalized + "/api.hpp>",
        "include");

    if (patchRootLink)
    {
      print_command_step(2, "vix build", "compile");
    }
    else
    {
      print_command_step(
          2,
          "target_link_libraries(" + project + " PRIVATE " + cnt::module_alias_name(project, module) + ")",
          "link manually");
    }

    return true;
  }

  bool cmd_check(const fs::path &root, const std::string &project)
  {
    const fs::path modulesDir = root / "modules";

    if (!utils::exists_dir(modulesDir))
    {
      ui::err_line(std::cout, "modules/ folder not found.");
      ui::warn_line(std::cout, "Run: vix modules init");
      return false;
    }

    // Collect module directories (only those with a CMakeLists.txt)
    std::vector<fs::path> moduleDirs;
    std::error_code ec{};
    for (auto it = fs::directory_iterator(modulesDir, ec);
         !ec && it != fs::directory_iterator();
         ++it)
    {
      std::error_code ec2{};
      if (!it->is_directory(ec2) || ec2)
        continue;
      if (utils::exists_file(it->path() / "CMakeLists.txt"))
        moduleDirs.push_back(it->path());
    }

    if (moduleDirs.empty())
    {
      ui::warn_line(std::cout, "No modules found in modules/* (no CMakeLists.txt).");
      return true;
    }

    // Pre-compute declared deps for each module
    std::unordered_map<std::string, std::unordered_set<std::string>> declaredDeps;
    for (const auto &dir : moduleDirs)
    {
      const std::string mod = dir.filename().string();
      declaredDeps[mod] = cnt::parse_declared_deps_from_module_cmake(
          dir / "CMakeLists.txt", project);
    }

    bool ok = true;
    std::size_t scannedHeaders = 0;
    std::size_t violations = 0;

    for (const auto &dir : moduleDirs)
    {
      const std::string mod = dir.filename().string();
      const fs::path includeRoot = dir / "include" / mod;

      if (!utils::exists_dir(includeRoot))
        continue;

      for (const auto &f : utils::list_files_recursive(includeRoot))
      {
        const std::string fstr = f.string();
        if (!utils::ends_with(fstr, ".hpp") &&
            !utils::ends_with(fstr, ".h") &&
            !utils::ends_with(fstr, ".hh"))
          continue;

        ++scannedHeaders;

        // Rule 1: public header must not include private impl
        if (cnt::header_includes_private_impl(f, dir))
        {
          ok = false;
          ++violations;
          ui::err_line(std::cout, "Illegal include (public header includes private impl)");
          ui::kv(std::cout, "module", mod, 10);
          ui::kv(std::cout, "file", fstr, 10);
        }

        // Rule 2: cross-module include must be declared in CMakeLists.txt
        for (const auto &otherRaw : cnt::parse_public_includes_for_cross_module(f, modulesDir))
        {
          const std::string other = cnt::normalize_module_id(otherRaw);
          if (other == cnt::normalize_module_id(mod))
            continue;

          const auto it = declaredDeps.find(mod);
          const bool declared =
              (it != declaredDeps.end() && it->second.find(other) != it->second.end());

          if (!declared)
          {
            ok = false;
            ++violations;
            ui::err_line(std::cout, "Missing explicit module dependency (include without link)");
            ui::kv(std::cout, "module", mod, 10);
            ui::kv(std::cout, "header", fstr, 10);
            ui::kv(std::cout, "uses", "<" + other + "/...>", 10);
            ui::warn_line(std::cout, "Fix (module CMakeLists.txt):");
            std::cout << "\n"
                      << "    " << GRAY << "target_link_libraries(" << RESET
                      << YELLOW << cnt::module_target_name(project, mod) << RESET
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

} // namespace vix::commands::modules_cmd::commands
