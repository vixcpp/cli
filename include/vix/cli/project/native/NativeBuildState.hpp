#ifndef VIX_CLI_PROJECT_NATIVE_NATIVE_BUILD_STATE_HPP
#define VIX_CLI_PROJECT_NATIVE_NATIVE_BUILD_STATE_HPP

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>

#include <vix/engine/SanitizerMode.hpp>

namespace vix::cli::project::native
{
  struct NativeToolchainIdentity
  {
    std::string compilerPath;
    std::string compilerFamily;
    std::string compilerVersion;
    std::string compilerTarget;
    std::uintmax_t executableSize{0};
    std::int64_t executableMtimeTicks{0};
    bool executableStatAvailable{false};
  };

  struct NativeBuildStateContext
  {
    std::string id;
    std::string configuration;
    std::string platform;
    NativeToolchainIdentity toolchain;
    std::string generator;
    vix::engine::SanitizerMode diagnosticMode{vix::engine::SanitizerMode::None};
    std::filesystem::path projectRoot;
    std::filesystem::path root;
    std::filesystem::path generatedSourceDir;
    std::filesystem::path backendDir;
    std::filesystem::path metadataPath;
    std::filesystem::path configuredInputSignaturePath;
    std::filesystem::path snapshotCachePath;
  };

  [[nodiscard]] NativeBuildStateContext make_native_build_state_context(
      const std::filesystem::path &projectRoot, std::string_view configuration,
      std::string_view platform, const NativeToolchainIdentity &toolchain,
      std::string_view generator,
      vix::engine::SanitizerMode diagnosticMode);

  [[nodiscard]] std::filesystem::path native_build_state_root(
      const std::filesystem::path &projectRoot);

  [[nodiscard]] std::filesystem::path native_toolchain_identity_cache_path(
      const std::filesystem::path &projectRoot);

  [[nodiscard]] bool reset_native_build_state_context(
      const NativeBuildStateContext &context, std::string &error);

  [[nodiscard]] std::string native_build_context_metadata(
      const NativeBuildStateContext &context, std::string_view generatedInputDigest);
}

#endif
