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

  bool generate_backend_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err)
  {
    namespace tpl = vix::commands::new_cmd::templates;

    const fs::path includeRoot = projectDir / "include" / projName;
    const fs::path srcDir = projectDir / "src";
    const fs::path appRoot = srcDir / projName;

    const fs::path includeAppDir = includeRoot / "app";
    const fs::path includeSupportDir = includeRoot / "support";
    const fs::path includePresentationDir = includeRoot / "presentation";
    const fs::path includeControllersDir = includePresentationDir / "controllers";
    const fs::path includeRoutesDir = includePresentationDir / "routes";
    const fs::path includeMiddlewareDir = includePresentationDir / "middleware";

    const fs::path appDir = appRoot / "app";
    const fs::path applicationDir = appRoot / "application";
    const fs::path domainDir = appRoot / "domain";
    const fs::path infrastructureDir = appRoot / "infrastructure";
    const fs::path presentationDir = appRoot / "presentation";
    const fs::path controllersDir = presentationDir / "controllers";
    const fs::path routesDir = presentationDir / "routes";
    const fs::path middlewareDir = presentationDir / "middleware";
    const fs::path supportDir = appRoot / "support";

    const fs::path publicDir = projectDir / "public";
    const fs::path viewsDir = projectDir / "views";
    const fs::path storageDir = projectDir / "storage";
    const fs::path migrationsDir = projectDir / "migrations";
    const fs::path testsDir = projectDir / "tests";
    const fs::path configDir = projectDir / "config";

    if (!ensure_dir(includeAppDir, err))
      return false;
    if (!ensure_dir(includeSupportDir, err))
      return false;
    if (!ensure_dir(includeControllersDir, err))
      return false;
    if (!ensure_dir(includeRoutesDir, err))
      return false;
    if (!ensure_dir(includeMiddlewareDir, err))
      return false;

    if (!ensure_dir(srcDir, err))
      return false;
    if (!ensure_dir(appDir, err))
      return false;
    if (!ensure_dir(applicationDir, err))
      return false;
    if (!ensure_dir(domainDir, err))
      return false;
    if (!ensure_dir(infrastructureDir, err))
      return false;
    if (!ensure_dir(controllersDir, err))
      return false;
    if (!ensure_dir(routesDir, err))
      return false;
    if (!ensure_dir(middlewareDir, err))
      return false;
    if (!ensure_dir(supportDir, err))
      return false;

    if (!ensure_dir(publicDir, err))
      return false;
    if (!ensure_dir(viewsDir, err))
      return false;
    if (!ensure_dir(storageDir, err))
      return false;
    if (!ensure_dir(migrationsDir, err))
      return false;
    if (!ensure_dir(testsDir, err))
      return false;
    if (!ensure_dir(configDir, err))
      return false;

    if (!write_text_file(srcDir / "main.cpp",
                         tpl::make_backend_main_cpp(projName), err))
      return false;

    if (!write_text_file(includeAppDir / "AppBootstrap.hpp",
                         tpl::make_backend_app_bootstrap_hpp(projName), err))
      return false;

    if (!write_text_file(appDir / "AppBootstrap.cpp",
                         tpl::make_backend_app_bootstrap_cpp(projName), err))
      return false;

    if (!write_text_file(includeSupportDir / "HttpResponses.hpp",
                         tpl::make_backend_http_responses_hpp(projName), err))
      return false;

    if (!write_text_file(supportDir / "HttpResponses.cpp",
                         tpl::make_backend_http_responses_cpp(projName), err))
      return false;

    if (!write_text_file(includeRoutesDir / "RouteRegistry.hpp",
                         tpl::make_backend_route_registry_hpp(projName), err))
      return false;

    if (!write_text_file(routesDir / "RouteRegistry.cpp",
                         tpl::make_backend_route_registry_cpp(projName), err))
      return false;

    if (!write_text_file(includeMiddlewareDir / "MiddlewareRegistry.hpp",
                         tpl::make_backend_middleware_registry_hpp(projName), err))
      return false;

    if (!write_text_file(middlewareDir / "MiddlewareRegistry.cpp",
                         tpl::make_backend_middleware_registry_cpp(projName), err))
      return false;

    if (!write_text_file(includeControllersDir / "HomeController.hpp",
                         tpl::make_backend_home_controller_hpp(projName), err))
      return false;

    if (!write_text_file(controllersDir / "HomeController.cpp",
                         tpl::make_backend_home_controller_cpp(projName), err))
      return false;

    if (!write_text_file(includeControllersDir / "HealthController.hpp",
                         tpl::make_backend_health_controller_hpp(projName), err))
      return false;

    if (!write_text_file(controllersDir / "HealthController.cpp",
                         tpl::make_backend_health_controller_cpp(projName), err))
      return false;

    if (!write_text_file(testsDir / "test_basic.cpp",
                         tpl::make_backend_basic_test_cpp(projName), err))
      return false;

    if (!write_text_file(testsDir / "vix.app",
                         tpl::make_backend_tests_manifest(projName), err))
      return false;

    if (!write_text_file(publicDir / "index.html",
                         "<!doctype html>\n"
                         "<html lang=\"en\">\n"
                         "  <head>\n"
                         "    <meta charset=\"utf-8\" />\n"
                         "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
                         "    <title>" +
                             projName +
                             "</title>\n"
                             "    <link rel=\"stylesheet\" href=\"/app.css\" />\n"
                             "  </head>\n"
                             "  <body>\n"
                             "    <main class=\"page\">\n"
                             "      <section class=\"card\">\n"
                             "        <p class=\"eyebrow\">Vix backend</p>\n"
                             "        <h1>" +
                             projName +
                             "</h1>\n"
                             "        <p>Your production-oriented Vix backend is running.</p>\n"
                             "        <div class=\"actions\">\n"
                             "          <a href=\"/api\">API</a>\n"
                             "          <a href=\"/health\">Health</a>\n"
                             "        </div>\n"
                             "      </section>\n"
                             "    </main>\n"
                             "    <script src=\"/app.js\"></script>\n"
                             "  </body>\n"
                             "</html>\n",
                         err))
      return false;

    if (!write_text_file(publicDir / "app.css",
                         ":root {\n"
                         "  color-scheme: light dark;\n"
                         "  font-family: Inter, system-ui, -apple-system, BlinkMacSystemFont, \"Segoe UI\", sans-serif;\n"
                         "  background: #0b0e14;\n"
                         "  color: #f7f7f8;\n"
                         "}\n"
                         "\n"
                         "* {\n"
                         "  box-sizing: border-box;\n"
                         "}\n"
                         "\n"
                         "body {\n"
                         "  margin: 0;\n"
                         "}\n"
                         "\n"
                         ".page {\n"
                         "  min-height: 100vh;\n"
                         "  display: grid;\n"
                         "  place-items: center;\n"
                         "  padding: 24px;\n"
                         "}\n"
                         "\n"
                         ".card {\n"
                         "  width: min(720px, 100%);\n"
                         "  padding: 40px;\n"
                         "  border: 1px solid rgba(255, 255, 255, 0.12);\n"
                         "  border-radius: 24px;\n"
                         "  background: rgba(255, 255, 255, 0.06);\n"
                         "  box-shadow: 0 24px 80px rgba(0, 0, 0, 0.35);\n"
                         "}\n"
                         "\n"
                         ".eyebrow {\n"
                         "  margin: 0 0 12px;\n"
                         "  color: #ff9900;\n"
                         "  font-weight: 700;\n"
                         "  letter-spacing: 0.08em;\n"
                         "  text-transform: uppercase;\n"
                         "}\n"
                         "\n"
                         "h1 {\n"
                         "  margin: 0;\n"
                         "  font-size: clamp(2.2rem, 8vw, 5rem);\n"
                         "  line-height: 1;\n"
                         "}\n"
                         "\n"
                         "p {\n"
                         "  font-size: 1.05rem;\n"
                         "  line-height: 1.7;\n"
                         "  color: rgba(255, 255, 255, 0.78);\n"
                         "}\n"
                         "\n"
                         ".actions {\n"
                         "  display: flex;\n"
                         "  gap: 12px;\n"
                         "  flex-wrap: wrap;\n"
                         "  margin-top: 24px;\n"
                         "}\n"
                         "\n"
                         ".actions a {\n"
                         "  color: #0b0e14;\n"
                         "  background: #ff9900;\n"
                         "  padding: 10px 16px;\n"
                         "  border-radius: 999px;\n"
                         "  text-decoration: none;\n"
                         "  font-weight: 700;\n"
                         "}\n",
                         err))
      return false;

    if (!write_text_file(publicDir / "app.js",
                         "console.log(\"Vix backend is running\");\n",
                         err))
      return false;

    if (!write_text_file(applicationDir / ".gitkeep", "", err))
      return false;
    if (!write_text_file(domainDir / ".gitkeep", "", err))
      return false;
    if (!write_text_file(infrastructureDir / ".gitkeep", "", err))
      return false;
    if (!write_text_file(viewsDir / ".gitkeep", "", err))
      return false;
    if (!write_text_file(storageDir / ".gitkeep", "", err))
      return false;
    if (!write_text_file(migrationsDir / ".gitkeep", "", err))
      return false;

    if (!write_text_file(configDir / "production.json",
                         tpl::make_backend_production_config_json(projName), err))
      return false;

    if (!write_text_file(projectDir / ".env.example",
                         tpl::make_backend_env_example(projName), err))
      return false;

    if (!write_text_file(projectDir / ".env",
                         tpl::make_backend_env_example(projName), err))
      return false;

    if (!write_text_file(projectDir / "vix.app",
                         tpl::make_project_manifest_backend(projName, features), err))
      return false;

    if (!write_text_file(projectDir / "vix.json",
                         tpl::make_vix_json_backend(projName), err))
      return false;

    if (!write_text_file(projectDir / "README.md",
                         tpl::make_readme_backend(projName), err))
      return false;

    return true;
  }

  // ------------------------------------------------------------------
  // Web
  // -----------------------------------------------------------------

  bool generate_web_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err)
  {
    namespace tpl = vix::commands::new_cmd::templates;

    const fs::path includeRoot = projectDir / "include" / projName;
    const fs::path srcDir = projectDir / "src";
    const fs::path appRoot = srcDir / projName;

    const fs::path includeAppDir = includeRoot / "app";
    const fs::path includePresentationDir = includeRoot / "presentation";
    const fs::path includeControllersDir = includePresentationDir / "controllers";
    const fs::path includeRoutesDir = includePresentationDir / "routes";
    const fs::path includeMiddlewareDir = includePresentationDir / "middleware";

    const fs::path appDir = appRoot / "app";
    const fs::path presentationDir = appRoot / "presentation";
    const fs::path controllersDir = presentationDir / "controllers";
    const fs::path routesDir = presentationDir / "routes";
    const fs::path middlewareDir = presentationDir / "middleware";

    const fs::path publicDir = projectDir / "public";
    const fs::path viewsDir = projectDir / "views";
    const fs::path storageDir = projectDir / "storage";
    const fs::path testsDir = projectDir / "tests";
    const fs::path configDir = projectDir / "config";

    if (!ensure_dir(includeAppDir, err))
      return false;
    if (!ensure_dir(includeControllersDir, err))
      return false;
    if (!ensure_dir(includeRoutesDir, err))
      return false;
    if (!ensure_dir(includeMiddlewareDir, err))
      return false;

    if (!ensure_dir(srcDir, err))
      return false;
    if (!ensure_dir(appDir, err))
      return false;
    if (!ensure_dir(controllersDir, err))
      return false;
    if (!ensure_dir(routesDir, err))
      return false;
    if (!ensure_dir(middlewareDir, err))
      return false;

    if (!ensure_dir(publicDir, err))
      return false;
    if (!ensure_dir(viewsDir, err))
      return false;
    if (!ensure_dir(storageDir, err))
      return false;
    if (!ensure_dir(testsDir, err))
      return false;
    if (!ensure_dir(configDir, err))
      return false;

    if (!write_text_file(srcDir / "main.cpp",
                         tpl::make_web_main_cpp(projName), err))
      return false;

    if (!write_text_file(includeAppDir / "AppBootstrap.hpp",
                         tpl::make_web_app_bootstrap_hpp(projName), err))
      return false;

    if (!write_text_file(appDir / "AppBootstrap.cpp",
                         tpl::make_web_app_bootstrap_cpp(projName), err))
      return false;

    if (!write_text_file(includeRoutesDir / "RouteRegistry.hpp",
                         tpl::make_web_route_registry_hpp(projName), err))
      return false;

    if (!write_text_file(routesDir / "RouteRegistry.cpp",
                         tpl::make_web_route_registry_cpp(projName), err))
      return false;

    if (!write_text_file(includeMiddlewareDir / "MiddlewareRegistry.hpp",
                         tpl::make_web_middleware_registry_hpp(projName), err))
      return false;

    if (!write_text_file(middlewareDir / "MiddlewareRegistry.cpp",
                         tpl::make_web_middleware_registry_cpp(projName), err))
      return false;

    if (!write_text_file(includeControllersDir / "PageController.hpp",
                         tpl::make_web_page_controller_hpp(projName), err))
      return false;

    if (!write_text_file(controllersDir / "PageController.cpp",
                         tpl::make_web_page_controller_cpp(projName), err))
      return false;

    if (!write_text_file(includeControllersDir / "HealthController.hpp",
                         tpl::make_web_health_controller_hpp(projName), err))
      return false;

    if (!write_text_file(controllersDir / "HealthController.cpp",
                         tpl::make_web_health_controller_cpp(projName), err))
      return false;

    if (!write_text_file(viewsDir / "base.html",
                         tpl::make_web_view_base_html(projName), err))
      return false;

    if (!write_text_file(viewsDir / "header.html",
                         tpl::make_web_view_header_html(projName), err))
      return false;

    if (!write_text_file(viewsDir / "index.html",
                         tpl::make_web_view_index_html(projName), err))
      return false;

    if (!write_text_file(viewsDir / "dashboard.html",
                         tpl::make_web_view_dashboard_html(projName), err))
      return false;

    if (!write_text_file(publicDir / "app.css",
                         tpl::make_web_public_app_css(projName), err))
      return false;

    if (!write_text_file(publicDir / "app.js",
                         tpl::make_web_public_app_js(projName), err))
      return false;

    if (!write_text_file(storageDir / ".gitkeep", "", err))
      return false;

    if (!write_text_file(testsDir / "test_basic.cpp",
                         tpl::make_web_basic_test_cpp(projName), err))
      return false;

    if (!write_text_file(testsDir / "vix.app",
                         tpl::make_web_tests_manifest(projName), err))
      return false;

    if (!write_text_file(configDir / "production.json",
                         tpl::make_web_production_config_json(projName), err))
      return false;

    if (!write_text_file(projectDir / ".env.example",
                         tpl::make_web_env_example(projName), err))
      return false;

    if (!write_text_file(projectDir / ".env",
                         tpl::make_web_env_example(projName), err))
      return false;

    if (!write_text_file(projectDir / "vix.app",
                         tpl::make_project_manifest_web(projName, features), err))
      return false;

    if (!write_text_file(projectDir / "vix.json",
                         tpl::make_vix_json_web(projName), err))
      return false;

    if (!write_text_file(projectDir / "README.md",
                         tpl::make_readme_web(projName), err))
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

  void print_next_steps_backend(const fs::path &projectDir, const std::string &projName)
  {
    output::print_creation_backend(projectDir, projName, FeaturesSelection{});
  }

  void print_next_steps_web(const fs::path &projectDir, const std::string &projName)
  {
    output::print_creation_web(projectDir, projName, FeaturesSelection{});
  }

  void print_next_steps_game(
      const fs::path &projectDir,
      const std::string &projName)
  {
    out::print_creation_game(projectDir, projName);
  }

} // namespace vix::commands::new_cmd::generator
