#ifndef VIX_CLI_PROJECT_CMAKE_CMAKE_BUILD_STATE_HPP
#define VIX_CLI_PROJECT_CMAKE_CMAKE_BUILD_STATE_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <vix/engine/ExecutionPlan.hpp>
#include <vix/engine/SanitizerMode.hpp>

namespace vix::cli::project::cmake
{
  struct ExternalCMakeBuildStateContext
  {
    std::string id;
    std::string configuration;
    std::string platform;
    std::string compilerIdentity;
    std::string generator;
    vix::engine::SanitizerMode diagnosticMode{vix::engine::SanitizerMode::None};
    std::filesystem::path projectRoot;
    std::filesystem::path root;
    std::filesystem::path backendDir;
    std::filesystem::path metadataPath;
    std::string canonicalIdentity;
    std::string metadata;
  };

  [[nodiscard]] ExternalCMakeBuildStateContext make_external_cmake_build_state_context(
    const std::filesystem::path &projectRoot,
    const vix::engine::ExecutionPlan &plan,
    std::string_view platform,
    std::string_view compilerIdentity,
    std::string_view sysroot,
    vix::engine::SanitizerMode diagnosticMode,
    const std::vector<std::string> &cmakeArguments,
    std::string_view toolchainContent);

  void apply_external_cmake_build_state_context(
    vix::engine::ExecutionPlan &plan,
    const ExternalCMakeBuildStateContext &context);

  [[nodiscard]] bool reset_external_cmake_build_state_context(
    const ExternalCMakeBuildStateContext &context,
    std::string &error);

  [[nodiscard]] bool prepare_external_cmake_build_state_context(
    const ExternalCMakeBuildStateContext &context,
    std::string &error);
}

#endif
