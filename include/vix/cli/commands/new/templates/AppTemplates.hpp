#pragma once

/**
 * @file AppTemplates.hpp
 * @author Gaspard Kirira
 *
 * File-content templates for the default `vix new` application template.
 */

#include <string>

#include <vix/cli/commands/new/NewTypes.hpp>

namespace vix::commands::new_cmd::templates
{

  /// src/main.cpp for a new App project.
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

  /// tests/test_basic.cpp for a new App project.
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

  /// config/app.json default content.
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

  /// Default .env.example content for an App project.
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

  std::string make_readme_app(const std::string &projectName);

  std::string make_cmake_presets_json_app(const FeaturesSelection &features);

  std::string make_project_manifest_app(
      const std::string &projectName,
      const FeaturesSelection &features);

  std::string make_vix_json_app(const std::string &projectName);

  std::string make_cmakelists_app(
      const std::string &projectName,
      const FeaturesSelection &features);

} // namespace vix::commands::new_cmd::templates
