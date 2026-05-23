/**
 *
 *  @file WsOutput.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/ws/WsOutput.hpp>
#include <vix/cli/util/Ui.hpp>

#include <ostream>
#include <string>

namespace vix::commands::ws::output
{
  namespace
  {
    std::string yes_no(bool value)
    {
      return value ? "yes" : "no";
    }

    std::string target_name(WsTarget target)
    {
      switch (target)
      {
      case WsTarget::Check:
        return "check";
      }

      return "check";
    }

    std::string selected_url(
        const WsConfig &cfg,
        const WsOptions &options)
    {
      if (!options.url.empty())
        return options.url;

      if (!cfg.publicUrl.empty())
        return cfg.publicUrl;

      return cfg.localUrl;
    }
  }

  void print_summary(
      std::ostream &out,
      const WsConfig &cfg,
      const WsOptions &options)
  {
    vix::cli::util::section(out, "WebSocket");

    vix::cli::util::kv(out, "App", cfg.appName);
    vix::cli::util::kv(out, "Target", target_name(options.target));
    vix::cli::util::kv(out, "URL", selected_url(cfg, options));
    vix::cli::util::kv(out, "Local URL", cfg.localUrl.empty() ? "(missing)" : cfg.localUrl);
    vix::cli::util::kv(out, "Public URL", cfg.publicUrl.empty() ? "(missing)" : cfg.publicUrl);
    vix::cli::util::kv(out, "Host", cfg.host);
    vix::cli::util::kv(out, "Port", std::to_string(cfg.port));
    vix::cli::util::kv(out, "Path", cfg.path);
    vix::cli::util::kv(out, "Timeout", std::to_string(cfg.timeoutMs) + "ms");
    vix::cli::util::kv(out, "Ping", yes_no(options.ping));
    vix::cli::util::kv(out, "Heartbeat", yes_no(cfg.heartbeat));
    vix::cli::util::kv(out, "Verbose", yes_no(options.verbose));
  }

  void step(
      std::ostream &out,
      const std::string &label)
  {
    vix::cli::util::section(out, label);
  }

  void command(
      std::ostream &out,
      const std::string &command)
  {
    vix::cli::util::kv(out, "Command", command);
  }

  void ok(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::ok_line(out, message);
  }

  void warn(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::warn_line(out, message);
  }

  void error(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::err_line(out, message);
  }

  void fix(
      std::ostream &out,
      const std::string &message)
  {
    vix::cli::util::warn_line(out, "Fix: " + message);
  }
}
