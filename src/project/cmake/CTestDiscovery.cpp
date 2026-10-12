/** @file CTestDiscovery.cpp */
#include <vix/cli/project/cmake/CTestDiscovery.hpp>
#include <vix/cli/project/cmake/CMakeProjectAdapter.hpp>

#include <vix/engine/Process.hpp>

#include <nlohmann/json.hpp>

#include <utility>

namespace vix::cli::project::cmake
{
  namespace
  {
    using json = nlohmann::json;

    [[nodiscard]] const json *object_member(const json &value, const char *name)
    {
      if (!value.is_object())
        return nullptr;
      const auto found = value.find(name);
      return found == value.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::optional<std::string> string_value(
        const json &value,
        const char *name)
    {
      const json *member = object_member(value, name);
      if (member == nullptr || !member->is_string())
        return std::nullopt;
      return member->get<std::string>();
    }

    [[nodiscard]] bool is_ctest_info_v1(const json &document)
    {
      const auto kind = string_value(document, "kind");
      const json *version = object_member(document, "version");
      const json *major = version == nullptr ? nullptr : object_member(*version, "major");
      return kind && *kind == "ctestInfo" && major != nullptr &&
             major->is_number_integer() && major->get<int>() == 1;
    }
  } // namespace

  std::vector<std::string> ctest_discovery_argv(
      const Path &buildRoot,
      const std::optional<std::string> &configuration)
  {
    std::vector<std::string> argv{"ctest", "--test-dir", buildRoot.string(),
                                  "--show-only=json-v1"};
    if (configuration)
    {
      argv.push_back("-C");
      argv.push_back(*configuration);
    }
    return argv;
  }

  CTestDiscoveryResult parse_ctest_discovery_json(std::string_view document)
  {
    const json parsed = json::parse(document, nullptr, false, true);
    if (parsed.is_discarded() || !parsed.is_object() || !is_ctest_info_v1(parsed))
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    const json *tests = object_member(parsed, "tests");
    if (tests == nullptr || !tests->is_array())
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    std::vector<Test> normalized;
    normalized.reserve(tests->size());
    for (std::size_t index = 0; index < tests->size(); ++index)
    {
      const json &entry = (*tests)[index];
      const auto name = string_value(entry, "name");
      if (!entry.is_object() || !name)
        return {std::nullopt, ProjectError::configured_metadata_unavailable};

      Test test;
      test.id.value = "ctest:" + std::to_string(index);
      test.name = *name;
      // CTest provides no authoritative CMake TargetId relationship.
      test.target = std::nullopt;
      normalized.push_back(std::move(test));
    }
    return {std::move(normalized), std::nullopt};
  }

  CTestDiscoveryResult discover_ctest_tests(const SnapshotRequest &request)
  {
    vix::engine::process::Command command;
    command.argv = ctest_discovery_argv(request.buildRoot, request.configuration);
    command.mergeStdErr = false;

    const vix::engine::process::Result result =
        vix::engine::process::execute(command);
    if (!result.success())
      return {std::nullopt, ProjectError::configured_metadata_unavailable};

    return parse_ctest_discovery_json(result.output);
  }
} // namespace vix::cli::project::cmake
