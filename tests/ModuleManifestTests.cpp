#include <vix/cli/commands/modules/ModulesContent.hpp>
#include <vix/cli/modules/ModuleManifest.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
static fs::path write(const std::string &text)
{
  const auto path = fs::temp_directory_path() / "vix-module-manifest-test.module";
  std::ofstream(path) << text;
  return path;
}
static void valid(const std::string &text)
{ assert(vix::cli::modules::load_module_manifest(write(text)).success()); }
static void invalid(const std::string &text)
{ assert(!vix::cli::modules::load_module_manifest(write(text)).success()); }
int main()
{
  namespace c = vix::commands::modules_cmd::content;
  valid(c::module_manifest_app_first("simple"));
  valid(c::module_routed_manifest_app_first("backend", "backend"));
  valid(c::module_backend_manifest_app_first("backend"));
  using vix::commands::modules_cmd::WebSocketWorkflow;
  valid(c::module_websocket_manifest_app_first("ws", WebSocketWorkflow::Attached));
  valid(c::module_websocket_manifest_app_first("ws", WebSocketWorkflow::Standalone));
  valid(c::module_websocket_manifest_app_first("ws", WebSocketWorkflow::Bridge));
  valid(c::module_websocket_manifest_app_first("ws", WebSocketWorkflow::Client));
  const auto m = vix::cli::modules::load_module_manifest(write("# c\r\n name = 'a'\r\nkind=\"module\"\r\n[deps]\r\nregistry=[\r\n \"one\",\r\n \"two\"\r\n]\r\nlinks=[]\r\n[tests]\r\nenabled=on\r\n"));
  assert(m.success() && m.manifest.registryDependencies.size() == 2 && m.manifest.testsEnabled);
  const auto git = vix::cli::modules::load_module_manifest(write("name=\"a\"\n[deps]\nregistry=[\"gk/json\"]\n[dependencies.spdlog]\ngit=\"https://github.com/gabime/spdlog\"\ntag=\"v1.15.3\"\ntarget=\"spdlog::spdlog\"\n[dependencies.spdlog.cmake]\nSPDLOG_BUILD_TESTS=false\nSPDLOG_BUILD_EXAMPLE=false\n"));
  assert(git.success() && git.manifest.registryDependencies.size() == 1 && git.manifest.gitDependencies.size() == 1 && git.manifest.gitDependencies[0].cmakeOptions.size() == 2);
  invalid(""); invalid("name = \"bad name\""); invalid("runtime = maybe");
  invalid("[websocket]\nport = 70000"); invalid("[oops"); invalid("[deps]\nregistry = nope");
  invalid("[deps]\nregistry = [\n \"x\""); invalid("name = \"a\"\nname = \"b\"");
  invalid("[dependencies.x]\ntag=\"v\"");
  invalid("[dependencies.x]\ngit=\"u\"\ntag=\"v\"\nbranch=\"main\"");
  invalid("[dependencies.x]\ngit=\"u\"\ntarget=\"bad target\"");
  invalid("[dependencies.x]\ngit=\"u\"\n[dependencies.x.cmake]\nOPT=false\nOPT=true");
  assert(!vix::cli::modules::load_module_manifest("/definitely/missing/vix.module").success());
}
