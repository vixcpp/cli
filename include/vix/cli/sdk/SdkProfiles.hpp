#ifndef VIX_CLI_SDK_PROFILES_HPP
#define VIX_CLI_SDK_PROFILES_HPP

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vix::cli::sdk
{
  namespace fs = std::filesystem;

  struct SdkProfileInfo
  {
    std::string name;
    std::string title;
    std::string description;
    std::vector<std::string> modules;
    std::vector<std::string> linuxDeps;
    std::vector<std::string> macosDeps;
    std::vector<std::string> windowsDeps;
    std::vector<std::string> notes;
  };

  struct InstalledSdkProfile
  {
    std::string profile;
    std::string version;
    fs::path installDir;
    fs::path configDir;
  };

  struct SdkResolution
  {
    bool ok{false};
    std::string error;
    std::vector<std::string> requiredModules;
    std::vector<std::string> selectedProfiles;
    std::vector<std::string> missingModules;
    std::map<std::string, std::string> moduleProviders;
    std::map<std::string, InstalledSdkProfile> installed;
    std::string version;
  };

  const std::vector<std::string> &profile_names();
  bool is_supported_profile(const std::string &profile);
  std::optional<SdkProfileInfo> profile_info(const std::string &profile);
  std::optional<std::string> provider_profile_for_module(const std::string &module);
  std::optional<std::string> module_for_target(const std::string &target);
  std::string target_for_module(const std::string &module);
  std::set<std::string> normalize_required_vix_targets(const std::set<std::string> &targets);

  fs::path sdk_root_dir();
  std::optional<InstalledSdkProfile> read_installed_profile(const std::string &profile);
  SdkResolution resolve_profiles_for_modules(const std::set<std::string> &modules);

  bool write_composed_sdk_config(
      const fs::path &destinationRoot,
      const SdkResolution &resolution,
      std::string &error);
}

#endif
