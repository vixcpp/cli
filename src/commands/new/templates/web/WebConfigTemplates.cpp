/**
 * @file WebConfigTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/web/WebConfigTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_production_config_json(const std::string &projectName)
  {
    std::string s;
    s.reserve(2600);

    s += "{\n";
    s += "  \"app\": {\n";
    s += "    \"name\": \"" + projectName + "\",\n";
    s += "    \"env\": \"production\",\n";
    s += "    \"template\": \"web\"\n";
    s += "  },\n";
    s += "  \"server\": {\n";
    s += "    \"host\": \"0.0.0.0\",\n";
    s += "    \"port\": 8080,\n";
    s += "    \"request_timeout\": 5000,\n";
    s += "    \"io_threads\": 0,\n";
    s += "    \"session_timeout_sec\": 20,\n";
    s += "    \"tls_enabled\": false\n";
    s += "  },\n";
    s += "  \"logging\": {\n";
    s += "    \"level\": \"info\",\n";
    s += "    \"format\": \"kv\",\n";
    s += "    \"async\": true,\n";
    s += "    \"queue_max\": 20000,\n";
    s += "    \"drop_on_overflow\": true\n";
    s += "  },\n";
    s += "  \"public\": {\n";
    s += "    \"path\": \"public\",\n";
    s += "    \"mount\": \"/\",\n";
    s += "    \"spa_fallback\": false\n";
    s += "  },\n";
    s += "  \"templates\": {\n";
    s += "    \"path\": \"views\",\n";
    s += "    \"auto_escape_html\": true,\n";
    s += "    \"cache\": true\n";
    s += "  },\n";
    s += "  \"storage\": {\n";
    s += "    \"path\": \"storage\"\n";
    s += "  },\n";
    s += "  \"health\": {\n";
    s += "    \"path\": \"/health\"\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  std::string make_web_env_example(const std::string &projectName)
  {
    std::string s;
    s.reserve(3600);

    s += "# ----------------------------------\n";
    s += "# App\n";
    s += "# ----------------------------------\n";
    s += "APP_NAME=" + projectName + "\n";
    s += "APP_ENV=development\n";
    s += "APP_TEMPLATE=web\n\n";

    s += "# ----------------------------------\n";
    s += "# Server\n";
    s += "# ----------------------------------\n";
    s += "SERVER_HOST=0.0.0.0\n";
    s += "SERVER_PORT=8080\n";
    s += "SERVER_REQUEST_TIMEOUT=5000\n";
    s += "SERVER_IO_THREADS=0\n";
    s += "SERVER_SESSION_TIMEOUT_SEC=20\n";
    s += "SERVER_BENCH_MODE=false\n\n";

    s += "# ----------------------------------\n";
    s += "# TLS\n";
    s += "# ----------------------------------\n";
    s += "# Keep TLS disabled when Nginx terminates HTTPS in production.\n";
    s += "SERVER_TLS_ENABLED=false\n";
    s += "SERVER_TLS_CERT_FILE=\n";
    s += "SERVER_TLS_KEY_FILE=\n\n";

    s += "# ----------------------------------\n";
    s += "# Logging\n";
    s += "# ----------------------------------\n";
    s += "VIX_LOG_LEVEL=info\n";
    s += "VIX_LOG_FORMAT=kv\n";
    s += "LOGGING_ASYNC=true\n";
    s += "LOGGING_QUEUE_MAX=20000\n";
    s += "LOGGING_DROP_ON_OVERFLOW=true\n\n";

    s += "# ----------------------------------\n";
    s += "# Web rendering\n";
    s += "# ----------------------------------\n";
    s += "PUBLIC_PATH=public\n";
    s += "VIEWS_PATH=views\n";
    s += "TEMPLATE_AUTO_ESCAPE_HTML=true\n";
    s += "TEMPLATE_CACHE=true\n\n";

    s += "# ----------------------------------\n";
    s += "# Storage\n";
    s += "# ----------------------------------\n";
    s += "STORAGE_PATH=storage\n\n";

    s += "# ----------------------------------\n";
    s += "# Production diagnostics\n";
    s += "# ----------------------------------\n";
    s += "VIX_SERVICE_NAME=" + projectName + "\n";
    s += "VIX_HEALTH_LOCAL=http://127.0.0.1:8080/health\n";
    s += "VIX_HEALTH_PUBLIC=\n";
    s += "VIX_PROXY_DOMAIN=\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
