/**
 *
 *  @file BuildContextCompatTests.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  CLI build context compatibility tests
 *
 */

#include <vix/cli/build/BuildContext.hpp>

#include <cassert>
#include <filesystem>
#include <type_traits>

int main()
{
  namespace build = vix::cli::build;
  namespace process = vix::cli::process;
  namespace engine = vix::engine;

  static_assert(std::is_same_v<process::Preset, engine::Preset>);
  static_assert(std::is_same_v<process::Plan, engine::ExecutionPlan>);

  const auto preset = build::resolve_builtin_preset("dev-ninja");
  assert(preset.has_value());
  assert(preset->name == "dev-ninja");
  assert(preset->generator == "Ninja");
  assert(preset->buildType == "Debug");
  assert(preset->buildDirName == "build-ninja");
  assert(!build::resolve_builtin_preset("unknown").has_value());

  process::Options options;
  process::Plan plan;
  plan.userProjectDir = "/tmp/app";
  plan.cmakeSourceDir = "/tmp/app";
  plan.projectDir = "/tmp/app";
  plan.preset = *preset;
  plan.buildDir = "/tmp/app/build-ninja";

  options.buildTarget = "explicit";
  assert(build::default_build_target_name(options, plan) == "explicit");
  assert(build::default_graph_target_name(options, plan) == "explicit");

  options.buildTarget.clear();
  plan.defaultTargetName = "manifest-name";
  assert(build::default_build_target_name(options, plan) == "manifest-name");

  plan.defaultTargetName.clear();
  assert(build::default_build_target_name(options, plan) == "app");
#ifdef _WIN32
  const std::filesystem::path expectedExe = "/tmp/app/build-ninja/app.exe";
#else
  const std::filesystem::path expectedExe = "/tmp/app/build-ninja/app";
#endif
  assert(build::default_project_executable_path(options, plan) == expectedExe);

  return 0;
}
