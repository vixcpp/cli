#include <vix/cli/project/cmake/CTestDiscovery.hpp>

#include <cassert>

namespace project = vix::cli::project;
namespace cmake_adapter = vix::cli::project::cmake;

namespace
{
  void test_argv()
  {
    const auto single = cmake_adapter::ctest_discovery_argv("/tmp/build", std::nullopt);
    assert((single == std::vector<std::string>{
        "ctest", "--test-dir", "/tmp/build", "--show-only=json-v1"}));

    const auto multi = cmake_adapter::ctest_discovery_argv(
        "/tmp/build", std::string{"RelWithDebInfo"});
    assert((multi == std::vector<std::string>{
        "ctest", "--test-dir", "/tmp/build", "--show-only=json-v1",
        "-C", "RelWithDebInfo"}));
  }

  void test_registrations()
  {
    const auto result = cmake_adapter::parse_ctest_discovery_json(R"json(
      {
        "kind": "ctestInfo",
        "version": {"major": 1, "minor": 99},
        "tests": [
          {"name": "alpha", "command": ["runner"]},
          {"name": "duplicate name", "properties": [{"name": "LABELS", "value": ["unit"]}]},
          {"name": "duplicate name", "backtrace": 3},
          {"name": "suite: beta / punctuation"}
        ],
        "unknownFutureField": true
      })json");
    assert(result.success());
    assert(result.tests->size() == 4);
    assert((*result.tests)[0].name == "alpha");
    assert((*result.tests)[1].name == "duplicate name");
    assert((*result.tests)[2].name == "duplicate name");
    assert((*result.tests)[3].name == "suite: beta / punctuation");
    assert((*result.tests)[1].id != (*result.tests)[2].id);
    for (const project::Test &test : *result.tests)
      assert(!test.target.has_value());
  }

  void test_empty_and_invalid_metadata()
  {
    const auto empty = cmake_adapter::parse_ctest_discovery_json(
        R"({"kind":"ctestInfo","version":{"major":1,"minor":0},"tests":[]})");
    assert(empty.success());
    assert(empty.tests->empty());

    const auto unsupported = cmake_adapter::parse_ctest_discovery_json(
        R"({"kind":"ctestInfo","version":{"major":2},"tests":[]})");
    assert(unsupported.error == project::ProjectError::configured_metadata_unavailable);

    const auto malformed = cmake_adapter::parse_ctest_discovery_json("{");
    assert(malformed.error == project::ProjectError::configured_metadata_unavailable);

    const auto missingName = cmake_adapter::parse_ctest_discovery_json(
        R"({"kind":"ctestInfo","version":{"major":1},"tests":[{}]})");
    assert(missingName.error == project::ProjectError::configured_metadata_unavailable);
  }
} // namespace

int main()
{
  test_argv();
  test_registrations();
  test_empty_and_invalid_metadata();
}
