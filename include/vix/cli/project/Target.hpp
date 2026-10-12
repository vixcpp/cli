/**
 * @file Target.hpp
 * @brief Normalized internal project target contract.
 */
#ifndef VIX_CLI_PROJECT_TARGET_HPP
#define VIX_CLI_PROJECT_TARGET_HPP

#include <string>

namespace vix::cli::project
{
  /** Adapter-native identity, valid only for a resolved project snapshot. */
  struct TargetId
  {
    std::string value;

    [[nodiscard]] bool valid() const noexcept { return !value.empty(); }
    friend bool operator==(const TargetId &, const TargetId &) = default;
  };

  enum class TargetKind
  {
    executable,
    static_library,
    shared_library,
    module_library,
    interface_library,
    object_library,
    utility,
    unknown
  };

  struct TargetCapabilities
  {
    bool buildable{false};
    bool runnable{false};
  };

  struct Target
  {
    TargetId id;
    std::string name;
    TargetKind kind{TargetKind::unknown};
    TargetCapabilities capabilities;

    [[nodiscard]] bool valid() const noexcept { return id.valid(); }
  };
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_TARGET_HPP
