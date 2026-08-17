#include <vix/cli/modules/ModuleGraph.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <system_error>

namespace vix::cli::modules
{
  namespace fs = std::filesystem;
  namespace
  {
    std::string trim(std::string value)
    {
      const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
      const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
      return first >= last ? std::string{} : std::string(first, last);
    }

    bool valid_name(const std::string &name)
    {
      return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-';
      });
    }

    bool under_root(const fs::path &root, const fs::path &path)
    {
      auto r = root.begin(); auto p = path.begin();
      for (; r != root.end(); ++r, ++p)
        if (p == path.end() || *r != *p) return false;
      return true;
    }

    fs::path safe_canonical(const fs::path &path)
    {
      std::error_code ec;
      const fs::path result = fs::weakly_canonical(path, ec);
      return ec ? path.lexically_normal() : result;
    }
  }

  std::string ModuleGraph::canonical_identity(const std::string &raw)
  {
    std::string value = trim(raw);
    for (char &c : value) if (c == '-') c = '_';
    return value;
  }

  std::string ModuleGraph::case_folded_identity(const std::string &raw)
  {
    std::string value = canonical_identity(raw);
    for (char &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
  }

  ModuleGraph ModuleGraph::from_app_modules(
      const std::vector<vix::cli::app::AppModule> &modules,
      std::string &error)
  {
    ModuleGraph graph;
    std::map<std::string, std::string> identities;
    std::map<std::string, std::string> folded;
    for (const auto &module : modules)
    {
      const std::string identity = canonical_identity(module.name);
      if (!valid_name(module.name) || identity.empty())
      { graph.error_ = "Invalid module name: " + module.name; break; }
      const auto exact = identities.emplace(identity, module.name);
      if (!exact.second)
      { graph.error_ = "Module identity/CMake target collision: '" + exact.first->second + "' and '" + module.name + "' both normalize to '" + identity + "'."; break; }
      const std::string foldedIdentity = case_folded_identity(module.name);
      const auto insensitive = folded.emplace(foldedIdentity, module.name);
      if (!insensitive.second)
      { graph.error_ = "Case-insensitive module identity collision: '" + insensitive.first->second + "' and '" + module.name + "'."; break; }
      ModuleNode node;
      node.name = module.name; node.identity = identity;
      node.path = module.path.empty() ? fs::path("modules") / identity : fs::path(module.path);
      node.kind = module.kind; node.enabled = module.enabled;
      graph.nodes_.push_back(std::move(node));
    }
    if (graph.error_.empty())
    {
      std::map<std::string, std::size_t> index;
      for (std::size_t i = 0; i < graph.nodes_.size(); ++i) index.emplace(graph.nodes_[i].identity, i);
      for (std::size_t i = 0; i < modules.size(); ++i)
      {
        std::set<std::string> seen;
        for (const std::string &raw : modules[i].depends)
        {
          const std::string dependency = canonical_identity(raw);
          const auto it = index.find(dependency);
          if (dependency.empty() || it == index.end()) { graph.error_ = "Module '" + graph.nodes_[i].name + "' depends on undeclared module '" + raw + "'."; break; }
          if (dependency == graph.nodes_[i].identity) { graph.error_ = "Module '" + graph.nodes_[i].name + "' depends on itself."; break; }
          if (graph.nodes_[i].enabled && !graph.nodes_[it->second].enabled) { graph.error_ = "Enabled module '" + graph.nodes_[i].name + "' depends on disabled module '" + graph.nodes_[it->second].name + "'."; break; }
          if (seen.insert(dependency).second) graph.nodes_[i].dependencies.push_back(dependency);
        }
        if (!graph.error_.empty()) break;
        std::sort(graph.nodes_[i].dependencies.begin(), graph.nodes_[i].dependencies.end());
      }
    }
    std::vector<std::string> ignored;
    if (graph.error_.empty()) graph.topological_order(ignored, false, &graph.error_);
    error = graph.error_;
    return graph;
  }

  bool ModuleGraph::contains(const std::string &name) const { return find(name) != nullptr; }
  const ModuleNode *ModuleGraph::find(const std::string &name) const
  {
    const std::string identity = canonical_identity(name);
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const ModuleNode &node) { return node.identity == identity; });
    return it == nodes_.end() ? nullptr : &*it;
  }

  bool ModuleGraph::validate_paths(const fs::path &projectRoot, bool requireDirectories, std::string &error) const
  {
    if (!valid()) { error = error_; return false; }
    std::error_code ec;
    const fs::path root = safe_canonical(fs::absolute(projectRoot, ec));
    if (ec || root.empty()) { error = "Invalid project root: " + projectRoot.string(); return false; }
    std::map<fs::path, std::string> paths;
    for (const auto &node : nodes_)
    {
      if (node.path.empty()) { error = "Module '" + node.name + "' has an empty path."; return false; }
      const fs::path raw = node.path.is_absolute() ? node.path : root / node.path;
      const fs::path resolved = safe_canonical(raw);
      if (!under_root(root, resolved)) { error = "Module '" + node.name + "' path escapes the project root: " + node.path.string(); return false; }
      const auto inserted = paths.emplace(resolved, node.name);
      if (!inserted.second) { error = "Module path collision: '" + inserted.first->second + "' and '" + node.name + "' resolve to " + resolved.string(); return false; }
      const bool exists = fs::exists(resolved, ec);
      if (ec) { error = "Cannot inspect module path: " + resolved.string(); return false; }
      if (exists && !fs::is_directory(resolved, ec)) { error = "Module '" + node.name + "' path is not a directory: " + resolved.string(); return false; }
      if (ec) { error = "Cannot inspect module path: " + resolved.string(); return false; }
      if (requireDirectories && !exists) { error = "Module '" + node.name + "' directory does not exist: " + resolved.string(); return false; }
    }
    error.clear(); return true;
  }

  bool ModuleGraph::topological_order(std::vector<std::string> &order, bool activeOnly, std::string *error) const
  {
    order.clear(); if (!valid()) { if (error) *error = error_; return false; }
    std::map<std::string, const ModuleNode *> index;
    for (const auto &node : nodes_) if (!activeOnly || node.enabled) index.emplace(node.identity, &node);
    std::map<std::string, int> state; std::vector<std::string> stack;
    struct Frame { std::string id; std::size_t next; };
    for (const auto &entry : index)
    {
      if (state[entry.first] != 0) continue;
      std::vector<Frame> work{{entry.first, 0}}; state[entry.first] = 1; stack.push_back(entry.first);
      while (!work.empty())
      {
        Frame &frame = work.back(); const ModuleNode *node = index.at(frame.id);
        if (frame.next == node->dependencies.size()) { state[frame.id] = 2; order.push_back(node->name); stack.pop_back(); work.pop_back(); continue; }
        const std::string dep = node->dependencies[frame.next++];
        if (activeOnly && !index.count(dep)) continue;
        const int depState = state[dep];
        if (depState == 1) { std::vector<std::string> cycle; auto begin = std::find(stack.begin(), stack.end(), dep); for (; begin != stack.end(); ++begin) cycle.push_back(index.at(*begin)->name); cycle.push_back(index.at(dep)->name); if (error) { std::string text = "Module dependency cycle: "; for (std::size_t i=0;i<cycle.size();++i) text += (i ? " -> " : "") + cycle[i]; *error = text; } order.clear(); return false; }
        if (depState == 0) { state[dep] = 1; stack.push_back(dep); work.push_back({dep, 0}); }
      }
    }
    if (error)
      error->clear();
    return true;
  }

  bool ModuleGraph::dependency_closure(const std::string &name, std::vector<std::string> &closure, std::string *error) const
  {
    closure.clear(); const ModuleNode *root = find(name);
    if (!valid() || !root) { if (error) *error = valid() ? "Unknown module: " + name : error_; return false; }
    std::set<std::string> needed; std::vector<std::string> work{root->identity};
    while (!work.empty()) { const std::string id = work.back(); work.pop_back(); if (!needed.insert(id).second) continue; for (const auto &dep : find(id)->dependencies) work.push_back(dep); }
    std::vector<std::string> all; if (!topological_order(all, false, error)) return false;
    for (const auto &item : all) if (needed.count(canonical_identity(item))) closure.push_back(item);
    return true;
  }

  bool ModuleGraph::active_closure(std::vector<std::string> &closure, std::string *error) const
  {
    return topological_order(closure, true, error);
  }
}
