/**
 * @file NativeProjectCommandDispatch.hpp
 * @brief Small dispatch seam for commands consuming ProjectDiscovery results.
 */
#ifndef VIX_CLI_PROJECT_NATIVE_PROJECT_COMMAND_DISPATCH_HPP
#define VIX_CLI_PROJECT_NATIVE_PROJECT_COMMAND_DISPATCH_HPP

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <vix/cli/project/ProjectDiscovery.hpp>

namespace vix::cli::project
{
  // Returns a value only when ProjectDiscovery selected a native Vix project.
  // A failed native runtime result is still a value and therefore cannot fall
  // through to an unrelated historical command path.
  template <typename NativeRuntime>
  [[nodiscard]] auto dispatch_native_project(
      const ProjectDiscoveryResult &discovery,
      NativeRuntime &&runtime)
      -> std::optional<std::invoke_result_t<NativeRuntime, const ProjectLocation &>>
  {
    using Result = std::invoke_result_t<NativeRuntime, const ProjectLocation &>;
    static_assert(!std::is_void_v<Result>, "native command dispatch requires a result");
    if (!discovery.success() ||
        discovery.location->system != ProjectSystem::native_vix)
      return std::nullopt;
    return std::invoke(std::forward<NativeRuntime>(runtime), *discovery.location);
  }

  template <typename NativeRuntime>
  [[nodiscard]] auto dispatch_native_build(
      const ProjectDiscoveryResult &discovery, const std::optional<std::string> &target,
      int jobs, std::string_view preset, NativeRuntime &&runtime)
      -> std::optional<std::invoke_result_t<NativeRuntime, const ProjectLocation &,
          const std::optional<std::string> &, int, std::string_view>>
  {
    return dispatch_native_project(discovery, [&](const ProjectLocation &location)
    {
      return std::invoke(std::forward<NativeRuntime>(runtime), location, target, jobs, preset);
    });
  }

  template <typename NativeRuntime>
  [[nodiscard]] auto dispatch_native_run(
      const ProjectDiscoveryResult &discovery, const std::optional<std::string> &target,
      const std::vector<std::string> &arguments, std::string_view preset,
      NativeRuntime &&runtime)
      -> std::optional<std::invoke_result_t<NativeRuntime, const ProjectLocation &,
          const std::optional<std::string> &, const std::vector<std::string> &, std::string_view>>
  {
    return dispatch_native_project(discovery, [&](const ProjectLocation &location)
    {
      return std::invoke(std::forward<NativeRuntime>(runtime), location, target, arguments, preset);
    });
  }

  template <typename NativeRuntime>
  [[nodiscard]] auto dispatch_native_tests(
      const ProjectDiscoveryResult &discovery, const std::optional<std::string> &testName,
      bool listOnly, const std::vector<std::string> &ctestArguments,
      std::string_view testPattern, std::string_view preset, NativeRuntime &&runtime)
      -> std::optional<std::invoke_result_t<NativeRuntime, const ProjectLocation &,
          const std::optional<std::string> &, bool, const std::vector<std::string> &,
          std::string_view, std::string_view>>
  {
    return dispatch_native_project(discovery, [&](const ProjectLocation &location)
    {
      return std::invoke(std::forward<NativeRuntime>(runtime), location, testName, listOnly,
                         ctestArguments, testPattern, preset);
    });
  }
} // namespace vix::cli::project

#endif // VIX_CLI_PROJECT_NATIVE_PROJECT_COMMAND_DISPATCH_HPP
