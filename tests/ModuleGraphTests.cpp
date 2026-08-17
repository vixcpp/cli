#include <vix/cli/modules/ModuleGraph.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using vix::cli::app::AppModule;
using vix::cli::modules::ModuleGraph;

static AppModule module(std::string name, std::vector<std::string> deps = {}, bool enabled = true, std::string path = {})
{ AppModule out; out.name = std::move(name); out.depends = std::move(deps); out.enabled = enabled; out.path = std::move(path); return out; }
static ModuleGraph graph(std::vector<AppModule> modules)
{ std::string error; auto out = ModuleGraph::from_app_modules(modules, error); assert(out.valid()); return out; }
static void invalid(std::vector<AppModule> modules)
{ std::string error; auto out = ModuleGraph::from_app_modules(modules, error); assert(!out.valid()); assert(!error.empty()); }

int main()
{
  { auto g = graph({}); std::vector<std::string> order; assert(g.topological_order(order)); assert(order.empty()); }
  { auto g = graph({module("auth")}); std::vector<std::string> c; assert(g.dependency_closure("auth", c)); assert((c == std::vector<std::string>{"auth"})); }
  { auto g = graph({module("orders", {"users"}), module("auth"), module("users", {"auth"}), module("billing", {"auth"})}); std::vector<std::string> order; assert(g.topological_order(order)); assert((order == std::vector<std::string>{"auth", "billing", "users", "orders"})); std::vector<std::string> c; assert(g.dependency_closure("orders", c)); assert((c == std::vector<std::string>{"auth", "users", "orders"})); }
  { auto g = graph({module("root", {"left", "right"}), module("left", {"leaf"}), module("right", {"leaf"}), module("leaf")}); std::vector<std::string> c; assert(g.dependency_closure("root", c)); assert((c == std::vector<std::string>{"leaf", "left", "right", "root"})); }
  invalid({module("a", {"missing"})}); invalid({module("a", {"a"})}); invalid({module("foo-bar"), module("foo_bar")}); invalid({module("Auth"), module("auth")}); invalid({module("a", {"b"}), module("b", {"a"})}); invalid({module("a", {"b"}), module("b", {"c"}), module("c", {"a"})}); invalid({module("a", {"b"}), module("b", {"a"}), module("c", {"d"}), module("d", {"c"})});
  invalid({module("enabled", {"disabled"}), module("disabled", {}, false)});
  { auto g = graph({module("live"), module("off", {}, false)}); std::vector<std::string> active; assert(g.active_closure(active)); assert((active == std::vector<std::string>{"live"})); }
  namespace fs = std::filesystem; const fs::path root = fs::temp_directory_path() / "vix_module_graph_tests"; fs::remove_all(root); fs::create_directories(root / "modules/with space"); fs::create_directories(root / "modules/a");
  { auto g = graph({module("space", {}, true, "modules/with space")}); std::string e; assert(g.validate_paths(root, true, e)); }
  { auto g = graph({module("a", {}, true, "modules/a"), module("b", {}, true, "modules/./a")}); std::string e; assert(!g.validate_paths(root, false, e)); }
  { auto g = graph({module("outside", {}, true, "../outside")}); std::string e; assert(!g.validate_paths(root, false, e)); }
  { auto g = graph({module("file", {}, true, "file")}); std::ofstream(root / "file") << "x"; std::string e; assert(!g.validate_paths(root, false, e)); }
  for (int count : {10, 100, 500}) { std::vector<AppModule> modules; for (int i=0;i<count;++i) { std::vector<std::string> deps; if (i) deps.push_back("m" + std::to_string(i-1)); if (i > 2 && i % 3 == 0) deps.push_back("m" + std::to_string(i-3)); modules.push_back(module("m" + std::to_string(i), deps)); } auto g = graph(modules); std::vector<std::string> order; assert(g.topological_order(order)); assert(order.size() == modules.size()); modules.front().depends.push_back("m" + std::to_string(count-1)); std::string e; auto cyclic = ModuleGraph::from_app_modules(modules, e); assert(!cyclic.valid()); }
  fs::remove_all(root); std::cout << "ModuleGraphTests passed\n";
}
