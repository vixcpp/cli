/**
 * @file Test.hpp
 * @brief Normalized internal project test contract.
 */
#ifndef VIX_CLI_PROJECT_TEST_HPP
#define VIX_CLI_PROJECT_TEST_HPP

#include <optional>
#include <string>

#include <vix/cli/project/Target.hpp>

namespace vix::cli::project
{
  /** Adapter-native test identity, distinct from TargetId. */
  struct TestId
  {
    std::string value;

    [[nodiscard]] bool valid() const noexcept { return !value.empty(); }
    friend bool operator==(const TestId &, const TestId &) = default;
  };

  struct Test
  {
    TestId id;
    std::string name;
    std::optional<TargetId> target;

    [[nodiscard]] bool valid() const noexcept { return id.valid(); }
  };
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_TEST_HPP
