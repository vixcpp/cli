/**
 * @file BackendBootstrapTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendBootstrapTemplates.hpp>
#include <vix/cli/commands/new/templates/backend/BackendTemplateUtils.hpp>
#include <algorithm>
#include <cctype>
#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_main_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(900);

    s += "/**\n";
    s += " * @file main.cpp\n";
    s += " * @brief Entry point for the " + projectName + " backend application.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/app/AppBootstrap.hpp>\n\n";

    s += "/**\n";
    s += " * @brief Start the backend application.\n";
    s += " *\n";
    s += " * The main function stays intentionally small. Application setup,\n";
    s += " * middleware registration, route registration, and server startup are\n";
    s += " * delegated to " + projectName + "::app::AppBootstrap.\n";
    s += " *\n";
    s += " * @return Process exit code.\n";
    s += " */\n";
    s += "int main()\n";
    s += "{\n";
    s += "  " + projectName + "::app::AppBootstrap bootstrap;\n";
    s += "  return bootstrap.run();\n";
    s += "}\n";

    return s;
  }

  std::string make_backend_app_bootstrap_hpp(const std::string &projectName)
  {
    const std::string guard = make_backend_header_guard(projectName, "APP_BOOTSTRAP");

    std::string s;
    s.reserve(1600);

    s += "/**\n";
    s += " * @file AppBootstrap.hpp\n";
    s += " * @brief Application bootstrap for the " + projectName + " backend.\n";
    s += " */\n\n";

    s += "#ifndef " + guard + "\n";
    s += "#define " + guard + "\n\n";

    s += "namespace " + projectName + "::app\n";
    s += "{\n";
    s += "  /**\n";
    s += "   * @brief Owns the startup sequence of the backend application.\n";
    s += "   *\n";
    s += "   * AppBootstrap keeps main.cpp minimal and centralizes the application\n";
    s += "   * initialization flow: configuration loading, Vix app creation,\n";
    s += "   * template/static directory setup, middleware registration, route\n";
    s += "   * registration, and server startup.\n";
    s += "   */\n";
    s += "  class AppBootstrap\n";
    s += "  {\n";
    s += "  public:\n";
    s += "    /**\n";
    s += "     * @brief Create a default application bootstrap instance.\n";
    s += "     */\n";
    s += "    AppBootstrap() = default;\n\n";

    s += "    /**\n";
    s += "     * @brief Destroy the application bootstrap instance.\n";
    s += "     */\n";
    s += "    ~AppBootstrap() = default;\n\n";

    s += "    AppBootstrap(const AppBootstrap &) = delete;\n";
    s += "    AppBootstrap &operator=(const AppBootstrap &) = delete;\n";
    s += "    AppBootstrap(AppBootstrap &&) = delete;\n";
    s += "    AppBootstrap &operator=(AppBootstrap &&) = delete;\n\n";

    s += "    /**\n";
    s += "     * @brief Run the backend application.\n";
    s += "     *\n";
    s += "     * @return Process exit code. Returns 0 when the application exits normally.\n";
    s += "     */\n";
    s += "    int run();\n";
    s += "  };\n";
    s += "} // namespace " + projectName + "::app\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_backend_app_bootstrap_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(3200);

    s += "/**\n";
    s += " * @file AppBootstrap.cpp\n";
    s += " * @brief Startup implementation for the " + projectName + " backend.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/app/AppBootstrap.hpp>\n";
    s += "#include <" + projectName + "/presentation/middleware/MiddlewareRegistry.hpp>\n";
    s += "#include <" + projectName + "/presentation/routes/RouteRegistry.hpp>\n\n";

    s += "#include <vix.hpp>\n";
    s += "#include <vix/log.hpp>\n";
    s += "#include <vix/middleware/app/adapter.hpp>\n";
    s += "#include <vix/middleware/performance/compression.hpp>\n";
    s += "#include <vix/middleware/performance/static_compression.hpp>\n\n";

    s += "namespace " + projectName + "::app\n";
    s += "{\n";
    s += "  int AppBootstrap::run()\n";
    s += "  {\n";
    s += "    vix::config::Config cfg{\".env\"};\n";
    s += "    vix::App app;\n\n";

    s += "    const std::string viewsPath = cfg.getString(\"templates.path\", \"views\");\n";
    s += "    const std::string publicPath = cfg.getString(\"public.path\", \"public\");\n";
    s += "    const std::string publicMount = cfg.getString(\"public.mount\", \"/\");\n";
    s += "    const std::string publicIndex = cfg.getString(\"public.index\", \"index.html\");\n";
    s += "    const std::string publicCacheControl = cfg.getString(\"public.cache_control\", \"public, max-age=3600\");\n";
    s += "    const bool publicSpaFallback = cfg.getBool(\"public.spa_fallback\", false);\n";
    s += "    const bool publicCompression = cfg.getBool(\"public.compression\", false);\n";
    s += "    const int publicCompressionMinSize = cfg.getInt(\"public.compression_min_size\", 1024);\n\n";

    s += "    if (publicCompression)\n";
    s += "    {\n";
    s += "      const auto compressionOptions = vix::middleware::performance::CompressionOptions{\n";
    s += "          .min_size = static_cast<std::size_t>(publicCompressionMinSize),\n";
    s += "          .add_vary = true,\n";
    s += "          .enabled = true,\n";
    s += "      };\n\n";

    s += "      auto compressionMiddleware = vix::middleware::app::adapt_ctx(\n";
    s += "          vix::middleware::performance::compression(compressionOptions));\n\n";

    s += "      app.use(std::move(compressionMiddleware));\n\n";

    s += "     vix::App::set_static_response_hook(\n";
    s += "          vix::middleware::performance::compressed_static_response_hook(compressionOptions));\n";
    s += "    }\n\n";

    s += "    app.templates(viewsPath);\n";
    s += "    app.static_dir(\n";
    s += "        publicPath,\n";
    s += "        publicMount,\n";
    s += "        publicIndex,\n";
    s += "        true,\n";
    s += "        publicCacheControl,\n";
    s += "        true,\n";
    s += "        publicSpaFallback);\n\n";

    s += "    presentation::middleware::MiddlewareRegistry::register_all(app);\n";
    s += "    presentation::routes::RouteRegistry::register_all(app);\n\n";

    s += "    vix::log::info(\"Starting " + projectName + " on port {}\", cfg.getServerPort());\n\n";

    s += "    app.run(cfg);\n";
    s += "    return 0;\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::app\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
