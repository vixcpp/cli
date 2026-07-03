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
#include <vix/cli/app/AppManifest.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

  static std::string join_strings(
      const std::vector<std::string> &values,
      const std::string &separator)
  {
    if (values.empty())
      return "-";

    std::ostringstream out;

    for (std::size_t i = 0; i < values.size(); ++i)
    {
      if (i > 0)
        out << separator;

      out << values[i];
    }

    return out.str();
  }

  static std::string enabled_text(bool enabled)
  {
    return enabled ? "enabled" : "disabled";
  }

  static std::string path_status(
      const fs::path &root,
      const std::string &path)
  {
    if (path.empty())
      return "missing-path";

    return utils::exists_dir(root / path) ? "ok" : "missing";
  }

  static std::string trim_copy(std::string value)
  {
    auto is_space = [](unsigned char c)
    {
      return std::isspace(c) != 0;
    };

    while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
      value.erase(value.begin());

    while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
      value.pop_back();

    return value;
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

  static std::string leading_whitespace(const std::string &line)
  {
    std::string out;

    for (char c : line)
    {
      if (c == ' ' || c == '\t')
        out.push_back(c);
      else
        break;
    }

    return out;
  }

  static bool parse_bool_text(
      const std::string &raw,
      bool &out)
  {
    const std::string value =
        lower_copy(strip_quotes(trim_copy(raw)));

    if (value == "true" ||
        value == "yes" ||
        value == "on" ||
        value == "1")
    {
      out = true;
      return true;
    }

    if (value == "false" ||
        value == "no" ||
        value == "off" ||
        value == "0")
    {
      out = false;
      return true;
    }

    return false;
  }

  static bool is_section_header(const std::string &line)
  {
    const std::string s =
        trim_copy(strip_inline_comment(line));

    return s.size() >= 2 &&
           s.front() == '[' &&
           s.back() == ']';
  }

  static bool module_section_name(
      const std::string &line,
      std::string &name)
  {
    const std::string s =
        trim_copy(strip_inline_comment(line));

    if (s.size() <= std::string("[module.]").size())
      return false;

    if (!utils::starts_with(s, "[module."))
      return false;

    if (s.back() != ']')
      return false;

    const std::string rawName =
        s.substr(
            std::string("[module.").size(),
            s.size() - std::string("[module.").size() - 1);

    name = cnt::normalize_module_id(rawName);

    return !name.empty() && cnt::is_valid_module_name(name);
  }

  static bool is_target_module_section(
      const std::string &line,
      const std::string &module)
  {
    std::string current;

    if (!module_section_name(line, current))
      return false;

    return cnt::normalize_module_id(current) ==
           cnt::normalize_module_id(module);
  }

  static std::vector<std::string> split_lines_preserve(
      const std::string &text)
  {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;

    while (std::getline(in, line))
      lines.push_back(line);

    if (!text.empty() && text.back() == '\n')
      lines.push_back("");

    return lines;
  }

  static std::string join_lines(
      const std::vector<std::string> &lines)
  {
    std::ostringstream out;

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
      if (i + 1 == lines.size() && lines[i].empty())
        break;

      out << lines[i] << "\n";
    }

    return out.str();
  }

  static bool set_module_enabled_in_vix_app(
      const fs::path &root,
      const std::string &moduleRaw,
      bool enabled)
  {
    const fs::path appPath = root / "vix.app";

    if (!utils::exists_file(appPath))
    {
      ui::err_line(std::cout, "vix.app not found.");
      ui::warn_line(std::cout, "Run this command inside a vix.app project.");
      return false;
    }

    const std::string module =
        cnt::normalize_module_id(moduleRaw);

    if (!cnt::is_valid_module_name(module))
    {
      ui::err_line(std::cout, "Invalid module name: " + moduleRaw);
      ui::warn_line(std::cout, "Allowed: [A-Za-z0-9_-]");
      return false;
    }

    auto contentOpt = utils::read_file(appPath);

    if (!contentOpt)
    {
      ui::err_line(std::cout, "Failed to read vix.app.");
      return false;
    }

    std::vector<std::string> lines =
        split_lines_preserve(*contentOpt);

    std::size_t sectionStart = lines.size();
    std::size_t sectionEnd = lines.size();

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
      if (!is_target_module_section(lines[i], module))
        continue;

      sectionStart = i;
      sectionEnd = lines.size();

      for (std::size_t j = i + 1; j < lines.size(); ++j)
      {
        if (is_section_header(lines[j]))
        {
          sectionEnd = j;
          break;
        }
      }

      break;
    }

    if (sectionStart == lines.size())
    {
      ui::err_line(
          std::cout,
          "Module is not declared in vix.app: " + module);

      ui::warn_line(
          std::cout,
          "Declare it first using [module." + module + "]");

      return false;
    }

    const std::string desired =
        enabled ? "true" : "false";

    bool foundEnabled = false;
    bool alreadyDesired = false;

    for (std::size_t i = sectionStart + 1; i < sectionEnd; ++i)
    {
      const std::string stripped =
          trim_copy(strip_inline_comment(lines[i]));

      const auto eq = stripped.find('=');

      if (eq == std::string::npos)
        continue;

      const std::string key =
          lower_copy(trim_copy(stripped.substr(0, eq)));

      if (key != "enabled")
        continue;

      foundEnabled = true;

      const std::string value =
          trim_copy(stripped.substr(eq + 1));

      bool current = true;

      if (parse_bool_text(value, current))
        alreadyDesired = current == enabled;

      const std::string indent =
          leading_whitespace(lines[i]);

      lines[i] = indent + "enabled = " + desired;
      break;
    }

    if (!foundEnabled)
    {
      lines.insert(
          lines.begin() + static_cast<std::ptrdiff_t>(sectionStart + 1),
          "enabled = " + desired);
    }

    if (alreadyDesired)
    {
      print_modules_banner(module, enabled ? "already enabled" : "already disabled");
      return true;
    }

    if (!utils::write_file_overwrite(appPath, join_lines(lines)))
    {
      ui::err_line(std::cout, "Failed to update vix.app.");
      return false;
    }

    print_modules_banner(module, enabled ? "enabled" : "disabled");
    sep();
    ui::kv(std::cout, "file", "vix.app", 10);
    ui::kv(std::cout, "module", module, 10);
    ui::kv(std::cout, "enabled", desired, 10);

    return true;
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

    const bool hasRootCMake =
        utils::exists_file(root / "CMakeLists.txt");

    const bool hasVixApp =
        utils::exists_file(root / "vix.app");

    if (patchRoot && hasRootCMake)
    {
      if (!cnt::patch_root_cmakelists_include(root))
      {
        ui::err_line(std::cout, "Failed to patch root CMakeLists.txt.");
        return false;
      }
    }
    else if (patchRoot && hasVixApp)
    {
      ui::warn_line(
          std::cout,
          "vix.app project detected. CMake patch skipped.");
    }
    else if (patchRoot)
    {
      ui::warn_line(
          std::cout,
          "CMakeLists.txt not found. Skipping root patch.");
    }

    print_modules_banner("modules", "initialized");
    sep();

    section("files");
    print_command_step(1, "modules/", "module directory");
    print_command_step(2, "cmake/vix_modules.cmake", "module loader");

    if (patchRoot && hasRootCMake)
    {
      print_command_step(3, "CMakeLists.txt", "patched");
    }

    sep();
    section("next");
    print_command_step(1, "vix modules add <name>", "create module");

    return true;
  }

  static std::string detect_vix_app_kind(const fs::path &root)
  {
    const fs::path appPath = root / "vix.app";

    auto contentOpt = utils::read_file(appPath);

    if (!contentOpt)
      return "module";

    std::istringstream in(*contentOpt);
    std::string line;

    while (std::getline(in, line))
    {
      const std::string stripped =
          trim_copy(strip_inline_comment(line));

      const auto eq = stripped.find('=');

      if (eq == std::string::npos)
        continue;

      const std::string key =
          lower_copy(trim_copy(stripped.substr(0, eq)));

      if (key != "type" && key != "kind" && key != "app_kind")
        continue;

      const std::string value =
          lower_copy(strip_quotes(trim_copy(stripped.substr(eq + 1))));

      if (value == "backend")
        return "backend";
    }

    // The current backend template still writes:
    // type = "executable"
    // so we detect backend apps from the scaffold shape.
    if (utils::exists_dir(root / "src") &&
        utils::exists_dir(root / "views") &&
        utils::exists_dir(root / "public"))
    {
      return "backend";
    }

    return "module";
  }

  static bool append_module_section_to_vix_app(
      const fs::path &root,
      const std::string &moduleRaw)
  {
    const fs::path appPath = root / "vix.app";

    if (!utils::exists_file(appPath))
      return false;

    const std::string module =
        cnt::normalize_module_id(moduleRaw);

    if (!cnt::is_valid_module_name(module))
    {
      ui::err_line(std::cout, "Invalid module name: " + moduleRaw);
      ui::warn_line(std::cout, "Allowed: [A-Za-z0-9_-]");
      return false;
    }

    auto contentOpt = utils::read_file(appPath);

    if (!contentOpt)
    {
      ui::err_line(std::cout, "Failed to read vix.app.");
      return false;
    }

    const std::string &content = *contentOpt;

    std::istringstream in(content);
    std::string line;

    while (std::getline(in, line))
    {
      std::string existing;

      if (module_section_name(line, existing) &&
          cnt::normalize_module_id(existing) == module)
      {
        return true;
      }
    }

    const std::string kind = detect_vix_app_kind(root);

    std::ostringstream out;
    out << content;

    if (!content.empty() && content.back() != '\n')
      out << "\n";

    out << "\n";
    out << "[module." << module << "]\n";
    out << "enabled = true\n";
    out << "path = modules/" << module << "\n";
    out << "kind = " << kind << "\n";

    if (!utils::write_file_overwrite(appPath, out.str()))
    {
      ui::err_line(std::cout, "Failed to update vix.app.");
      return false;
    }

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
      ui::warn_line(
          std::cout,
          "Pick a domain name (auth, orders, billing, ...). Avoid tool/library names.");
      return false;
    }

    const std::string normalized = cnt::normalize_module_id(module);

    const fs::path modulesDir = root / "modules";
    const fs::path moduleDir = modulesDir / normalized;

    const bool hasRootCMake =
        utils::exists_file(root / "CMakeLists.txt");

    const bool hasVixApp =
        utils::exists_file(root / "vix.app");

    const bool isBackendModule =
        hasVixApp && detect_vix_app_kind(root) == "backend";

    const fs::path includeDir = moduleDir / "include" / normalized;
    const fs::path srcDir = moduleDir / "src";
    const fs::path migrationsDir = moduleDir / "migrations";
    const fs::path testsDir = moduleDir / "tests";

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

    if (isBackendModule)
    {
      if (!utils::ensure_dir(includeDir / "controllers"))
      {
        ui::err_line(
            std::cout,
            "Failed to create backend module controllers include directory.");
        return false;
      }

      if (!utils::ensure_dir(srcDir / "controllers"))
      {
        ui::err_line(
            std::cout,
            "Failed to create backend module controllers source directory.");
        return false;
      }

      if (!utils::ensure_dir(migrationsDir))
      {
        ui::err_line(
            std::cout,
            "Failed to create backend module migrations directory.");
        return false;
      }

      if (!utils::ensure_dir(testsDir))
      {
        ui::err_line(
            std::cout,
            "Failed to create backend module tests directory.");
        return false;
      }
    }
    else
    {
      if (!utils::ensure_dir(testsDir))
      {
        ui::err_line(
            std::cout,
            "Failed to create module tests directory.");
        return false;
      }
    }

    const fs::path cmakeLists = moduleDir / "CMakeLists.txt";

    if (isBackendModule)
    {
      const std::string classBase =
          cnt::module_class_name(normalized);

      const fs::path moduleHeader =
          includeDir / (classBase + "Module.hpp");

      const fs::path moduleImpl =
          srcDir / (classBase + "Module.cpp");

      const fs::path controllerHeader =
          includeDir / "controllers" / (classBase + "Controller.hpp");

      const fs::path controllerImpl =
          srcDir / "controllers" / (classBase + "Controller.cpp");

      const fs::path moduleManifest =
          moduleDir / "vix.module";

      const fs::path testFile =
          testsDir / ("test_" + normalized + ".cpp");

      if (!utils::write_file_if_missing(
              cmakeLists,
              cnt::module_backend_cmakelists_txt_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write backend module CMakeLists.txt.");
        return false;
      }

      if (!utils::write_file_if_missing(
              moduleManifest,
              cnt::module_backend_manifest_app_first(normalized)))
      {
        ui::err_line(std::cout, "Failed to write vix.module.");
        return false;
      }

      if (!utils::write_file_if_missing(
              moduleHeader,
              cnt::module_backend_header_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write backend module header.");
        return false;
      }

      if (!utils::write_file_if_missing(
              moduleImpl,
              cnt::module_backend_impl_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write backend module implementation.");
        return false;
      }

      if (!utils::write_file_if_missing(
              controllerHeader,
              cnt::module_backend_controller_header_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write backend controller header.");
        return false;
      }

      if (!utils::write_file_if_missing(
              controllerImpl,
              cnt::module_backend_controller_impl_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write backend controller implementation.");
        return false;
      }

      utils::write_file_if_missing(migrationsDir / ".gitkeep", "");

      if (!utils::write_file_if_missing(
              testFile,
              cnt::module_backend_test_cpp_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write backend module test.");
        return false;
      }
    }
    else
    {
      const fs::path header =
          includeDir / "api.hpp";

      const fs::path impl =
          srcDir / (normalized + ".cpp");

      const fs::path moduleManifest =
          moduleDir / "vix.module";

      const fs::path testFile =
          testsDir / ("test_" + normalized + ".cpp");

      if (!utils::write_file_if_missing(
              cmakeLists,
              cnt::module_cmakelists_txt_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write module CMakeLists.txt.");
        return false;
      }

      if (!utils::write_file_if_missing(
              moduleManifest,
              cnt::module_manifest_app_first(normalized)))
      {
        ui::err_line(std::cout, "Failed to write vix.module.");
        return false;
      }

      if (!utils::write_file_if_missing(
              testFile,
              cnt::module_test_cpp_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write module test.");
        return false;
      }

      if (!utils::write_file_if_missing(
              header,
              cnt::module_public_header_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write public header.");
        return false;
      }

      if (!utils::write_file_if_missing(
              impl,
              cnt::module_impl_cpp_app_first(project, normalized)))
      {
        ui::err_line(std::cout, "Failed to write module implementation.");
        return false;
      }
    }

    bool registeredInVixApp = false;

    if (patchRootLink && hasRootCMake)
    {
      if (!cnt::patch_root_cmakelists_link_module(root, project, normalized))
      {
        ui::err_line(
            std::cout,
            "Failed to patch root CMakeLists.txt with module link.");
        return false;
      }
    }
    else if (patchRootLink && hasVixApp)
    {
      if (!append_module_section_to_vix_app(root, normalized))
      {
        ui::err_line(std::cout, "Failed to register module in vix.app.");
        return false;
      }

      registeredInVixApp = true;
    }
    else if (patchRootLink)
    {
      ui::warn_line(
          std::cout,
          "CMakeLists.txt not found. Skipping auto-link.");
    }

    print_modules_banner(normalized, "module created");
    sep();

    section("files");

    print_command_step(
        1,
        "modules/" + normalized + "/");

    if (isBackendModule)
    {
      const std::string classBase =
          cnt::module_class_name(normalized);

      print_command_step(
          2,
          "modules/" + normalized + "/include/" + normalized + "/" +
              classBase + "Module.hpp");

      print_command_step(
          3,
          "modules/" + normalized + "/src/" +
              classBase + "Module.cpp");

      print_command_step(
          4,
          "modules/" + normalized + "/include/" + normalized +
              "/controllers/" + classBase + "Controller.hpp");

      print_command_step(
          5,
          "modules/" + normalized + "/src/controllers/" +
              classBase + "Controller.cpp");

      print_command_step(
          6,
          "modules/" + normalized + "/vix.module");

      print_command_step(
          7,
          "modules/" + normalized + "/tests/test_" + normalized + ".cpp");
    }
    else
    {
      print_command_step(
          2,
          "modules/" + normalized + "/include/" + normalized + "/api.hpp");

      print_command_step(
          3,
          "modules/" + normalized + "/src/" + normalized + ".cpp");

      print_command_step(
          4,
          "modules/" + normalized + "/vix.module");

      print_command_step(
          5,
          "modules/" + normalized + "/tests/test_" + normalized + ".cpp");
    }

    sep();
    section("next");

    if (hasVixApp)
    {
      if (registeredInVixApp)
      {
        print_command_step(
            1,
            "vix modules list",
            "verify module");

        print_command_step(
            2,
            "vix build",
            "compile");
      }
      else
      {
        print_command_step(
            1,
            "add [module." + normalized + "] to vix.app");

        print_command_step(
            2,
            "vix build",
            "compile");
      }
    }
    else if (patchRootLink && hasRootCMake)
    {
      if (isBackendModule)
      {
        const std::string classBase =
            cnt::module_class_name(normalized);

        print_command_step(
            1,
            "#include <" + normalized + "/" + classBase + "Module.hpp>");
      }
      else
      {
        print_command_step(
            1,
            "#include <" + normalized + "/api.hpp>");
      }

      print_command_step(
          2,
          "vix build",
          "compile");
    }
    else
    {
      if (isBackendModule)
      {
        const std::string classBase =
            cnt::module_class_name(normalized);

        print_command_step(
            1,
            "#include <" + normalized + "/" + classBase + "Module.hpp>");
      }
      else
      {
        print_command_step(
            1,
            "#include <" + normalized + "/api.hpp>");
      }

      print_command_step(
          2,
          "target_link_libraries(" + project + " PRIVATE " +
              cnt::module_alias_name(project, normalized) + ")");

      print_command_step(
          3,
          "vix build",
          "compile");
    }

    return true;
  }

  bool cmd_list(const fs::path &root)
  {
    const fs::path appPath = root / "vix.app";

    if (!utils::exists_file(appPath))
    {
      ui::err_line(std::cout, "vix.app not found.");
      ui::warn_line(std::cout, "Run this command inside a vix.app project.");
      return false;
    }

    const vix::cli::app::AppManifestLoadResult loadResult =
        vix::cli::app::load_app_manifest(appPath);

    if (!loadResult.success())
    {
      ui::err_line(std::cout, "Failed to parse vix.app.");
      ui::warn_line(std::cout, loadResult.error);
      return false;
    }

    const auto &manifest = loadResult.manifest;

    print_modules_banner("modules", "list");
    sep();

    ui::kv(std::cout, "app", manifest.name, 12);
    ui::kv(std::cout, "type", manifest.appKind, 12);

    sep();

    if (!manifest.appModules.empty())
    {
      std::cout << PAD
                << std::left
                << std::setw(16) << "module"
                << std::setw(12) << "status"
                << std::setw(12) << "kind"
                << std::setw(28) << "path"
                << std::setw(10) << "fs"
                << "depends"
                << "\n";

      std::cout << PAD
                << GRAY
                << "────────────────────────────────────────────────────────────────────────────"
                << RESET
                << "\n";

      for (const auto &module : manifest.appModules)
      {
        const std::string kind =
            module.kind.empty() ? "-" : module.kind;

        const std::string path =
            module.path.empty() ? ("modules/" + module.name) : module.path;

        std::cout << PAD
                  << std::left
                  << std::setw(16) << module.name
                  << std::setw(12) << enabled_text(module.enabled)
                  << std::setw(12) << kind
                  << std::setw(28) << path
                  << std::setw(10) << path_status(root, path)
                  << join_strings(module.depends, ", ")
                  << "\n";
      }

      sep();

      ui::kv(
          std::cout,
          "enabled",
          std::to_string(manifest.modules.size()),
          12);

      ui::kv(
          std::cout,
          "declared",
          std::to_string(manifest.appModules.size()),
          12);

      return true;
    }

    if (!manifest.modules.empty())
    {
      ui::warn_line(
          std::cout,
          "Using legacy modules = [...] syntax. Prefer [module.<name>] sections.");

      sep();

      std::cout << PAD
                << std::left
                << std::setw(16) << "module"
                << std::setw(12) << "status"
                << std::setw(12) << "kind"
                << std::setw(28) << "path"
                << std::setw(10) << "fs"
                << "depends"
                << "\n";

      std::cout << PAD
                << GRAY
                << "────────────────────────────────────────────────────────────────────────────"
                << RESET
                << "\n";

      for (const std::string &module : manifest.modules)
      {
        const std::string normalized =
            cnt::normalize_module_id(module);

        const std::string path = "modules/" + normalized;

        std::cout << PAD
                  << std::left
                  << std::setw(16) << normalized
                  << std::setw(12) << "enabled"
                  << std::setw(12) << "module"
                  << std::setw(28) << path
                  << std::setw(10) << path_status(root, path)
                  << "-"
                  << "\n";
      }

      sep();

      ui::kv(
          std::cout,
          "enabled",
          std::to_string(manifest.modules.size()),
          12);

      ui::kv(
          std::cout,
          "declared",
          std::to_string(manifest.modules.size()),
          12);

      return true;
    }

    ui::warn_line(std::cout, "No modules declared in vix.app.");
    ui::warn_line(std::cout, "Add modules using [module.<name>] sections.");
    return true;
  }

  bool cmd_enable(
      const fs::path &root,
      const std::string &module)
  {
    return set_module_enabled_in_vix_app(root, module, true);
  }

  bool cmd_disable(
      const fs::path &root,
      const std::string &module)
  {
    return set_module_enabled_in_vix_app(root, module, false);
  }

  static std::string read_manifest_value(
      const fs::path &path,
      const std::string &section,
      const std::string &wantedKey)
  {
    auto contentOpt = utils::read_file(path);

    if (!contentOpt)
      return "";

    std::istringstream in(*contentOpt);
    std::string line;
    std::string activeSection;

    while (std::getline(in, line))
    {
      const std::string stripped =
          trim_copy(strip_inline_comment(line));

      if (stripped.empty())
        continue;

      if (is_section_header(stripped))
      {
        activeSection = stripped.substr(1, stripped.size() - 2);
        activeSection = lower_copy(trim_copy(activeSection));
        continue;
      }

      if (lower_copy(activeSection) != lower_copy(section))
        continue;

      const auto eq = stripped.find('=');

      if (eq == std::string::npos)
        continue;

      const std::string key =
          lower_copy(trim_copy(stripped.substr(0, eq)));

      if (key != lower_copy(wantedKey))
        continue;

      return strip_quotes(trim_copy(stripped.substr(eq + 1)));
    }

    return "";
  }

  static bool check_dependency_cycle_visit(
      const std::string &module,
      const std::unordered_map<std::string, std::vector<std::string>> &graph,
      std::unordered_set<std::string> &visiting,
      std::unordered_set<std::string> &visited,
      std::vector<std::string> &stack,
      std::vector<std::string> &cycle)
  {
    if (visited.find(module) != visited.end())
      return false;

    if (visiting.find(module) != visiting.end())
    {
      cycle.clear();

      bool capture = false;

      for (const std::string &item : stack)
      {
        if (item == module)
          capture = true;

        if (capture)
          cycle.push_back(item);
      }

      cycle.push_back(module);
      return true;
    }

    visiting.insert(module);
    stack.push_back(module);

    const auto it = graph.find(module);

    if (it != graph.end())
    {
      for (const std::string &dep : it->second)
      {
        if (check_dependency_cycle_visit(
                dep,
                graph,
                visiting,
                visited,
                stack,
                cycle))
        {
          return true;
        }
      }
    }

    stack.pop_back();
    visiting.erase(module);
    visited.insert(module);

    return false;
  }

  static bool check_dependency_cycles(
      const std::unordered_map<std::string, std::vector<std::string>> &graph,
      std::vector<std::string> &cycle)
  {
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;
    std::vector<std::string> stack;

    for (const auto &entry : graph)
    {
      if (check_dependency_cycle_visit(
              entry.first,
              graph,
              visiting,
              visited,
              stack,
              cycle))
      {
        return true;
      }
    }

    return false;
  }

  static std::string join_cycle(
      const std::vector<std::string> &cycle)
  {
    if (cycle.empty())
      return "-";

    std::ostringstream out;

    for (std::size_t i = 0; i < cycle.size(); ++i)
    {
      if (i > 0)
        out << " -> ";

      out << cycle[i];
    }

    return out.str();
  }

  bool cmd_check(const fs::path &root, const std::string &project)
  {
    const fs::path modulesDir = root / "modules";
    const fs::path appPath = root / "vix.app";
    const bool hasVixApp = utils::exists_file(appPath);

    if (!utils::exists_dir(modulesDir))
    {
      ui::err_line(std::cout, "modules/ folder not found.");
      ui::warn_line(std::cout, "Run: vix modules init");
      return false;
    }

    bool ok = true;
    std::size_t scannedHeaders = 0;
    std::size_t violations = 0;

    std::unordered_map<std::string, bool> declaredInApp;
    std::unordered_map<std::string, bool> enabledInApp;
    std::unordered_map<std::string, std::string> kindInApp;
    std::unordered_map<std::string, std::string> pathInApp;
    std::unordered_map<std::string, std::vector<std::string>> appDeps;

    if (hasVixApp)
    {
      const vix::cli::app::AppManifestLoadResult loadResult =
          vix::cli::app::load_app_manifest(appPath);

      if (!loadResult.success())
      {
        ui::err_line(std::cout, "Failed to parse vix.app.");
        ui::warn_line(std::cout, loadResult.error);
        return false;
      }

      for (const auto &module : loadResult.manifest.appModules)
      {
        const std::string name =
            cnt::normalize_module_id(module.name);

        declaredInApp[name] = true;
        enabledInApp[name] = module.enabled;
        kindInApp[name] = module.kind.empty() ? "module" : module.kind;
        pathInApp[name] = module.path.empty() ? ("modules/" + name) : module.path;

        for (const std::string &depRaw : module.depends)
        {
          appDeps[name].push_back(cnt::normalize_module_id(depRaw));
        }
      }
    }

    std::vector<fs::path> moduleDirs;
    std::error_code ec{};

    for (auto it = fs::directory_iterator(modulesDir, ec);
         !ec && it != fs::directory_iterator();
         ++it)
    {
      std::error_code ec2{};

      if (!it->is_directory(ec2) || ec2)
        continue;

      moduleDirs.push_back(it->path());
    }

    if (moduleDirs.empty())
    {
      ui::warn_line(std::cout, "No modules found in modules/*.");
      return true;
    }

    if (hasVixApp)
    {
      for (const auto &entry : declaredInApp)
      {
        const std::string &module = entry.first;
        const std::string path =
            pathInApp[module].empty() ? ("modules/" + module) : pathInApp[module];

        const fs::path modulePath = root / path;

        if (!utils::exists_dir(modulePath))
        {
          ok = false;
          ++violations;

          ui::err_line(std::cout, "Module declared in vix.app but folder is missing");
          ui::kv(std::cout, "module", module, 12);
          ui::kv(std::cout, "path", path, 12);
        }

        if (enabledInApp[module] && !utils::exists_file(modulePath / "CMakeLists.txt"))
        {
          ok = false;
          ++violations;

          ui::err_line(std::cout, "Enabled module is missing CMakeLists.txt");
          ui::kv(std::cout, "module", module, 12);
          ui::kv(std::cout, "path", (modulePath / "CMakeLists.txt").string(), 12);
        }

        if (enabledInApp[module] && !utils::exists_file(modulePath / "vix.module"))
        {
          ok = false;
          ++violations;

          ui::err_line(std::cout, "Enabled module is missing vix.module");
          ui::kv(std::cout, "module", module, 12);
          ui::kv(std::cout, "path", (modulePath / "vix.module").string(), 12);
        }
      }

      for (const auto &dir : moduleDirs)
      {
        const std::string module =
            cnt::normalize_module_id(dir.filename().string());

        if (declaredInApp.find(module) == declaredInApp.end())
        {
          ui::warn_line(
              std::cout,
              "Module folder exists but is not declared in vix.app: " + module);
        }
      }

      for (const auto &entry : appDeps)
      {
        const std::string &module = entry.first;

        for (const std::string &dep : entry.second)
        {
          if (declaredInApp.find(dep) == declaredInApp.end())
          {
            ok = false;
            ++violations;

            ui::err_line(std::cout, "Module depends on an undeclared module");
            ui::kv(std::cout, "module", module, 12);
            ui::kv(std::cout, "depends", dep, 12);
          }
          else if (enabledInApp[module] && !enabledInApp[dep])
          {
            ok = false;
            ++violations;

            ui::err_line(std::cout, "Enabled module depends on a disabled module");
            ui::kv(std::cout, "module", module, 12);
            ui::kv(std::cout, "depends", dep, 12);
          }
        }
      }

      std::vector<std::string> cycle;

      if (check_dependency_cycles(appDeps, cycle))
      {
        ok = false;
        ++violations;

        ui::err_line(std::cout, "Circular module dependency detected");
        ui::kv(std::cout, "cycle", join_cycle(cycle), 12);
      }
    }

    std::unordered_map<std::string, std::string> routePrefixes;

    for (const auto &dir : moduleDirs)
    {
      const std::string module =
          cnt::normalize_module_id(dir.filename().string());

      const fs::path moduleManifest = dir / "vix.module";

      if (!utils::exists_file(moduleManifest))
        continue;

      const std::string routePrefix =
          read_manifest_value(moduleManifest, "routes", "prefix");

      if (routePrefix.empty())
        continue;

      const auto existing = routePrefixes.find(routePrefix);

      if (existing != routePrefixes.end())
      {
        ok = false;
        ++violations;

        ui::err_line(std::cout, "Duplicate module route prefix");
        ui::kv(std::cout, "prefix", routePrefix, 12);
        ui::kv(std::cout, "module", module, 12);
        ui::kv(std::cout, "conflict", existing->second, 12);
      }
      else
      {
        routePrefixes[routePrefix] = module;
      }
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> declaredDeps;

    for (const auto &dir : moduleDirs)
    {
      const std::string mod =
          cnt::normalize_module_id(dir.filename().string());

      if (!utils::exists_file(dir / "CMakeLists.txt"))
        continue;

      declaredDeps[mod] =
          cnt::parse_declared_deps_from_module_cmake(
              dir / "CMakeLists.txt",
              project);
    }

    for (const auto &dir : moduleDirs)
    {
      const std::string mod =
          cnt::normalize_module_id(dir.filename().string());

      const fs::path includeRoot = dir / "include" / mod;

      if (!utils::exists_dir(includeRoot))
        continue;

      for (const auto &f : utils::list_files_recursive(includeRoot))
      {
        const std::string fstr = f.string();

        if (!utils::ends_with(fstr, ".hpp") &&
            !utils::ends_with(fstr, ".h") &&
            !utils::ends_with(fstr, ".hh"))
        {
          continue;
        }

        ++scannedHeaders;

        if (cnt::header_includes_private_impl(f, dir))
        {
          ok = false;
          ++violations;

          ui::err_line(std::cout, "Illegal include: public header includes private implementation");
          ui::kv(std::cout, "module", mod, 12);
          ui::kv(std::cout, "file", fstr, 12);
        }

        for (const auto &otherRaw :
             cnt::parse_public_includes_for_cross_module(f, modulesDir))
        {
          const std::string other =
              cnt::normalize_module_id(otherRaw);

          if (other == mod)
            continue;

          const auto it = declaredDeps.find(mod);

          const bool declared =
              it != declaredDeps.end() &&
              it->second.find(other) != it->second.end();

          if (!declared)
          {
            ok = false;
            ++violations;

            ui::err_line(std::cout, "Missing explicit module dependency");
            ui::kv(std::cout, "module", mod, 12);
            ui::kv(std::cout, "header", fstr, 12);
            ui::kv(std::cout, "uses", "<" + other + "/...>", 12);
            ui::warn_line(std::cout, "Fix in module CMakeLists.txt:");

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

      if (hasVixApp)
      {
        std::size_t enabledCount = 0;

        for (const auto &entry : enabledInApp)
        {
          if (entry.second)
            ++enabledCount;
        }

        ui::kv(std::cout, "declared", std::to_string(declaredInApp.size()), 10);
        ui::kv(std::cout, "enabled", std::to_string(enabledCount), 10);
      }

      return true;
    }

    ui::err_line(std::cout, "Modules check failed");
    ui::kv(std::cout, "modules", std::to_string(moduleDirs.size()), 10);
    ui::kv(std::cout, "headers", std::to_string(scannedHeaders), 10);
    ui::kv(std::cout, "issues", std::to_string(violations), 10);

    return false;
  }

} // namespace vix::commands::modules_cmd::commands
