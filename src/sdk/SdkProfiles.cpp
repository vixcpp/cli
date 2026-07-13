#include <vix/cli/sdk/SdkProfiles.hpp>

#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Strings.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::cli::sdk
{
  using json = nlohmann::json;

  namespace
  {
    const std::vector<std::string> kProfiles{
        "default", "web", "data", "desktop", "p2p", "game", "agent", "all"};

    const std::vector<std::string> kBaseModules{
        "cli", "core", "json", "error", "path", "fs", "io", "env", "os",
        "utils", "log", "async", "time", "process", "threadpool", "template",
        "ui", "note", "net", "sync", "crypto", "cache", "requests"};

    std::string lower_copy(std::string s)
    {
      std::transform(
          s.begin(), s.end(), s.begin(),
          [](unsigned char c)
          { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    std::vector<std::string> sorted_unique(std::vector<std::string> values)
    {
      std::sort(values.begin(), values.end());
      values.erase(std::unique(values.begin(), values.end()), values.end());
      return values;
    }

    std::string join_strings(const std::vector<std::string> &values, const std::string &sep)
    {
      std::ostringstream out;
      for (std::size_t i = 0; i < values.size(); ++i)
      {
        if (i > 0)
          out << sep;
        out << values[i];
      }
      return out.str();
    }

    SdkProfileInfo make_info(
        const std::string &name,
        std::string title,
        std::string description,
        std::vector<std::string> extraModules,
        std::vector<std::string> linuxDeps,
        std::vector<std::string> macosDeps,
        std::vector<std::string> windowsDeps,
        std::vector<std::string> notes)
    {
      SdkProfileInfo info;
      info.name = name;
      info.title = std::move(title);
      info.description = std::move(description);
      info.modules = kBaseModules;
      info.modules.insert(info.modules.end(), extraModules.begin(), extraModules.end());
      info.modules = sorted_unique(info.modules);
      info.linuxDeps = sorted_unique(std::move(linuxDeps));
      info.macosDeps = sorted_unique(std::move(macosDeps));
      info.windowsDeps = sorted_unique(std::move(windowsDeps));
      info.notes = std::move(notes);
      return info;
    }

    std::optional<json> read_json_if_exists(const fs::path &path)
    {
      std::error_code ec;
      if (!fs::exists(path, ec) || ec)
        return std::nullopt;

      std::ifstream in(path);
      if (!in)
        return std::nullopt;

      try
      {
        json j;
        in >> j;
        return j;
      }
      catch (...)
      {
        return std::nullopt;
      }
    }

    bool file_exists(const fs::path &path)
    {
      std::error_code ec;
      return fs::is_regular_file(path, ec) && !ec;
    }

    std::string read_text(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};
      return std::string(
          std::istreambuf_iterator<char>(in),
          std::istreambuf_iterator<char>());
    }

    bool write_text(const fs::path &path, const std::string &content, std::string &error)
    {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);
      if (ec)
      {
        error = "failed to create directory: " + path.parent_path().string();
        return false;
      }

      std::ofstream out(path, std::ios::binary);
      if (!out)
      {
        error = "failed to write: " + path.string();
        return false;
      }
      out << content;
      return true;
    }

    std::string cmake_quote(const fs::path &path)
    {
      std::string s = path.generic_string();
      std::string out;
      out.reserve(s.size() + 2);
      out.push_back('"');
      for (char c : s)
      {
        if (c == '\\' || c == '"')
          out.push_back('\\');
        out.push_back(c);
      }
      out.push_back('"');
      return out;
    }

    fs::path targets_file(const InstalledSdkProfile &profile)
    {
      return profile.configDir / "VixTargets.cmake";
    }

    std::vector<fs::path> target_config_files(const InstalledSdkProfile &profile)
    {
      std::vector<fs::path> files;
      std::error_code ec;
      if (!fs::exists(profile.configDir, ec) || ec)
        return files;

      for (const auto &entry : fs::directory_iterator(profile.configDir, ec))
      {
        if (ec)
          break;
        const std::string name = entry.path().filename().string();
        if (name.rfind("VixTargets-", 0) == 0 &&
            entry.path().extension() == ".cmake")
        {
          files.push_back(entry.path());
        }
      }
      std::sort(files.begin(), files.end());
      return files;
    }

    std::string cmake_path_literal(const fs::path &path)
    {
      std::string s = path.generic_string();
      std::string out;
      out.reserve(s.size());
      for (char c : s)
      {
        if (c == '\\' || c == '"')
          out.push_back('\\');
        out.push_back(c);
      }
      return out;
    }

    std::string replace_import_prefix(std::string text, const fs::path &prefix)
    {
      const std::string replacement = cmake_path_literal(prefix);
      const std::string marker = "${_IMPORT_PREFIX}";
      std::size_t pos = 0;
      while ((pos = text.find(marker, pos)) != std::string::npos)
      {
        text.replace(pos, marker.size(), replacement);
        pos += replacement.size();
      }
      return text;
    }

    std::optional<std::string> extract_target_block(
        const std::string &text,
        const std::string &target)
    {
      const std::string needle = "# Create imported target " + target + "\n";
      const std::size_t begin = text.find(needle);
      if (begin == std::string::npos)
        return std::nullopt;

      std::size_t end = text.find("\n# Create imported target ", begin + needle.size());
      const std::size_t loadInfo = text.find("\n# Load information for each installed configuration.", begin + needle.size());
      if (end == std::string::npos || (loadInfo != std::string::npos && loadInfo < end))
        end = loadInfo;
      if (end == std::string::npos)
        end = text.size();

      return text.substr(begin, end - begin);
    }

    std::optional<std::string> extract_config_block(
        const std::string &text,
        const std::string &target)
    {
      const std::string importHeader = "# Import target \"" + target + "\"";
      std::size_t begin = text.find(importHeader);
      if (begin == std::string::npos)
      {
        const std::string needle = "set_property(TARGET " + target + " APPEND PROPERTY IMPORTED_CONFIGURATIONS";
        const std::size_t found = text.find(needle);
        if (found == std::string::npos)
          return std::nullopt;

        const std::size_t startLine = text.rfind('\n', found);
        begin = (startLine == std::string::npos) ? 0 : startLine + 1;
      }
      else
      {
        const std::size_t startLine = text.rfind('\n', begin);
        begin = (startLine == std::string::npos) ? 0 : startLine + 1;
      }

      std::size_t next = text.find("\n# Import target \"", begin + 1);
      if (next == std::string::npos)
        next = text.size();

      return text.substr(begin, next - begin);
    }

    std::string trim_copy_local(const std::string &value)
    {
      std::size_t first = 0;
      while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
      std::size_t last = value.size();
      while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;
      return value.substr(first, last - first);
    }

    bool starts_with_local(const std::string &value, const std::string &prefix)
    {
      return value.rfind(prefix, 0) == 0;
    }

    std::string target_overlay_block(
        const std::string &targetBlock,
        const std::string &target,
        const fs::path &installDir)
    {
      const std::string rewritten = replace_import_prefix(targetBlock, installDir);
      std::istringstream in(rewritten);
      std::ostringstream body;
      std::string addArgs = "INTERFACE IMPORTED";
      std::string line;

      while (std::getline(in, line))
      {
        const std::string trimmed = trim_copy_local(line);
        const std::string addPrefix = "add_library(" + target;
        if (starts_with_local(trimmed, "# Create imported target "))
          continue;
        if (trimmed == "if(NOT TARGET " + target + ")" ||
            trimmed == "if (NOT TARGET " + target + ")")
          continue;
        if (starts_with_local(trimmed, addPrefix))
        {
          const std::size_t argBegin = trimmed.find(target) + target.size();
          const std::size_t argEnd = trimmed.rfind(')');
          if (argEnd != std::string::npos && argEnd > argBegin)
            addArgs = trim_copy_local(trimmed.substr(argBegin, argEnd - argBegin));
          continue;
        }
        if (trimmed == "endif()")
          continue;
        body << line << "\n";
      }

      std::ostringstream out;
      out << "if(NOT TARGET " << target << ")\n";
      out << "  add_library(" << target << " " << addArgs << ")\n";
      out << "endif()\n";
      out << body.str();
      return out.str();
    }

    std::string profile_for_module_or_empty(const std::string &module)
    {
      if (module == "db" || module == "orm" || module == "kv")
        return "data";
      if (module == "websocket" || module == "middleware" ||
          module == "validation" || module == "webrpc")
        return "web";
      if (module == "p2p" || module == "p2p_http")
        return "p2p";
      if (module == "game")
        return "game";
      if (module == "agent" || module == "ai_agent")
        return "agent";
      return {};
    }

    bool profile_has_module(const std::string &profile, const std::string &module)
    {
      const auto info = profile_info(profile);
      if (!info)
        return false;
      return std::find(info->modules.begin(), info->modules.end(), module) != info->modules.end();
    }

    std::optional<std::string> installed_profile_for_common_module(const std::string &module)
    {
      for (const std::string &profile : kProfiles)
      {
        if (!profile_has_module(profile, module))
          continue;
        if (read_installed_profile(profile))
          return profile;
      }
      return std::nullopt;
    }

    std::string default_profile_for_common_module(const std::string &module)
    {
      if (profile_has_module("default", module))
        return "default";
      return {};
    }

    std::string module_to_exported_target(const std::string &module)
    {
      if (module == "time")
        return "vix::vix_time";
      if (module == "conversion")
        return "vix::vix_conversion";
      if (module == "validation")
        return "vix::vix_validation";
      if (module == "webrpc")
        return "vix::vix_webrpc";
      if (module == "agent")
        return "vix::ai_agent";
      return "vix::" + module;
    }

    std::string public_target_for_module(const std::string &module)
    {
      if (module == "agent")
        return "vix::ai_agent";
      return "vix::" + module;
    }
  }

  const std::vector<std::string> &profile_names()
  {
    return kProfiles;
  }

  bool is_supported_profile(const std::string &profile)
  {
    return std::find(kProfiles.begin(), kProfiles.end(), profile) != kProfiles.end();
  }

  std::optional<SdkProfileInfo> profile_info(const std::string &rawProfile)
  {
    const std::string profile = lower_copy(rawProfile);

    const std::vector<std::string> baseLinuxDeps{
        "build-essential", "cmake", "ninja-build", "pkg-config",
        "ca-certificates", "git", "curl", "tar", "unzip", "zip",
        "nlohmann-json3-dev", "libssl-dev", "zlib1g-dev",
        "libsqlite3-dev", "libbrotli-dev", "libspdlog-dev", "libfmt-dev"};
    const std::vector<std::string> baseMacosDeps{
        "xcode-select --install",
        "brew install cmake ninja pkg-config openssl@3 nlohmann-json spdlog fmt"};
    const std::vector<std::string> baseWindowsDeps{
        "Visual Studio 2022 Build Tools", "CMake", "Ninja", "Git",
        "WebView2 Runtime",
        "vcpkg install openssl sqlite3 zlib brotli nlohmann-json spdlog fmt --triplet x64-windows"};

    if (profile == "default")
      return make_info(profile, "Default SDK",
                       "Balanced SDK for normal Vix.cpp projects and local development.",
                       {}, baseLinuxDeps, baseMacosDeps, baseWindowsDeps,
                       {"Good first choice for most projects."});

    if (profile == "web")
      return make_info(profile, "Web SDK",
                       "SDK for HTTP, middleware, WebSocket, validation, crypto, WebRPC and requests.",
                       {"websocket", "middleware", "validation", "webrpc"},
                       baseLinuxDeps, baseMacosDeps, baseWindowsDeps,
                       {"Use this for APIs, realtime apps and backend services."});

    if (profile == "data")
      return make_info(profile, "Data SDK",
                       "SDK for database, ORM, key-value storage and cache workflows.",
                       {"db", "orm", "kv"},
                       baseLinuxDeps, baseMacosDeps, baseWindowsDeps,
                       {"SQLite is the default lightweight local database dependency."});

    if (profile == "desktop")
      return make_info(profile, "Desktop SDK",
                       "SDK for desktop apps using the Vix UI desktop shell.",
                       {"desktop", "ui-webview"},
                       sorted_unique(std::vector<std::string>(baseLinuxDeps.begin(), baseLinuxDeps.end())),
                       baseMacosDeps, baseWindowsDeps,
                       {"On Linux, WebKitGTK is required for the desktop WebView backend."});

    if (profile == "p2p")
      return make_info(profile, "P2P SDK",
                       "SDK for peer-to-peer networking, crypto and local-first sync systems.",
                       {"p2p", "p2p_http"}, baseLinuxDeps, baseMacosDeps, baseWindowsDeps,
                       {"Use this for node, discovery, replication and local network workflows."});

    if (profile == "game")
      return make_info(profile, "Game SDK",
                       "SDK for game and realtime rendering workflows.",
                       {"game"}, baseLinuxDeps, baseMacosDeps, baseWindowsDeps,
                       {"SDL2/OpenGL dependencies are required for native game examples."});

    if (profile == "agent")
      return make_info(profile, "Agent SDK",
                       "SDK for AI agent tooling and controlled automation workflows.",
                       {"agent"}, baseLinuxDeps, baseMacosDeps, baseWindowsDeps,
                       {"Use this for agent-oriented tooling, local state and runtime orchestration."});

    if (profile == "all")
      return make_info(profile, "Full SDK",
                       "Complete SDK with web, data, desktop, p2p, game and agent modules.",
                       {"websocket", "middleware", "validation", "webrpc",
                        "db", "orm", "kv", "p2p", "p2p_http", "game", "agent"},
                       baseLinuxDeps, baseMacosDeps, baseWindowsDeps,
                       {"This profile is heavier. Prefer a smaller profile when possible."});

    return std::nullopt;
  }

  std::optional<std::string> provider_profile_for_module(const std::string &module)
  {
    const std::string direct = profile_for_module_or_empty(module);
    if (!direct.empty())
      return direct;
    return std::nullopt;
  }

  std::optional<std::string> module_for_target(const std::string &target)
  {
    if (target.rfind("vix::", 0) != 0)
      return std::nullopt;
    std::string name = target.substr(5);
    if (name == "vix" || name == "deps")
      return std::nullopt;
    if (name == "ws")
      name = "websocket";
    if (name == "mw")
      name = "middleware";
    if (name == "ai_agent")
      name = "agent";
    if (name == "vix_time")
      name = "time";
    if (name == "vix_conversion")
      name = "conversion";
    if (name == "vix_validation")
      name = "validation";
    if (name == "vix_webrpc")
      name = "webrpc";
    return name;
  }

  std::string target_for_module(const std::string &module)
  {
    return public_target_for_module(module);
  }

  std::set<std::string> normalize_required_vix_targets(const std::set<std::string> &targets)
  {
    std::set<std::string> modules;
    for (const std::string &target : targets)
    {
      const auto module = module_for_target(target);
      if (module)
        modules.insert(*module);
    }
    return modules;
  }

  fs::path sdk_root_dir()
  {
#ifdef _WIN32
    if (const char *home = vix::utils::vix_getenv("USERPROFILE"))
      return fs::path(home) / ".vix" / "sdk";
#else
    if (const char *home = vix::utils::vix_getenv("HOME"))
      return fs::path(home) / ".vix" / "sdk";
#endif
    return fs::current_path() / ".vix" / "sdk";
  }

  std::optional<InstalledSdkProfile> read_installed_profile(const std::string &profile)
  {
    const fs::path root = sdk_root_dir() / profile;
    const auto meta = read_json_if_exists(root / "current.json");
    if (!meta)
      return std::nullopt;

    const std::string version = meta->value("installed_version", meta->value("version", ""));
    const std::string installDirString = meta->value("install_dir", "");
    if (version.empty() || installDirString.empty())
      return std::nullopt;

    InstalledSdkProfile installed;
    installed.profile = profile;
    installed.version = version;
    installed.installDir = fs::path(installDirString);
    installed.configDir = installed.installDir / "lib" / "cmake" / "Vix";

    if (!file_exists(installed.configDir / "VixConfig.cmake") ||
        !file_exists(installed.configDir / "VixTargets.cmake"))
      return std::nullopt;

    return installed;
  }

  SdkResolution resolve_profiles_for_modules(const std::set<std::string> &modules)
  {
    SdkResolution result;
    result.ok = true;
    result.requiredModules.assign(modules.begin(), modules.end());

    std::set<std::string> neededProfiles;
    for (const std::string &module : modules)
    {
      auto provider = provider_profile_for_module(module);
      if (!provider)
        provider = installed_profile_for_common_module(module);
      if (!provider)
      {
        const std::string fallback = default_profile_for_common_module(module);
        if (!fallback.empty())
          provider = fallback;
      }
      if (!provider)
        continue;
      neededProfiles.insert(*provider);
      result.moduleProviders[module] = *provider;
    }

    if (neededProfiles.empty())
      return result;

    result.selectedProfiles.assign(neededProfiles.begin(), neededProfiles.end());

    for (const std::string &profile : result.selectedProfiles)
    {
      const auto installed = read_installed_profile(profile);
      if (!installed)
      {
        result.ok = false;
        for (const auto &[module, provider] : result.moduleProviders)
        {
          if (provider == profile)
            result.missingModules.push_back(module);
        }
        continue;
      }

      if (result.version.empty())
        result.version = installed->version;
      else if (result.version != installed->version)
      {
        result.ok = false;
        result.error = "installed SDK profiles have incompatible versions";
      }

      result.installed[profile] = *installed;
    }

    return result;
  }

  bool write_composed_sdk_config(
      const fs::path &destinationRoot,
      const SdkResolution &resolution,
      std::string &error)
  {
    if (resolution.selectedProfiles.empty())
      return true;

    const fs::path configDir = destinationRoot / "lib" / "cmake" / "Vix";
    const std::string primaryProfile = resolution.selectedProfiles.front();
    const auto primaryIt = resolution.installed.find(primaryProfile);
    if (primaryIt == resolution.installed.end())
    {
      error = "primary SDK profile is not installed: " + primaryProfile;
      return false;
    }

    std::ostringstream config;
    config << "include(CMakeFindDependencyMacro)\n";
    config << "include(" << cmake_quote(primaryIt->second.configDir / "VixConfig.cmake") << ")\n\n";
    config << "set(VIX_COMPOSED_SDK_VERSION \"" << resolution.version << "\")\n";
    config << "set(VIX_COMPOSED_SDK_PROFILES \"" << join_strings(resolution.selectedProfiles, ";") << "\")\n\n";

    std::map<std::string, std::string> overlayProviders = resolution.moduleProviders;
    for (const std::string &profile : resolution.selectedProfiles)
    {
      if (profile == primaryProfile)
        continue;
      const auto info = profile_info(profile);
      if (!info)
        continue;
      for (const std::string &module : info->modules)
      {
        const auto provider = provider_profile_for_module(module);
        if (provider && *provider == profile)
          overlayProviders[module] = profile;
      }
    }

    std::ostringstream overlays;
    for (const auto &[module, provider] : overlayProviders)
    {
      if (provider == primaryProfile)
        continue;
      const auto sdkIt = resolution.installed.find(provider);
      if (sdkIt == resolution.installed.end())
        continue;

      const std::string exportedTarget = module_to_exported_target(module);
      const std::string publicTarget = public_target_for_module(module);
      const std::string targetsText = read_text(targets_file(sdkIt->second));
      const auto targetBlock = extract_target_block(targetsText, exportedTarget);
      if (!targetBlock)
      {
        error = "SDK profile '" + provider + "' does not export target " + publicTarget;
        return false;
      }

      overlays << target_overlay_block(*targetBlock, exportedTarget, sdkIt->second.installDir) << "\n";
      for (const fs::path &configFile : target_config_files(sdkIt->second))
      {
        const std::string configText = read_text(configFile);
        const auto configBlock = extract_config_block(configText, exportedTarget);
        if (configBlock)
          overlays << replace_import_prefix(*configBlock, sdkIt->second.installDir) << "\n";
      }
      overlays << "\n";

      if (publicTarget != exportedTarget)
      {
        overlays << "if(TARGET " << exportedTarget << " AND NOT TARGET " << publicTarget << ")\n";
        overlays << "  add_library(" << publicTarget << " ALIAS " << exportedTarget << ")\n";
        overlays << "endif()\n\n";
      }
    }

    config << overlays.str();

    for (const std::string &module : resolution.requiredModules)
    {
      const std::string publicTarget = public_target_for_module(module);
      config << "if(NOT TARGET " << publicTarget << ")\n";
      config << "  set(Vix_FOUND FALSE)\n";
      config << "  set(Vix_NOT_FOUND_MESSAGE \"Vix SDK composition did not provide " << publicTarget << "\")\n";
      config << "  return()\n";
      config << "endif()\n";
    }

    if (!write_text(configDir / "VixConfig.cmake", config.str(), error))
      return false;

    const std::string lowercaseConfig = "include(\"${CMAKE_CURRENT_LIST_DIR}/VixConfig.cmake\")\n";
    if (!write_text(configDir / "vixConfig.cmake", lowercaseConfig, error))
      return false;

    const fs::path primaryVersion = primaryIt->second.configDir / "VixConfigVersion.cmake";
    if (file_exists(primaryVersion))
    {
      const std::string versionText = read_text(primaryVersion);
      if (!write_text(configDir / "VixConfigVersion.cmake", versionText, error))
        return false;
      if (!write_text(configDir / "vixConfigVersion.cmake", versionText, error))
        return false;
    }

    json meta{
        {"kind", "composed-sdk"},
        {"version", resolution.version},
        {"profiles", resolution.selectedProfiles},
        {"required_modules", resolution.requiredModules},
    };
    if (!write_text(destinationRoot / "composition.json", meta.dump(2) + "\n", error))
      return false;

    return true;
  }
}
