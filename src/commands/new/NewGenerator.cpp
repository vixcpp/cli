/**
 * @file NewGenerator.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/NewGenerator.hpp>
#include <vix/cli/commands/new/NewOutput.hpp>
#include <vix/cli/commands/new/NewTemplates.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace vix::commands::new_cmd::generator
{
  namespace fs = std::filesystem;
  namespace tpl = vix::commands::new_cmd::templates;
  namespace out = vix::commands::new_cmd::output;

  // ------------------------------------------------------------------
  // File-system helpers
  // ------------------------------------------------------------------

  bool is_dot_path(const std::string &s)
  {
    return s == "." || s == "./" || s == ".\\";
  }

  std::string current_dir_name()
  {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::current_path(), ec);
    if (ec)
      p = fs::current_path();

    std::string name = p.filename().string();
    if (name.empty())
      name = "app";

    return name;
  }

  bool dir_exists(const fs::path &p)
  {
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_directory(p, ec);
  }

  bool dir_is_empty(const fs::path &p)
  {
    std::error_code ec;
    if (!dir_exists(p))
      return true;

    fs::directory_iterator it(p, ec);
    if (ec)
      return false;

    return it == fs::directory_iterator{};
  }

  bool ensure_dir(const fs::path &p, std::string &err)
  {
    std::error_code ec;
    fs::create_directories(p, ec);

    if (ec)
    {
      err = ec.message();
      return false;
    }

    return true;
  }

  bool write_text_file(
      const fs::path &p,
      const std::string &content,
      std::string &err)
  {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    if (ec)
    {
      err = ec.message();
      return false;
    }

    std::ofstream out(p, std::ios::binary);
    if (!out)
    {
      err = "cannot open file for writing";
      return false;
    }

    out.write(content.data(), static_cast<std::streamsize>(content.size()));

    if (!out.good())
    {
      err = "write failed";
      return false;
    }

    return true;
  }

  // ------------------------------------------------------------------
  // App project scaffold
  // ------------------------------------------------------------------

  bool generate_app_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err)
  {
    const fs::path srcDir = projectDir / "src";
    const fs::path testsDir = projectDir / "tests";

    if (!ensure_dir(srcDir, err))
      return false;

    if (!ensure_dir(testsDir, err))
      return false;

    if (!write_text_file(srcDir / "main.cpp", tpl::kMainCpp, err))
      return false;

    if (!write_text_file(testsDir / "test_basic.cpp", tpl::kBasicTestCpp_App, err))
      return false;

    if (!write_text_file(projectDir / ".env.example", tpl::kEnvExample, err))
      return false;

    if (!write_text_file(projectDir / ".env", tpl::kEnvExample, err))
      return false;

    if (!write_text_file(projectDir / "README.md",
                         tpl::make_readme_app(projName), err))
      return false;

    if (!write_text_file(projectDir / "vix.json",
                         tpl::make_vix_json_app(projName), err))
      return false;

    if (!write_text_file(projectDir / "vix.app",
                         tpl::make_project_manifest_app(projName, features), err))
      return false;

    return true;
  }

  // ------------------------------------------------------------------
  // Library project scaffold
  // ------------------------------------------------------------------

  bool generate_lib_project(
      const fs::path &projectDir,
      const std::string &projName,
      std::string &err)
  {
    const fs::path includeDir = projectDir / "include" / projName;
    const fs::path testsDir = projectDir / "tests";
    const fs::path examplesDir = projectDir / "examples";

    if (!ensure_dir(includeDir, err))
      return false;

    if (!ensure_dir(testsDir, err))
      return false;

    if (!ensure_dir(examplesDir, err))
      return false;

    if (!write_text_file(includeDir / (projName + ".hpp"),
                         tpl::make_lib_header(projName), err))
      return false;

    if (!write_text_file(testsDir / "test_basic.cpp",
                         tpl::make_basic_test_cpp_lib(projName), err))
      return false;

    if (!write_text_file(examplesDir / "basic.cpp",
                         tpl::make_basic_example_cpp_lib(projName), err))
      return false;

    if (!write_text_file(examplesDir / "CMakeLists.txt",
                         tpl::make_examples_cmakelists_lib(projName), err))
      return false;

    if (!write_text_file(projectDir / "CMakeLists.txt",
                         tpl::make_cmakelists_lib(projName), err))
      return false;

    if (!write_text_file(projectDir / "README.md",
                         tpl::make_readme_lib(projName), err))
      return false;

    if (!write_text_file(projectDir / "CMakePresets.json",
                         tpl::make_cmake_presets_json_lib(), err))
      return false;

    if (!write_text_file(projectDir / "vix.json",
                         tpl::make_vix_json_lib(projName), err))
      return false;

    if (!write_text_file(projectDir / (projName + ".vix"),
                         tpl::make_project_manifest_lib(projName), err))
      return false;

    return true;
  }

  // ------------------------------------------------------------------
  // Vue + Vix app project scaffold
  // ------------------------------------------------------------------

  bool generate_vue_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err)
  {
    if (!generate_app_project(projectDir, projName, features, err))
      return false;

    const fs::path frontendDir = projectDir / "frontend";
    const fs::path frontendSrcDir = frontendDir / "src";

    if (!ensure_dir(frontendSrcDir, err))
      return false;

    if (!write_text_file(frontendDir / "package.json",
                         tpl::make_vue_package_json(projName), err))
      return false;

    if (!write_text_file(frontendDir / "index.html",
                         tpl::make_vue_index_html(projName), err))
      return false;

    if (!write_text_file(frontendDir / "vite.config.js",
                         tpl::make_vue_vite_config(), err))
      return false;

    if (!write_text_file(frontendSrcDir / "main.js",
                         tpl::make_vue_main_js(), err))
      return false;

    if (!write_text_file(frontendSrcDir / "App.vue",
                         tpl::make_vue_app_vue(), err))
      return false;

    if (!write_text_file(projectDir / "README.md",
                         tpl::make_readme_vue_app(projName), err))
      return false;

    if (!write_text_file(projectDir / "vix.json",
                         tpl::make_vix_json_vue_app(projName), err))
      return false;

    return true;
  }

  // ------------------------------------------------------------------
  // Game
  // ------------------------------------------------------------------

  bool generate_game_project(
      const fs::path &projectDir,
      const std::string &projName,
      std::string &err)
  {
    const fs::path srcDir = projectDir / "src";
    const fs::path assetsDir = projectDir / "assets";

    if (!ensure_dir(srcDir, err))
      return false;

    if (!ensure_dir(assetsDir, err))
      return false;

    if (!write_text_file(srcDir / "main.cpp",
                         tpl::make_game_main_cpp(projName), err))
      return false;

    if (!write_text_file(projectDir / "game.package.json",
                         tpl::make_game_package_json(projName), err))
      return false;

    if (!write_text_file(projectDir / "README.md",
                         tpl::make_readme_game(projName), err))
      return false;

    if (!write_text_file(projectDir / "vix.json",
                         tpl::make_vix_json_game(projName), err))
      return false;

    if (!write_text_file(projectDir / "vix.app",
                         tpl::make_project_manifest_game(projName), err))
      return false;

    if (!write_text_file(assetsDir / ".gitkeep", "", err))
      return false;

    return true;
  }

  // ------------------------------------------------------------------
  // Post-generation output
  // ------------------------------------------------------------------

  void print_next_steps_app(
      const fs::path &projectDir,
      const std::string &projName)
  {
    FeaturesSelection features{};
    out::print_creation_app(projectDir, projName, features);
  }

  void print_next_steps_vue(
      const fs::path &projectDir,
      const std::string &projName)
  {
    out::print_creation_vue(projectDir, projName);
  }

  void print_next_steps_lib(
      const fs::path &projectDir,
      const std::string &projName)
  {
    out::print_creation_lib(projectDir, projName);
  }

  void print_next_steps_game(
      const fs::path &projectDir,
      const std::string &projName)
  {
    out::print_creation_game(projectDir, projName);
  }

} // namespace vix::commands::new_cmd::generator
