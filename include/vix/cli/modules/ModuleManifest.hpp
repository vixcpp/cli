#ifndef VIX_CLI_MODULES_MODULE_MANIFEST_HPP
#define VIX_CLI_MODULES_MODULE_MANIFEST_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::modules
{
  struct ModuleWebSocket
  {
    std::string workflow, path, host;
    std::optional<unsigned short> port;
    bool longPolling{false};
    bool metrics{false};
  };
  struct ModuleManifest
  {
    std::string name, kind, workflow, routePrefix, exportInclude;
    bool runtime{false}, testsEnabled{true};
    std::vector<std::string> registryDependencies, links;
    std::optional<ModuleWebSocket> websocket;
  };
  struct ModuleManifestLoadResult
  {
    ModuleManifest manifest;
    std::string error;
    bool success() const { return error.empty(); }
  };
  ModuleManifestLoadResult load_module_manifest(const std::filesystem::path &path);
}
#endif
