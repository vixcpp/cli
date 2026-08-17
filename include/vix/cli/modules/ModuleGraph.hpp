#ifndef VIX_CLI_MODULES_MODULE_GRAPH_HPP
#define VIX_CLI_MODULES_MODULE_GRAPH_HPP

#include <filesystem>
#include <string>
#include <vector>

#include <vix/cli/app/AppManifest.hpp>

namespace vix::cli::modules
{
  struct ModuleNode
  {
    std::string name;
    std::string identity;
    std::filesystem::path path;
    std::string kind;
    bool enabled{true};
    std::vector<std::string> dependencies;
  };

  class ModuleGraph
  {
  public:
    static std::string canonical_identity(const std::string &name);
    static std::string case_folded_identity(const std::string &name);
    static ModuleGraph from_app_modules(
        const std::vector<vix::cli::app::AppModule> &modules,
        std::string &error);

    bool valid() const { return error_.empty(); }
    const std::string &error() const { return error_; }
    bool contains(const std::string &name) const;
    const ModuleNode *find(const std::string &name) const;
    const std::vector<ModuleNode> &nodes() const { return nodes_; }

    // Validates paths against a project root without modifying project files.
    bool validate_paths(const std::filesystem::path &projectRoot,
                        bool requireDirectories,
                        std::string &error) const;
    bool topological_order(std::vector<std::string> &order,
                           bool activeOnly = false,
                           std::string *error = nullptr) const;
    bool dependency_closure(const std::string &name,
                            std::vector<std::string> &closure,
                            std::string *error = nullptr) const;
    bool active_closure(std::vector<std::string> &closure,
                        std::string *error = nullptr) const;

  private:
    std::vector<ModuleNode> nodes_;
    std::string error_;
  };
}
#endif
