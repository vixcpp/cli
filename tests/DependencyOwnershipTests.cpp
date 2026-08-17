#include <vix/cli/app/AppCMakeGenerator.hpp>
#include <vix/cli/modules/DependencyOwnership.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using vix::cli::app::AppManifest;
using vix::cli::app::AppModule;
using vix::cli::modules::DependencyOwnerKind;
using vix::cli::modules::DependencySource;

static AppModule module(const std::string &name, bool enabled = true, const std::string &path = {})
{ AppModule m; m.name = name; m.enabled = enabled; m.path = path; return m; }
static void manifest(const fs::path &path, const std::string &deps, const std::string &links)
{ fs::create_directories(path.parent_path()); std::ofstream(path) << "name = \"x\"\n[deps]\nregistry = [" << deps << "]\nlinks = [" << links << "]\n"; }
static auto ownership(const AppManifest &app, const fs::path &root)
{ std::string error; auto graph = vix::cli::modules::ModuleGraph::from_app_modules(app.appModules, error); assert(graph.valid()); auto value = vix::cli::modules::build_dependency_ownership(app, graph, root); assert(value.success()); return value; }

int main()
{
  const fs::path root = fs::temp_directory_path() / "vix-dependency-ownership-tests";
  fs::remove_all(root); fs::create_directories(root);
  AppManifest app; app.name = "demo"; app.deps = {"gk/fmt@^10"};
  vix::cli::app::AppGitDependency git; git.name = "catch2"; git.git = "https://example.invalid/catch2.git"; git.target = "Catch2::Catch2"; app.gitDependencies.push_back(git);
  app.appModules = {module("auth"), module("billing"), module("analytics", false), module("space", true, "modules/with space")};
  manifest(root / "modules/auth/vix.module", "\"gk/jwt@^1\", \"gk/json@^1\"", "\"gk::jwt\", \"gk::json\"");
  manifest(root / "modules/billing/vix.module", "\"gk/json@^2\"", "\"gk::billing\"");
  manifest(root / "modules/with space/vix.module", "\"gk/space@^1\"", "\"gk::space\"");
  { std::ofstream out(root / "modules/auth/vix.module", std::ios::app); out << "[dependencies.spdlog]\ngit=\"https://example.invalid/spdlog.git\"\ntag=\"v1\"\ntarget=\"spdlog::spdlog\"\n"; }
  auto value = ownership(app, root);
  assert(value.requirements.size() == 7); // root registry + root git + auth(2 + git) + billing + active space
  assert(value.links.size() == 6);
  assert(value.requirements[0].owner.kind == DependencyOwnerKind::Application);
  bool authJwt = false, billingJson2 = false, analytics = false;
  for (const auto &item : value.requirements) { authJwt |= item.owner.module == "auth" && item.requirement == "gk/jwt@^1"; billingJson2 |= item.owner.module == "billing" && item.requirement == "gk/json@^2"; analytics |= item.owner.module == "analytics"; }
  assert(authJwt && billingJson2 && !analytics); // incompatible-looking requirements remain distinct
  assert(value.gitDependencies.size() == 2 && value.gitDependencies[1].owner.module == "auth");
  const std::string cmake = vix::cli::app::generate_app_cmake_lists_content(app, root);
  assert(cmake.find("set(VIX_MODULE_billing_LINKS\n  gk::billing\n)") != std::string::npos);
  assert(cmake.find("target_link_libraries(demo PRIVATE gk::jwt)") == std::string::npos);
  assert(cmake.find("target_link_libraries(demo PRIVATE gk::fmt)") != std::string::npos);
  assert(cmake.find("set(VIX_MODULE_auth_LINKS\n  gk::json\n  gk::jwt\n  spdlog::spdlog\n)") != std::string::npos);
  AppManifest missing; missing.name = "missing"; missing.appModules = {module("required")};
  std::string error; auto graph = vix::cli::modules::ModuleGraph::from_app_modules(missing.appModules, error); assert(graph.valid()); assert(!vix::cli::modules::build_dependency_ownership(missing, graph, root).success());
  fs::remove_all(root);
}
