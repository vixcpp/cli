/**
 *
 *  @file BuildContext.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Shared build context helpers
 *
 */

#include <vix/cli/build/BuildContext.hpp>

#include <map>

namespace vix::cli::build
{
  namespace
  {
    static std::map<std::string, process::Preset> builtin_presets()
    {
      std::map<std::string, process::Preset> presets;

      presets.emplace(
          "dev",
          process::Preset{"dev", "Ninja", "Debug", "build-dev"});

      presets.emplace(
          "dev-ninja",
          process::Preset{"dev-ninja", "Ninja", "Debug", "build-ninja"});

      presets.emplace(
          "release",
          process::Preset{"release", "Ninja", "Release", "build-release"});

      return presets;
    }
  } // namespace

  std::optional<process::Preset> resolve_builtin_preset(
      const std::string &name)
  {
    const auto presets = builtin_presets();
    const auto it = presets.find(name);

    if (it == presets.end())
      return std::nullopt;

    return it->second;
  }

  std::string default_build_target_name(
      const process::Options &opt,
      const process::Plan &plan)
  {
    if (!opt.buildTarget.empty())
      return opt.buildTarget;

    if (!plan.defaultTargetName.empty())
      return plan.defaultTargetName;

    return plan.projectDir.filename().string();
  }

  std::string default_graph_target_name(
      const process::Options &opt,
      const process::Plan &plan)
  {
    if (!opt.buildTarget.empty())
      return opt.buildTarget;

    if (!plan.defaultTargetName.empty())
      return plan.defaultTargetName;

    return plan.projectDir.filename().string();
  }
  fs::path default_project_executable_path(
      const process::Options &opt,
      const process::Plan &plan)
  {
    const std::string target = default_build_target_name(opt, plan);

#ifdef _WIN32
    return plan.buildDir / (target + ".exe");
#else
    return plan.buildDir / target;
#endif
  }

} // namespace vix::cli::build
