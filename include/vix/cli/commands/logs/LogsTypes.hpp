/**
 *
 *  @file LogsTypes.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_LOGS_TYPES_HPP
#define VIX_LOGS_TYPES_HPP

#include <filesystem>
#include <string>

namespace vix::commands::logs
{
  /**
   * @enum LogsTarget
   * @brief Target selected by `vix logs`.
   */
  enum class LogsTarget
  {
    All,
    App,
    Proxy,
    Errors
  };

  /**
   * @struct LogsOptions
   * @brief Runtime options passed from the logs command line.
   */
  struct LogsOptions
  {
    /**
     * @brief Selected log target.
     */
    LogsTarget target{LogsTarget::All};

    /**
     * @brief Follow logs live.
     */
    bool follow{false};

    /**
     * @brief Show only errors.
     */
    bool errorsOnly{false};

    /**
     * @brief Show repeated error summary.
     */
    bool repeated{false};

    /**
     * @brief Print machine-readable JSON output when supported.
     */
    bool json{false};

    /**
     * @brief Number of lines to show.
     */
    int lines{120};

    /**
     * @brief Optional systemd time filter.
     *
     * Example:
     * "1 hour ago"
     */
    std::string since{};
  };

  /**
   * @struct LogsConfig
   * @brief Production logs configuration.
   */
  struct LogsConfig
  {
    /**
     * @brief Application name.
     */
    std::string appName{"vix-app"};

    /**
     * @brief systemd service name used for app logs.
     */
    std::string serviceName{};

    /**
     * @brief Nginx access log path.
     */
    std::filesystem::path nginxAccessLog{};

    /**
     * @brief Nginx error log path.
     */
    std::filesystem::path nginxErrorLog{};

    /**
     * @brief Default number of lines to show.
     */
    int lines{120};
  };
}

#endif
