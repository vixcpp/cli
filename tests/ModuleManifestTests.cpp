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
  invalid(""); invalid("name = \"bad name\""); invalid("runtime = maybe");
  invalid("[websocket]\nport = 70000"); invalid("[oops"); invalid("[deps]\nregistry = nope");
  invalid("[deps]\nregistry = [\n \"x\""); invalid("name = \"a\"\nname = \"b\"");
  assert(!vix::cli::modules::load_module_manifest("/definitely/missing/vix.module").success());
}
