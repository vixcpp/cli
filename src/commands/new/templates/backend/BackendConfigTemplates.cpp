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

  std::string make_backend_production_config_json()
  {
    return R"JSON({
  "app": {
    "env": "production"
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "request_timeout": 5000,
    "io_threads": 0,
    "session_timeout_sec": 20
  },
  "logging": {
    "level": "info",
    "async": true,
    "queue_max": 20000,
    "drop_on_overflow": true
  },
  "storage": {
    "path": "storage"
  },
  "health": {
    "path": "/health"
  }
}
)JSON";
  }

  std::string make_backend_env_example()
  {
    return R"(# ----------------------------------
# Server
# ----------------------------------
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
APP_ENV=development

# ----------------------------------
# Logging
# ----------------------------------
LOG_LEVEL=info
LOGGING_ASYNC=true

# ----------------------------------
# Storage
# ----------------------------------
STORAGE_PATH=storage

# ----------------------------------
# Database
# ----------------------------------
DATABASE_ENGINE=mysql
DATABASE_DEFAULT_HOST=127.0.0.1
DATABASE_DEFAULT_PORT=3306
DATABASE_DEFAULT_USER=root
DATABASE_DEFAULT_PASSWORD=
DATABASE_DEFAULT_NAME=appdb
)";
  }

} // namespace vix::commands::new_cmd::templates
