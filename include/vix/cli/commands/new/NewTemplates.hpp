#pragma once

/**
 * @file NewTemplates.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * File content templates for the `vix new` command.
 * All functions return the textual content of a generated file.
 */

#include <string>
#include <vix/cli/commands/new/NewTypes.hpp>

namespace vix::commands::new_cmd::templates
{

  // ------------------------------------------------------------------
  // Static content (inline constants)
  // ------------------------------------------------------------------

  /// src/main.cpp for a new App project
  constexpr const char *kMainCpp = R"(#include <vix.hpp>
using namespace vix;

int main()
{
  App app;

  // GET /
  app.get("/", [](Request&, Response& res) {
    res.send("Hello world");
  });

  app.run(8080);
}
)";

  /// tests/test_basic.cpp for a new App project
  constexpr const char *kBasicTestCpp_App = R"(#include <vix/tests/tests.hpp>

int main()
{
  using namespace vix::tests;

  auto &registry = TestRegistry::instance();
  registry.clear();

  registry.add(TestCase("app basic test", [] {
    Assert::equal(2 + 2, 4);
  }));

  return TestRunner::run_all_and_exit();
}
)";

  /// config/app.json default content
  constexpr const char *kAppConfigJson = R"JSON({
  "server": {
    "port": 8080,
    "request_timeout": 2000,
    "io_threads": 0,
    "session_timeout_sec": 20
  },
  "logging": {
    "async": true,
    "queue_max": 20000,
    "drop_on_overflow": true
  },
  "waf": {
    "mode": "basic",
    "max_target_len": 4096,
    "max_body_bytes": 1048576
  },
  "database": {
    "default": {
      "host": "localhost",
      "user": "root",
      "password": "",
      "name": "",
      "port": 3306
    }
  }
}
)JSON";

  /// Default .env.example / .env content for an App project
  constexpr const char *kEnvExample = R"(# ----------------------------------
# Server
# ----------------------------------
SERVER_PORT=8080

# ----------------------------------
# Database
# ----------------------------------
DATABASE_ENGINE=mysql
DATABASE_DEFAULT_HOST=127.0.0.1
DATABASE_DEFAULT_PORT=3306
DATABASE_DEFAULT_USER=root
DATABASE_DEFAULT_PASSWORD=
DATABASE_DEFAULT_NAME=appdb

# ----------------------------------
# Logging
# ----------------------------------
LOGGING_ASYNC=true

# ----------------------------------
# Security / WAF
# ----------------------------------
WAF_MODE=basic
)";

  // ------------------------------------------------------------------
  // Library scaffold helpers
  // ------------------------------------------------------------------

  /// include/<name>/<name>.hpp  (header-only library stub)
  std::string make_lib_header(const std::string &name);

  /// tests/test_basic.cpp for a library project
  std::string make_basic_test_cpp_lib(const std::string &name);

  /// examples/basic.cpp for a library project
  std::string make_basic_example_cpp_lib(const std::string &name);

  /// examples/CMakeLists.txt for a library project
  std::string make_examples_cmakelists_lib(const std::string &name);

  // ------------------------------------------------------------------
  // README generators
  // ------------------------------------------------------------------

  std::string make_readme_app(const std::string &projectName);
  std::string make_readme_vue_app(const std::string &projectName);
  std::string make_readme_lib(const std::string &name);

  // ------------------------------------------------------------------
  // CMakePresets.json generators
  // ------------------------------------------------------------------

  std::string make_cmake_presets_json_app(const FeaturesSelection &f);
  std::string make_cmake_presets_json_lib();

  // ------------------------------------------------------------------
  // Project manifest (.vix) generators
  // ------------------------------------------------------------------

  std::string make_project_manifest_app(const std::string &name, const FeaturesSelection &f);
  std::string make_project_manifest_lib(const std::string &name);

  // ------------------------------------------------------------------
  // vix.json generators
  // ------------------------------------------------------------------

  std::string make_vix_json_app(const std::string &name);
  std::string make_vix_json_lib(const std::string &name);
  std::string make_vix_json_vue_app(const std::string &name);

  // ------------------------------------------------------------------
  // Vue frontend generators
  // ------------------------------------------------------------------

  std::string make_vue_package_json(const std::string &projectName);
  std::string make_vue_index_html(const std::string &projectName);
  std::string make_vue_vite_config();
  std::string make_vue_main_js();
  std::string make_vue_app_vue();

  // ------------------------------------------------------------------
  // CMakeLists.txt generators
  // ------------------------------------------------------------------

  std::string make_cmakelists_app(const std::string &projectName, const FeaturesSelection &f);
  std::string make_cmakelists_lib(const std::string &name);

} // namespace vix::commands::new_cmd::templates
