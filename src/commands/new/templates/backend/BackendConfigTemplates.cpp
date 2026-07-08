/**
 * @file BackendConfigTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendConfigTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_production_config_json(const std::string &projectName)
  {
    std::string s;
    s.reserve(2600);

    s += "{\n";
    s += "  \"app\": {\n";
    s += "    \"name\": \"" + projectName + "\",\n";
    s += "    \"env\": \"production\"\n";
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
    s += "    \"index\": \"index.html\",\n";
    s += "    \"cache_control\": \"public, max-age=3600\",\n";
    s += "    \"spa_fallback\": false\n";
    s += "  },\n";
    s += "  \"templates\": {\n";
    s += "    \"path\": \"views\"\n";
    s += "  },\n";
    s += "  \"storage\": {\n";
    s += "    \"path\": \"storage\"\n";
    s += "  },\n";
    s += "  \"database\": {\n";
    s += "    \"engine\": \"sqlite\",\n";
    s += "    \"sqlite_path\": \"storage/" + projectName + ".db\",\n";
    s += "    \"migrations\": \"migrations\"\n";
    s += "  },\n";
    s += "  \"health\": {\n";
    s += "    \"path\": \"/health\",\n";
    s += "    \"api_path\": \"/api/health\"\n";
    s += "  },\n";
    s += "  \"websocket\": {\n";
    s += "    \"enabled\": false,\n";
    s += "    \"host\": \"0.0.0.0\",\n";
    s += "    \"port\": 9090,\n";
    s += "    \"path\": \"/ws\"\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  std::string make_backend_env_example(
      const std::string &projectName,
      bool apiOnly)
  {
    std::string s;
    s.reserve(4200);

    s += "# ----------------------------------\n";
    s += "# App\n";
    s += "# ----------------------------------\n";
    s += "APP_NAME=" + projectName + "\n";
    s += "APP_ENV=development\n\n";

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

    if (!apiOnly)
    {
      s += "# Public files and templates\n";
      s += "# ----------------------------------\n";
      s += "PUBLIC_PATH=public\n";
      s += "PUBLIC_MOUNT=/\n";
      s += "PUBLIC_INDEX=index.html\n";
      s += "PUBLIC_CACHE_CONTROL=public, max-age=3600\n";
      s += "PUBLIC_SPA_FALLBACK=false\n";
      s += "PUBLIC_COMPRESSION=false\n";
      s += "PUBLIC_COMPRESSION_MIN_SIZE=1024\n";
      s += "VIEWS_PATH=views\n\n";
    }

    s += "# ----------------------------------\n";
    s += "# Storage\n";
    s += "# ----------------------------------\n";
    s += "STORAGE_PATH=storage\n\n";

    s += "# ----------------------------------\n";
    s += "# Database\n";
    s += "# ----------------------------------\n";
    s += "DATABASE_ENGINE=sqlite\n";
    s += "DATABASE_SQLITE_PATH=storage/" + projectName + ".db\n";
    s += "DATABASE_DEFAULT_HOST=127.0.0.1\n";
    s += "DATABASE_DEFAULT_PORT=3306\n";
    s += "DATABASE_DEFAULT_USER=root\n";
    s += "DATABASE_DEFAULT_PASSWORD=\n";
    s += "DATABASE_DEFAULT_NAME=" + projectName + "\n\n";

    s += "# ----------------------------------\n";
    s += "# ORM and migrations\n";
    s += "# ----------------------------------\n";
    s += "VIX_ORM_HOST=tcp://127.0.0.1:3306\n";
    s += "VIX_ORM_USER=root\n";
    s += "VIX_ORM_PASS=\n";
    s += "VIX_ORM_DB=" + projectName + "\n";
    s += "VIX_ORM_DIR=migrations\n";
    s += "VIX_ORM_TOOL=\n\n";

    s += "# ----------------------------------\n";
    s += "# WebSocket\n";
    s += "# ----------------------------------\n";
    s += "WEBSOCKET_ENABLED=false\n";
    s += "WEBSOCKET_HOST=0.0.0.0\n";
    s += "WEBSOCKET_PORT=9090\n";
    s += "WEBSOCKET_MAX_MESSAGE_SIZE=65536\n";
    s += "WEBSOCKET_IDLE_TIMEOUT=60\n";
    s += "WEBSOCKET_ENABLE_DEFLATE=true\n";
    s += "WEBSOCKET_PING_INTERVAL=30\n";
    s += "WEBSOCKET_AUTO_PING_PONG=true\n\n";

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
