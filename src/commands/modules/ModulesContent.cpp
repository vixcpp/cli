/**
 * @file ModulesContent.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/modules/ModulesContent.hpp>
#include <vix/cli/commands/modules/ModulesUtils.hpp>

#include <sstream>
#include <unordered_set>
#include <cctype>

namespace vix::commands::modules_cmd::content
{

  namespace fs = std::filesystem;
  using namespace vix::commands::modules_cmd::utils;

  // ------------------------------------------------------------------
  // Naming helpers
  // ------------------------------------------------------------------

  std::string normalize_module_id(std::string name)
  {
    name = trim(name);
    std::string out;
    out.reserve(name.size());
    for (char c : name)
      out.push_back(c == '-' ? '_' : c);
    return out;
  }

  std::string module_target_name(const std::string &project, const std::string &module)
  {
    return project + "_" + normalize_module_id(module);
  }

  std::string module_alias_name(const std::string &project, const std::string &module)
  {
    return project + "::" + normalize_module_id(module);
  }

  // ------------------------------------------------------------------
  // Validation
  // ------------------------------------------------------------------

  bool is_valid_module_name(const std::string &name)
  {
    if (name.empty())
      return false;
    for (char c : name)
    {
      const bool ok =
          (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') ||
          (c == '_' || c == '-');
      if (!ok)
        return false;
    }
    return true;
  }

  bool is_reserved_module_name(std::string name)
  {
    name = to_lower(normalize_module_id(name));
    static const std::unordered_set<std::string> reserved = {
        "modules", "module", "src", "source", "include", "cmake", "build", "dist",
        "test", "tests", "example", "examples", "vendor", "third_party", "thirdparty",
        "external", "internal", "internals", "detail", "details", "private", "public",
        "main", "app", "api", "core", "std", "vix", "vixcpp",
        "registry", "deps", "pack", "lock", "install", "add", "remove", "store", "gc",
        "fmt", "spdlog", "boost", "openssl", "zlib", "sqlite", "mysql", "postgres", "curl",
        "asio", "beast"};
    return reserved.find(name) != reserved.end();
  }

  std::string module_class_name(const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);

    std::string out;
    bool upperNext = true;

    for (char c : normalized)
    {
      if (c == '_' || c == '-')
      {
        upperNext = true;
        continue;
      }

      if (upperNext)
      {
        out.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))));
        upperNext = false;
      }
      else
      {
        out.push_back(c);
      }
    }

    if (out.empty())
      out = "Module";

    return out;
  }

  // ------------------------------------------------------------------
  // CMake content generators
  // ------------------------------------------------------------------

  std::string cmake_vix_modules_cmake_app_first()
  {
    std::ostringstream o;

    o << "##\n";
    o << "## Vix Modules (app-first, opt-in)\n";
    o << "##\n";
    o << "## Contract (Go-like):\n";
    o << "## - modules/<m>/include/<m>/...  (public headers)\n";
    o << "## - modules/<m>/src/...          (private impl)\n";
    o << "## - Each module exports <project>::<m> as an ALIAS target\n";
    o << "## - Public headers must never include private sources (src/)\n";
    o << "## - Cross-module usage must be explicit via target_link_libraries\n";
    o << "## - vix.app projects load only VIX_ENABLED_MODULES\n";
    o << "##\n\n";

    o << "if(DEFINED VIX_MODULES_INCLUDED)\n";
    o << "  return()\n";
    o << "endif()\n";
    o << "set(VIX_MODULES_INCLUDED ON)\n\n";

    o << "set(VIX_MODULES_PROJECT_DIR \"${CMAKE_CURRENT_LIST_DIR}/..\")\n";
    o << "set(VIX_MODULES_DIR \"${VIX_MODULES_PROJECT_DIR}/modules\")\n\n";

    o << "##\n";
    o << "## vix.app mode\n";
    o << "##\n";
    o << "## When VIX_ENABLED_MODULES is defined by the generated CMakeLists.txt,\n";
    o << "## only those modules are loaded. A module folder can exist without\n";
    o << "## being active.\n";
    o << "##\n";
    o << "if(DEFINED VIX_ENABLED_MODULES)\n";
    o << "  foreach(_m ${VIX_ENABLED_MODULES})\n";
    o << "    string(REPLACE \"-\" \"_\" _m_norm \"${_m}\")\n";
    o << "    set(_m_path_var \"VIX_MODULE_${_m_norm}_PATH\")\n\n";

    o << "    if(DEFINED ${_m_path_var})\n";
    o << "      set(_m_path \"${${_m_path_var}}\")\n";
    o << "      if(IS_ABSOLUTE \"${_m_path}\")\n";
    o << "        set(_m_dir \"${_m_path}\")\n";
    o << "      else()\n";
    o << "        set(_m_dir \"${VIX_MODULES_PROJECT_DIR}/${_m_path}\")\n";
    o << "      endif()\n";
    o << "    else()\n";
    o << "      set(_m_dir \"${VIX_MODULES_DIR}/${_m_norm}\")\n";
    o << "    endif()\n\n";

    o << "    if(NOT EXISTS \"${_m_dir}\")\n";
    o << "      message(FATAL_ERROR \"VIX_MODULE_NOT_FOUND module=${_m_norm} path=${_m_dir}\")\n";
    o << "    endif()\n\n";

    o << "    if(NOT EXISTS \"${_m_dir}/CMakeLists.txt\")\n";
    o << "      message(FATAL_ERROR \"VIX_MODULE_CMAKELISTS_NOT_FOUND module=${_m_norm} path=${_m_dir}/CMakeLists.txt\")\n";
    o << "    endif()\n\n";

    o << "    add_subdirectory(\"${_m_dir}\" \"${CMAKE_BINARY_DIR}/vix_modules/${_m_norm}\")\n";
    o << "  endforeach()\n";
    o << "  return()\n";
    o << "endif()\n\n";

    o << "##\n";
    o << "## Legacy CMake mode\n";
    o << "##\n";
    o << "## If VIX_ENABLED_MODULES is not defined, keep the old behavior for\n";
    o << "## classic CMake projects: load every module folder under modules/*.\n";
    o << "##\n";
    o << "if(NOT EXISTS \"${VIX_MODULES_DIR}\")\n";
    o << "  return()\n";
    o << "endif()\n\n";

    o << "file(GLOB VIX_MODULE_DIRS RELATIVE \"${VIX_MODULES_DIR}\" \"${VIX_MODULES_DIR}/*\")\n";
    o << "foreach(_m ${VIX_MODULE_DIRS})\n";
    o << "  if(IS_DIRECTORY \"${VIX_MODULES_DIR}/${_m}\")\n";
    o << "    if(EXISTS \"${VIX_MODULES_DIR}/${_m}/CMakeLists.txt\")\n";
    o << "      add_subdirectory(\"${VIX_MODULES_DIR}/${_m}\" \"${CMAKE_BINARY_DIR}/vix_modules/${_m}\")\n";
    o << "    endif()\n";
    o << "  endif()\n";
    o << "endforeach()\n";

    return o.str();
  }

  std::string module_cmakelists_txt_app_first(const std::string &project, const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string target = module_target_name(project, module);
    const std::string alias = module_alias_name(project, module);

    std::ostringstream o;

    o << "cmake_minimum_required(VERSION 3.16)\n\n";
    o << "add_library(" << target << ")\n";
    o << "add_library(" << alias << " ALIAS " << target << ")\n\n";

    o << "target_sources(" << target << "\n";
    o << "  PRIVATE\n";
    o << "    src/" << normalized << ".cpp\n";
    o << ")\n\n";

    o << "target_include_directories(" << target << "\n";
    o << "  PUBLIC\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/include\n";
    o << "  PRIVATE\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/src\n";
    o << ")\n\n";

    o << "target_compile_features(" << target << " PUBLIC cxx_std_20)\n\n";

    o << "set_target_properties(" << target << " PROPERTIES\n";
    o << "  OUTPUT_NAME \"" << target << "\"\n";
    o << ")\n";

    return o.str();
  }

  std::string module_backend_cmakelists_txt_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string target = module_target_name(project, module);
    const std::string alias = module_alias_name(project, module);

    std::ostringstream o;

    o << "cmake_minimum_required(VERSION 3.16)\n\n";

    o << "file(GLOB_RECURSE " << target << "_SOURCES CONFIGURE_DEPENDS\n";
    o << "  ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp\n";
    o << ")\n\n";

    o << "add_library(" << target << " ${" << target << "_SOURCES})\n";
    o << "add_library(" << alias << " ALIAS " << target << ")\n\n";

    o << "target_include_directories(" << target << "\n";
    o << "  PUBLIC\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/include\n";
    o << "  PRIVATE\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/src\n";
    o << ")\n\n";

    o << "target_compile_features(" << target << " PUBLIC cxx_std_20)\n\n";

    o << "target_link_libraries(" << target << "\n";
    o << "  PUBLIC\n";
    o << "    vix::vix\n";
    o << ")\n\n";

    o << "if(DEFINED VIX_MODULE_" << normalized << "_LINKS)\n";
    o << "  target_link_libraries(" << target << "\n";
    o << "    PUBLIC\n";
    o << "      ${VIX_MODULE_" << normalized << "_LINKS}\n";
    o << "  )\n";
    o << "endif()\n\n";

    o << "if(DEFINED " << project << "_BUILD_TESTS AND " << project << "_BUILD_TESTS)\n";
    o << "  file(GLOB_RECURSE " << target << "_TEST_SOURCES CONFIGURE_DEPENDS\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp\n";
    o << "  )\n\n";

    o << "  if(" << target << "_TEST_SOURCES)\n";
    o << "    add_executable(" << target << "_tests\n";
    o << "      ${" << target << "_TEST_SOURCES}\n";
    o << "    )\n\n";

    o << "    target_include_directories(" << target << "_tests PRIVATE\n";
    o << "      ${CMAKE_CURRENT_LIST_DIR}/include\n";
    o << "      ${CMAKE_CURRENT_LIST_DIR}/src\n";
    o << "    )\n\n";

    o << "    target_link_libraries(" << target << "_tests PRIVATE\n";
    o << "      " << target << "\n";
    o << "      vix::vix\n";
    o << "    )\n\n";

    o << "    target_compile_features(" << target << "_tests PRIVATE cxx_std_20)\n\n";
    o << "    add_test(NAME " << project << "." << normalized << " COMMAND " << target << "_tests)\n";
    o << "  endif()\n";
    o << "endif()\n\n";

    o << "set_target_properties(" << target << " PROPERTIES\n";
    o << "  OUTPUT_NAME \"" << target << "\"\n";
    o << ")\n";

    return o.str();
  }

  std::string module_websocket_cmakelists_txt_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string target = module_target_name(project, module);
    const std::string alias = module_alias_name(project, module);

    std::ostringstream o;

    o << "cmake_minimum_required(VERSION 3.16)\n\n";

    o << "file(GLOB_RECURSE " << target << "_SOURCES CONFIGURE_DEPENDS\n";
    o << "  ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp\n";
    o << ")\n\n";

    o << "add_library(" << target << " ${" << target << "_SOURCES})\n";
    o << "add_library(" << alias << " ALIAS " << target << ")\n\n";

    o << "target_include_directories(" << target << "\n";
    o << "  PUBLIC\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/include\n";
    o << "  PRIVATE\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/src\n";
    o << ")\n\n";

    o << "target_compile_features(" << target << " PUBLIC cxx_std_20)\n\n";

    o << "target_link_libraries(" << target << "\n";
    o << "  PUBLIC\n";
    o << "    vix::vix\n";
    o << "    vix::websocket\n";
    o << ")\n\n";

    o << "if(DEFINED " << project << "_BUILD_TESTS AND " << project << "_BUILD_TESTS)\n";
    o << "  file(GLOB_RECURSE " << target << "_TEST_SOURCES CONFIGURE_DEPENDS\n";
    o << "    ${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp\n";
    o << "  )\n\n";

    o << "  if(" << target << "_TEST_SOURCES)\n";
    o << "    add_executable(" << target << "_tests\n";
    o << "      ${" << target << "_TEST_SOURCES}\n";
    o << "    )\n\n";

    o << "    target_include_directories(" << target << "_tests PRIVATE\n";
    o << "      ${CMAKE_CURRENT_LIST_DIR}/include\n";
    o << "      ${CMAKE_CURRENT_LIST_DIR}/src\n";
    o << "    )\n\n";

    o << "    target_link_libraries(" << target << "_tests PRIVATE\n";
    o << "      " << target << "\n";
    o << "      vix::vix\n";
    o << "      vix::websocket\n";
    o << "    )\n\n";

    o << "    target_compile_features(" << target << "_tests PRIVATE cxx_std_20)\n\n";
    o << "    add_test(NAME " << project << "." << normalized << " COMMAND " << target << "_tests)\n";
    o << "  endif()\n";
    o << "endif()\n\n";

    o << "set_target_properties(" << target << " PROPERTIES\n";
    o << "  OUTPUT_NAME \"" << target << "\"\n";
    o << ")\n";

    return o.str();
  }

  std::string module_routed_manifest_app_first(
      const std::string &module,
      const std::string &kind)
  {
    const std::string normalized = normalize_module_id(module);

    std::ostringstream o;

    o << "name = \"" << normalized << "\"\n";
    o << "kind = \"" << kind << "\"\n\n";

    o << "[routes]\n";
    o << "prefix = \"/api/" << normalized << "\"\n\n";

    o << "[deps]\n";
    o << "registry = [\n";
    o << "]\n\n";

    o << "links = [\n";
    o << "]\n\n";

    o << "[tests]\n";
    o << "enabled = true\n";

    return o.str();
  }

  std::string module_backend_manifest_app_first(
      const std::string &module)
  {
    return module_routed_manifest_app_first(module, "backend");
  }

  std::string module_manifest_app_first(
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);

    std::ostringstream o;

    o << "name = \"" << normalized << "\"\n";
    o << "kind = \"module\"\n\n";

    o << "[exports]\n";
    o << "include = \"include\"\n\n";

    o << "[tests]\n";
    o << "enabled = true\n";

    return o.str();
  }

  std::string module_backend_header_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);
    const std::string guard =
        to_lower(normalize_module_id(project)) + "_" +
        to_lower(normalized) + "_module_hpp";

    std::ostringstream o;

    o << "#ifndef " << guard << "\n";
    o << "#define " << guard << "\n\n";

    o << "namespace vix\n";
    o << "{\n";
    o << "  class App;\n";
    o << "}\n\n";

    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";
    o << "  class " << classBase << "Module\n";
    o << "  {\n";
    o << "  public:\n";
    o << "    static const char *name();\n";
    o << "    static void register_routes(vix::App &app);\n";
    o << "  };\n";
    o << "} // namespace " << project << "::" << normalized << "\n\n";

    o << "#endif // " << guard << "\n";

    return o.str();
  }

  std::string module_backend_impl_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);

    std::ostringstream o;

    o << "#include <" << normalized << "/" << classBase << "Module.hpp>\n";
    o << "#include <" << normalized << "/controllers/" << classBase << "Controller.hpp>\n\n";

    o << "#include <vix.hpp>\n\n";

    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";
    o << "  const char *" << classBase << "Module::name()\n";
    o << "  {\n";
    o << "    return \"" << normalized << "\";\n";
    o << "  }\n\n";

    o << "  void " << classBase << "Module::register_routes(vix::App &app)\n";
    o << "  {\n";
    o << "    controllers::" << classBase << "Controller::register_routes(app);\n";
    o << "  }\n";
    o << "} // namespace " << project << "::" << normalized << "\n";

    return o.str();
  }

  std::string module_backend_controller_header_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);
    const std::string guard =
        to_lower(normalize_module_id(project)) + "_" +
        to_lower(normalized) + "_controller_hpp";

    std::ostringstream o;

    o << "#ifndef " << guard << "\n";
    o << "#define " << guard << "\n\n";

    o << "namespace vix\n";
    o << "{\n";
    o << "  class App;\n";
    o << "}\n\n";

    o << "namespace " << project << "::" << normalized << "::controllers\n";
    o << "{\n";
    o << "  class " << classBase << "Controller\n";
    o << "  {\n";
    o << "  public:\n";
    o << "    static void register_routes(vix::App &app);\n";
    o << "  };\n";
    o << "} // namespace " << project << "::" << normalized << "::controllers\n\n";

    o << "#endif // " << guard << "\n";

    return o.str();
  }

  std::string module_backend_controller_impl_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);

    std::ostringstream o;

    o << "#include <" << normalized << "/controllers/" << classBase << "Controller.hpp>\n\n";
    o << "#include <vix.hpp>\n\n";

    o << "namespace " << project << "::" << normalized << "::controllers\n";
    o << "{\n";
    o << "  void " << classBase << "Controller::register_routes(vix::App &app)\n";
    o << "  {\n";
    o << "    app.get(\"/api/" << normalized << "\", [](vix::Request &req, vix::Response &res)\n";
    o << "    {\n";
    o << "      (void)req;\n\n";
    o << "      res.json({\n";
    o << "        \"ok\", true,\n";
    o << "        \"module\", \"" << normalized << "\",\n";
    o << "        \"message\", \"" << classBase << " module is available\"\n";
    o << "      });\n";
    o << "    });\n";
    o << "  }\n";
    o << "} // namespace " << project << "::" << normalized << "::controllers\n";

    return o.str();
  }

  // ------------------------------------------------------------------
  // WebSocket module content generators
  // ------------------------------------------------------------------

  std::string module_websocket_manifest_app_first(
      const std::string &module,
      WebSocketWorkflow workflow)
  {
    const std::string normalized = normalize_module_id(module);

    std::ostringstream o;

    o << "name = \"" << normalized << "\"\n";
    o << "kind = \"backend\"\n";
    o << "workflow = \"" << websocket_manifest_workflow_name(workflow) << "\"\n";
    o << "runtime = true\n\n";

    o << "[routes]\n";
    o << "prefix = \"/ws\"\n\n";

    o << "[websocket]\n";
    o << "workflow = \"" << websocket_workflow_name(workflow) << "\"\n";
    o << "path = \"/ws\"\n";
    o << "host = \"0.0.0.0\"\n";
    o << "port = 9090\n";
    o << "long_polling = ";
    o << (workflow == WebSocketWorkflow::Bridge ? "true" : "false");
    o << "\n";
    o << "metrics = false\n\n";

    o << "[deps]\n";
    o << "registry = [\n";
    o << "]\n\n";

    o << "links = [\n";
    o << "]\n\n";

    o << "[tests]\n";
    o << "enabled = true\n";

    return o.str();
  }

  std::string module_websocket_header_app_first(
      const std::string &project,
      const std::string &module,
      WebSocketWorkflow workflow)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);
    const std::string guard =
        to_lower(normalize_module_id(project)) + "_" +
        to_lower(normalized) + "_websocket_module_hpp";

    std::ostringstream o;

    o << "#ifndef " << guard << "\n";
    o << "#define " << guard << "\n\n";

    o << "#include <memory>\n\n";

    o << "namespace vix\n";
    o << "{\n";
    o << "  class App;\n";
    o << "}\n\n";

    o << "namespace vix::config\n";
    o << "{\n";
    o << "  class Config;\n";
    o << "}\n\n";

    o << "namespace vix::executor\n";
    o << "{\n";
    o << "  class RuntimeExecutor;\n";
    o << "}\n\n";

    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";

    o << "  /**\n";
    o << "   * @brief Application module generated for WebSocket runtime wiring.\n";
    o << "   *\n";
    o << "   * This module keeps HTTP route registration separate from runtime startup.\n";
    o << "   * `register_routes()` is safe to call during application configuration.\n";
    o << "   * Runtime startup belongs to `run()` for workflows that need a long-lived\n";
    o << "   * WebSocket server.\n";
    o << "   */\n";
    o << "  class " << classBase << "Module\n";
    o << "  {\n";
    o << "  public:\n";

    o << "    /**\n";
    o << "     * @brief Return the stable module name.\n";
    o << "     *\n";
    o << "     * @return Module name used in generated manifests and diagnostics.\n";
    o << "     */\n";
    o << "    static const char *name();\n\n";

    o << "    /**\n";
    o << "     * @brief Register HTTP-facing routes for the module.\n";
    o << "     *\n";
    o << "     * This function must remain lightweight. It must not start a WebSocket\n";
    o << "     * listener, block the process, or own runtime shutdown.\n";
    o << "     *\n";
    o << "     * @param app Application receiving the generated routes.\n";
    o << "     */\n";
    o << "    static void register_routes(vix::App &app);\n\n";

    if (workflow != WebSocketWorkflow::Client)
    {
      o << "    /**\n";
      o << "     * @brief Run the selected WebSocket workflow.\n";
      o << "     *\n";
      o << "     * Runtime workflows are started explicitly from the generated application\n";
      o << "     * runner. This avoids hiding long-lived WebSocket services inside normal\n";
      o << "     * route registration.\n";
      o << "     *\n";
      o << "     * @param app HTTP application instance.\n";
      o << "     * @param cfg Application configuration.\n";
      o << "     * @param executor Shared runtime executor.\n";
      o << "     * @return Process exit code.\n";
      o << "     */\n";
      o << "    static int run(\n";
      o << "        vix::App &app,\n";
      o << "        const vix::config::Config &cfg,\n";
      o << "        std::shared_ptr<vix::executor::RuntimeExecutor> executor);\n";
    }

    o << "  };\n";
    o << "} // namespace " << project << "::" << normalized << "\n\n";

    o << "#endif // " << guard << "\n";

    return o.str();
  }

  std::string module_websocket_impl_app_first(
      const std::string &project,
      const std::string &module,
      WebSocketWorkflow workflow)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);

    std::ostringstream o;

    o << "#include <" << normalized << "/" << classBase << "Module.hpp>\n\n";
    o << "#include <vix.hpp>\n";
    o << "#include <vix/websocket/AttachedRuntime.hpp>\n\n";

    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";

    o << "  const char *" << classBase << "Module::name()\n";
    o << "  {\n";
    o << "    return \"" << normalized << "\";\n";
    o << "  }\n\n";

    o << "  void " << classBase << "Module::register_routes(vix::App &app)\n";
    o << "  {\n";
    o << "    app.get(\"/ws/status\", [](vix::Request &req, vix::Response &res)\n";
    o << "    {\n";
    o << "      (void)req;\n\n";
    o << "      res.json({\n";
    o << "        \"ok\", true,\n";
    o << "        \"module\", \"" << normalized << "\",\n";
    o << "        \"workflow\", \"" << websocket_workflow_name(workflow) << "\",\n";
    o << "        \"message\", \"" << classBase << " WebSocket module is available\"\n";
    o << "      });\n";
    o << "    });\n";
    o << "  }\n";

    if (workflow != WebSocketWorkflow::Client)
    {
      o << "\n";
      o << "  int " << classBase << "Module::run(\n";
      o << "      vix::App &app,\n";
      o << "      const vix::config::Config &cfg,\n";
      o << "      std::shared_ptr<vix::executor::RuntimeExecutor> executor)\n";
      o << "  {\n";
      o << "    vix::websocket::Server ws{\n";
      o << "        const_cast<vix::config::Config &>(cfg),\n";
      o << "        executor};\n\n";
      o << "    vix::run_http_and_ws(app, ws, executor, cfg);\n";
      o << "    return 0;\n";
      o << "  }\n";
    }

    o << "} // namespace " << project << "::" << normalized << "\n";

    return o.str();
  }

  std::string module_websocket_test_cpp_app_first(
      const std::string &project,
      const std::string &module,
      WebSocketWorkflow workflow)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);

    std::ostringstream o;

    o << "/**\n";
    o << " * @file test_" << normalized << ".cpp\n";
    o << " * @brief Basic tests for the " << normalized << " WebSocket module.\n";
    o << " */\n\n";

    o << "#include <" << normalized << "/" << classBase << "Module.hpp>\n\n";
    o << "#include <vix/tests/tests.hpp>\n\n";
    o << "#include <string>\n\n";

    o << "int main()\n";
    o << "{\n";
    o << "  using namespace vix::tests;\n\n";

    o << "  auto &registry = TestRegistry::instance();\n";
    o << "  registry.clear();\n\n";

    o << "  registry.add(TestCase(\"" << normalized << " websocket module exposes its name\", []\n";
    o << "  {\n";
    o << "    Assert::equal(\n";
    o << "        std::string(" << project << "::" << normalized << "::" << classBase << "Module::name()),\n";
    o << "        std::string(\"" << normalized << "\"));\n";
    o << "  }));\n\n";

    o << "  registry.add(TestCase(\"" << normalized << " websocket workflow is generated\", []\n";
    o << "  {\n";
    o << "    Assert::equal(\n";
    o << "        std::string(\"" << websocket_workflow_name(workflow) << "\"),\n";
    o << "        std::string(\"" << websocket_workflow_name(workflow) << "\"));\n";
    o << "  }));\n\n";

    o << "  return TestRunner::run_all_and_exit();\n";
    o << "}\n";

    return o.str();
  }

  std::string module_backend_test_cpp_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string classBase = module_class_name(module);

    std::ostringstream o;

    o << "/**\n";
    o << " * @file test_" << normalized << ".cpp\n";
    o << " * @brief Basic tests for the " << normalized << " backend module.\n";
    o << " */\n\n";

    o << "#include <" << normalized << "/" << classBase << "Module.hpp>\n\n";
    o << "#include <vix/tests/tests.hpp>\n\n";

    o << "int main()\n";
    o << "{\n";
    o << "  using namespace vix::tests;\n\n";

    o << "  auto &registry = TestRegistry::instance();\n";
    o << "  registry.clear();\n\n";

    o << "  registry.add(TestCase(\"" << normalized << " module exposes its name\", []\n";
    o << "  {\n";
    o << "    Assert::equal(\n";
    o << "        std::string(" << project << "::" << normalized << "::" << classBase << "Module::name()),\n";
    o << "        std::string(\"" << normalized << "\"));\n";
    o << "  }));\n\n";

    o << "  return TestRunner::run_all_and_exit();\n";
    o << "}\n";

    return o.str();
  }

  std::string module_test_cpp_app_first(
      const std::string &project,
      const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);

    std::ostringstream o;

    o << "/**\n";
    o << " * @file test_" << normalized << ".cpp\n";
    o << " * @brief Basic tests for the " << normalized << " module.\n";
    o << " */\n\n";

    o << "#include <" << normalized << "/api.hpp>\n\n";
    o << "#include <vix/tests/tests.hpp>\n\n";

    o << "int main()\n";
    o << "{\n";
    o << "  using namespace vix::tests;\n\n";

    o << "  auto &registry = TestRegistry::instance();\n";
    o << "  registry.clear();\n\n";

    o << "  registry.add(TestCase(\"" << normalized << " module exposes its API name\", []\n";
    o << "  {\n";
    o << "    Assert::equal(\n";
    o << "        " << project << "::" << normalized << "::Api::name(),\n";
    o << "        std::string(\"" << project << "::" << normalized << "\"));\n";
    o << "  }));\n\n";

    o << "  return TestRunner::run_all_and_exit();\n";
    o << "}\n";

    return o.str();
  }

  // ------------------------------------------------------------------
  // C++ content generators
  // ------------------------------------------------------------------

  std::string module_public_header_app_first(const std::string &project, const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    const std::string guard =
        to_lower(normalize_module_id(project)) + "_" + to_lower(normalized) + "_api_hpp";

    std::ostringstream o;
    o << "#ifndef " << guard << "\n";
    o << "#define " << guard << "\n\n";
    o << "#include <string>\n\n";
    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";
    o << "  struct Api\n";
    o << "  {\n";
    o << "    static std::string name();\n";
    o << "  };\n";
    o << "}\n\n";
    o << "#endif\n";
    return o.str();
  }

  std::string module_impl_cpp_app_first(const std::string &project, const std::string &module)
  {
    const std::string normalized = normalize_module_id(module);
    std::ostringstream o;
    o << "#include <" << normalized << "/api.hpp>\n\n";
    o << "namespace " << project << "::" << normalized << "\n";
    o << "{\n";
    o << "  std::string Api::name()\n";
    o << "  {\n";
    o << "    return \"" << project << "::" << normalized << "\";\n";
    o << "  }\n";
    o << "}\n";
    return o.str();
  }

  // ------------------------------------------------------------------
  // CMakeLists.txt patching
  // ------------------------------------------------------------------

  bool patch_root_cmakelists_include(const fs::path &root)
  {
    const fs::path cm = root / "CMakeLists.txt";
    auto contentOpt = read_file(cm);
    if (!contentOpt)
      return false;

    std::string content = *contentOpt;

    const std::string beginMark = "# VIX_MODULES_BEGIN";
    const std::string endMark = "# VIX_MODULES_END";
    const std::string block =
        beginMark + "\n"
                    "include(${CMAKE_CURRENT_LIST_DIR}/cmake/vix_modules.cmake)\n" +
        endMark + "\n";

    // Already patched — idempotent
    if (content.find(beginMark) != std::string::npos &&
        content.find(endMark) != std::string::npos)
      return true;

    std::istringstream in(content);
    std::ostringstream out;
    std::string line;
    bool inserted = false;

    while (std::getline(in, line))
    {
      out << line << "\n";
      if (!inserted && starts_with(to_lower(trim(line)), "project("))
      {
        out << "\n"
            << block << "\n";
        inserted = true;
      }
    }

    if (!inserted)
    {
      // No project() found — prepend
      content = block + "\n" + content;
    }
    else
    {
      content = out.str();
    }

    return write_file_overwrite(cm, content);
  }

  bool patch_root_cmakelists_link_module(
      const fs::path &root,
      const std::string &project,
      const std::string &module)
  {
    const fs::path cm = root / "CMakeLists.txt";
    auto contentOpt = read_file(cm);
    if (!contentOpt)
      return false;

    std::string content = *contentOpt;

    const std::string beginMark = "# VIX_MODULE_LINKS_BEGIN";
    const std::string endMark = "# VIX_MODULE_LINKS_END";

    // Ensure the links section exists
    if (content.find(beginMark) == std::string::npos ||
        content.find(endMark) == std::string::npos)
    {
      std::ostringstream section;
      section << "\n"
              << beginMark << "\n"
              << "# Auto-generated by: vix modules\n"
              << "# NOTE: This links modules into the main target named like project(" << project << ").\n"
              << "#       If your main target differs, remove this section and link targets manually.\n"
              << endMark << "\n";
      content += section.str();
    }

    const std::string alias = module_alias_name(project, module);
    const std::string modNorm = normalize_module_id(module);
    const std::string perBegin = "# VIX_MODULE_LINK_BEGIN " + modNorm;
    const std::string perEnd = "# VIX_MODULE_LINK_END " + modNorm;

    // Already patched for this module — idempotent
    if (content.find(perBegin) != std::string::npos &&
        content.find(perEnd) != std::string::npos)
      return true;

    std::ostringstream block;
    block << perBegin << "\n"
          << "if (TARGET " << alias << ")\n"
          << "  if (TARGET " << project << ")\n"
          << "    target_link_libraries(" << project << " PRIVATE " << alias << ")\n"
          << "  endif()\n"
          << "endif()\n"
          << perEnd << "\n";

    const size_t endPos = content.find(endMark);
    if (endPos == std::string::npos)
      return false;

    content.insert(endPos, block.str());
    return write_file_overwrite(cm, content);
  }

  // ------------------------------------------------------------------
  // Static analysis helpers
  // ------------------------------------------------------------------

  std::unordered_set<std::string> parse_declared_deps_from_module_cmake(
      const fs::path &moduleCmake,
      const std::string &project)
  {
    std::unordered_set<std::string> deps;

    auto content = read_file(moduleCmake);
    if (!content)
      return deps;

    const std::string &s = *content;
    const std::string needle = project + "::";
    size_t pos = 0;

    while ((pos = s.find(needle, pos)) != std::string::npos)
    {
      size_t start = pos + needle.size();
      size_t end = start;
      while (end < s.size())
      {
        char c = s[end];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
          break;
        ++end;
      }
      if (end > start)
        deps.insert(s.substr(start, end - start));
      pos = end;
    }

    return deps;
  }

  std::set<std::string> parse_public_includes_for_cross_module(
      const fs::path &publicHeader,
      const fs::path &modulesDir)
  {
    std::set<std::string> used;

    auto content = read_file(publicHeader);
    if (!content)
      return used;

    std::istringstream in(*content);
    std::string line;
    while (std::getline(in, line))
    {
      std::string s = trim(line);
      if (!starts_with(s, "#include"))
        continue;

      auto lt = s.find('<');
      auto gt = s.find('>', lt == std::string::npos ? 0 : lt + 1);
      if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1)
        continue;

      const std::string inside = s.substr(lt + 1, gt - (lt + 1));
      auto slash = inside.find('/');
      if (slash == std::string::npos)
        continue;

      const std::string first = inside.substr(0, slash);
      if (!first.empty() && exists_dir(modulesDir / first))
        used.insert(first);
    }

    return used;
  }

  bool header_includes_private_impl(
      const fs::path &publicHeader,
      const fs::path &moduleDir)
  {
    auto content = read_file(publicHeader);
    if (!content)
      return false;

    const std::string &s = *content;
    const std::string mod = moduleDir.filename().string();

    return s.find("\"src/") != std::string::npos ||
           s.find("../src/") != std::string::npos ||
           s.find("/src/") != std::string::npos ||
           s.find("modules/" + mod + "/src/") != std::string::npos;
  }

} // namespace vix::commands::modules_cmd::content
