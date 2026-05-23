/**
 *
 *  @file ServiceCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/ServiceCommand.hpp>
#include <vix/net/http/CurlClient.hpp>
#include <vix/net/http/ClientRequest.hpp>
#include <vix/net/http/Method.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  namespace
  {
    std::string trim_copy(std::string s)
    {
      while (!s.empty() &&
             (s.back() == '\n' ||
              s.back() == '\r' ||
              std::isspace(static_cast<unsigned char>(s.back()))))
      {
        s.pop_back();
      }

      std::size_t i = 0;

      while (i < s.size() &&
             std::isspace(static_cast<unsigned char>(s[i])))
      {
        ++i;
      }

      return s.substr(i);
    }

    std::string lower_copy(std::string s)
    {
      std::transform(
          s.begin(),
          s.end(),
          s.begin(),
          [](unsigned char c)
          {
            return static_cast<char>(std::tolower(c));
          });

      return s;
    }

    struct HealthResult
    {
      bool ok{false};
      int statusCode{0};
      std::string error;
    };

    HealthResult run_http_health_check(
        const std::string &url,
        std::uint64_t timeoutMs)
    {
      HealthResult result;

      vix::net::http::CurlClient client;

      vix::net::http::ClientRequest request;
      request.set_method(vix::net::http::Method::Get)
          .set_url(url)
          .set_timeout_ms(timeoutMs);

      auto response = client.send(request);

      if (!response)
      {
        result.error = std::string(response.error().message());
        return result;
      }

      result.statusCode = response.value().status_code;
      result.ok = response.value().success();

      if (!response.value().error.empty())
        result.error = response.value().error;

      return result;
    }

    std::string shell_quote(const std::string &s)
    {
      std::string out = "'";

      for (char c : s)
      {
        if (c == '\'')
          out += "'\\''";
        else
          out += c;
      }

      out += "'";
      return out;
    }

    std::optional<std::string> run_capture(const std::string &cmd)
    {
      FILE *pipe = popen(cmd.c_str(), "r");

      if (!pipe)
        return std::nullopt;

      std::string out;
      char buffer[2048];

      while (std::fgets(buffer, sizeof(buffer), pipe))
      {
        out += buffer;
      }

      pclose(pipe);

      out = trim_copy(out);

      if (out.empty())
        return std::nullopt;

      return out;
    }

    bool run_cmd(const std::string &cmd)
    {
      return std::system(cmd.c_str()) == 0;
    }

    json read_json_or_empty(const fs::path &path)
    {
      if (!fs::exists(path))
        return json::object();

      std::ifstream in(path);

      if (!in)
        return json::object();

      json j;

      try
      {
        in >> j;
      }
      catch (...)
      {
        return json::object();
      }

      return j;
    }

    std::optional<std::string> read_project_name()
    {
      const fs::path vixJsonPath = fs::current_path() / "vix.json";
      const json j = read_json_or_empty(vixJsonPath);

      if (j.is_object() &&
          j.contains("name") &&
          j["name"].is_string())
      {
        const std::string name = trim_copy(j["name"].get<std::string>());

        if (!name.empty())
          return name;
      }

      for (const auto &entry : fs::directory_iterator(fs::current_path()))
      {
        if (!entry.is_regular_file())
          continue;

        if (entry.path().extension() == ".vix")
          return entry.path().stem().string();
      }

      const std::string fallback = fs::current_path().filename().string();

      if (!fallback.empty())
        return fallback;

      return std::nullopt;
    }

    std::optional<fs::path> detect_build_dir()
    {
      const std::vector<fs::path> candidates = {
          fs::current_path() / "build-ninja",
          fs::current_path() / "build-release",
          fs::current_path() / "build",
          fs::current_path() / "cmake-build-debug",
          fs::current_path() / "cmake-build-release"};

      for (const auto &candidate : candidates)
      {
        if (fs::exists(candidate) && fs::is_directory(candidate))
          return candidate;
      }

      return std::nullopt;
    }

    fs::path detect_binary_path(const std::string &appName)
    {
      const auto buildDir = detect_build_dir();

      if (!buildDir)
        return fs::current_path() / "build-ninja" / appName;

      const fs::path direct = *buildDir / appName;

      if (fs::exists(direct))
        return direct;

      for (const auto &entry : fs::recursive_directory_iterator(*buildDir))
      {
        if (!entry.is_regular_file())
          continue;

        if (entry.path().filename() == appName)
          return entry.path();
      }

      return direct;
    }

    std::string current_user()
    {
      if (const char *user = vix::utils::vix_getenv("USER"))
      {
        if (*user)
          return user;
      }

      if (auto out = run_capture("id -un 2>/dev/null"))
        return *out;

      return "root";
    }

    std::optional<fs::path> detect_vix_package_dir()
    {
      if (const char *vixDir = vix::utils::vix_getenv("Vix_DIR"))
      {
        if (*vixDir)
          return fs::path(vixDir);
      }

      if (const char *prefix = vix::utils::vix_getenv("CMAKE_PREFIX_PATH"))
      {
        if (*prefix)
        {
          std::string value(prefix);
          const auto colon = value.find(':');

          if (colon != std::string::npos)
            value = value.substr(0, colon);

          if (!value.empty())
            return fs::path(value);
        }
      }

      const fs::path localBuildNinja =
          fs::current_path().parent_path() / "vix-clean" / "build-ninja";

      if (fs::exists(localBuildNinja))
        return localBuildNinja;

      return std::nullopt;
    }

    std::string service_name_for_app(const std::string &appName)
    {
      return lower_copy(appName) + ".service";
    }

    fs::path service_path_for_name(const std::string &serviceName)
    {
      return fs::path("/etc/systemd/system") / serviceName;
    }

    struct ServiceConfig
    {
      std::string appName;
      std::string serviceName;
      fs::path workingDir;
      fs::path execPath;
      std::string user;
      std::string restart{"always"};
      int restartSec{3};
      int limitNoFile{65535};
      std::optional<fs::path> vixDir;
      std::optional<std::string> healthLocal;
      std::optional<std::string> healthPublic;
      std::uint64_t healthTimeoutMs{2000};
    };

    ServiceConfig load_service_config();

    int service_health()
    {
      const ServiceConfig cfg = load_service_config();

      vix::cli::util::section(std::cout, "Service Health");

      vix::cli::util::kv(std::cout, "Service", cfg.serviceName);
      vix::cli::util::kv(std::cout, "Timeout", std::to_string(cfg.healthTimeoutMs) + "ms");

      bool ok = true;

      if (cfg.healthLocal)
      {
        vix::cli::util::kv(std::cout, "Health local", *cfg.healthLocal);

        const HealthResult result =
            run_http_health_check(*cfg.healthLocal, cfg.healthTimeoutMs);

        if (result.ok)
        {
          vix::cli::util::ok_line(
              std::cout,
              "local health check passed: HTTP " + std::to_string(result.statusCode));
        }
        else
        {
          ok = false;

          vix::cli::util::err_line(
              std::cerr,
              "local health check failed");

          if (result.statusCode > 0)
          {
            vix::cli::util::warn_line(
                std::cerr,
                "HTTP status: " + std::to_string(result.statusCode));
          }

          if (!result.error.empty())
          {
            vix::cli::util::warn_line(
                std::cerr,
                result.error);
          }
        }
      }

      if (cfg.healthPublic)
      {
        vix::cli::util::kv(std::cout, "Health public", *cfg.healthPublic);

        const HealthResult result =
            run_http_health_check(*cfg.healthPublic, cfg.healthTimeoutMs);

        if (result.ok)
        {
          vix::cli::util::ok_line(
              std::cout,
              "public health check passed: HTTP " + std::to_string(result.statusCode));
        }
        else
        {
          ok = false;

          vix::cli::util::err_line(
              std::cerr,
              "public health check failed");

          if (result.statusCode > 0)
          {
            vix::cli::util::warn_line(
                std::cerr,
                "HTTP status: " + std::to_string(result.statusCode));
          }

          if (!result.error.empty())
          {
            vix::cli::util::warn_line(
                std::cerr,
                result.error);
          }
        }
      }

      if (!cfg.healthLocal && !cfg.healthPublic)
      {
        vix::cli::util::warn_line(
            std::cerr,
            "No health check configured.");

        vix::cli::util::warn_line(
            std::cerr,
            "Add production.service.health_local or production.service.health_public to vix.json.");

        return 1;
      }

      return ok ? 0 : 1;
    }

    ServiceConfig load_service_config()
    {
      ServiceConfig cfg;

      cfg.appName = read_project_name().value_or("vix-app");
      cfg.serviceName = service_name_for_app(cfg.appName);
      cfg.workingDir = fs::current_path();
      cfg.execPath = detect_binary_path(cfg.appName);
      cfg.user = current_user();
      cfg.vixDir = detect_vix_package_dir();

      const json j = read_json_or_empty(fs::current_path() / "vix.json");

      if (j.is_object() && j.contains("production") && j["production"].is_object())
      {
        const auto &prod = j["production"];

        if (prod.contains("service") && prod["service"].is_object())
        {
          const auto &svc = prod["service"];

          if (svc.contains("name") && svc["name"].is_string())
            cfg.serviceName = svc["name"].get<std::string>();

          if (!cfg.serviceName.ends_with(".service"))
            cfg.serviceName += ".service";

          if (svc.contains("user") && svc["user"].is_string())
            cfg.user = svc["user"].get<std::string>();

          if (svc.contains("working_dir") && svc["working_dir"].is_string())
            cfg.workingDir = fs::path(svc["working_dir"].get<std::string>());

          if (svc.contains("health_local") && svc["health_local"].is_string())
            cfg.healthLocal = svc["health_local"].get<std::string>();

          if (svc.contains("health_public") && svc["health_public"].is_string())
            cfg.healthPublic = svc["health_public"].get<std::string>();

          if (svc.contains("health_timeout_ms") && svc["health_timeout_ms"].is_number_unsigned())
            cfg.healthTimeoutMs = svc["health_timeout_ms"].get<std::uint64_t>();

          if (svc.contains("exec") && svc["exec"].is_string())
          {
            const fs::path exec = svc["exec"].get<std::string>();

            if (exec.is_absolute())
              cfg.execPath = exec;
            else
              cfg.execPath = cfg.workingDir / exec;
          }

          if (svc.contains("restart") && svc["restart"].is_string())
            cfg.restart = svc["restart"].get<std::string>();

          if (svc.contains("restart_sec") && svc["restart_sec"].is_number_integer())
            cfg.restartSec = svc["restart_sec"].get<int>();

          if (svc.contains("limit_nofile") && svc["limit_nofile"].is_number_integer())
            cfg.limitNoFile = svc["limit_nofile"].get<int>();
        }
      }

      return cfg;
    }

    std::string render_systemd_service(const ServiceConfig &cfg)
    {
      std::ostringstream out;

      out << "[Unit]\n";
      out << "Description=" << cfg.appName << "\n";
      out << "After=network.target\n\n";

      out << "[Service]\n";
      out << "User=" << cfg.user << "\n";
      out << "WorkingDirectory=" << cfg.workingDir.string() << "\n";

      if (cfg.vixDir)
      {
        out << "Environment=Vix_DIR=" << cfg.vixDir->string() << "\n";
        out << "Environment=CMAKE_PREFIX_PATH=" << cfg.vixDir->string() << "\n";
      }

      out << "ExecStart=" << cfg.execPath.string() << "\n";
      out << "Restart=" << cfg.restart << "\n";
      out << "RestartSec=" << cfg.restartSec << "\n";
      out << "LimitNOFILE=" << cfg.limitNoFile << "\n\n";

      out << "[Install]\n";
      out << "WantedBy=multi-user.target\n";

      return out.str();
    }

    int install_service()
    {
      const ServiceConfig cfg = load_service_config();

      vix::cli::util::section(std::cout, "Service Install");

      vix::cli::util::kv(std::cout, "App", cfg.appName);
      vix::cli::util::kv(std::cout, "Service", cfg.serviceName);
      vix::cli::util::kv(std::cout, "Working directory", cfg.workingDir.string());
      vix::cli::util::kv(std::cout, "ExecStart", cfg.execPath.string());
      vix::cli::util::kv(std::cout, "User", cfg.user);

      if (!fs::exists(cfg.execPath))
      {
        vix::cli::util::err_line(
            std::cerr,
            "Executable not found: " + cfg.execPath.string());

        vix::cli::util::warn_line(
            std::cerr,
            "Fix: run `vix build` before installing the service.");

        return 1;
      }

      const std::string unit = render_systemd_service(cfg);
      const fs::path tmp = fs::temp_directory_path() / cfg.serviceName;

      {
        std::ofstream out(tmp);

        if (!out)
        {
          vix::cli::util::err_line(
              std::cerr,
              "Failed to write temporary service file: " + tmp.string());

          return 1;
        }

        out << unit;
      }

      const fs::path target = service_path_for_name(cfg.serviceName);

      const bool copied = run_cmd(
          "sudo cp " +
          shell_quote(tmp.string()) +
          " " +
          shell_quote(target.string()));

      if (!copied)
      {
        vix::cli::util::err_line(std::cerr, "Failed to install systemd service.");
        return 1;
      }

      run_cmd("rm -f " + shell_quote(tmp.string()));

      if (!run_cmd("sudo systemctl daemon-reload"))
      {
        vix::cli::util::err_line(std::cerr, "systemctl daemon-reload failed.");
        return 1;
      }

      if (!run_cmd("sudo systemctl enable " + shell_quote(cfg.serviceName)))
      {
        vix::cli::util::err_line(std::cerr, "systemctl enable failed.");
        return 1;
      }

      vix::cli::util::ok_line(
          std::cout,
          "service installed: " + cfg.serviceName);

      return 0;
    }

    int service_action(const std::string &action)
    {
      const ServiceConfig cfg = load_service_config();

      if (action == "status")
      {
        return std::system(
            ("systemctl status " + shell_quote(cfg.serviceName)).c_str());
      }

      if (action == "logs")
      {
        return std::system(
            ("journalctl -u " + shell_quote(cfg.serviceName) + " -n 120 --no-pager").c_str());
      }

      const std::string cmd =
          "sudo systemctl " + action + " " + shell_quote(cfg.serviceName);

      const int rc = std::system(cmd.c_str());

      if (rc == 0)
      {
        vix::cli::util::ok_line(
            std::cout,
            "service " + action + ": " + cfg.serviceName);
      }
      else
      {
        vix::cli::util::err_line(
            std::cerr,
            "service " + action + " failed: " + cfg.serviceName);
      }

      return rc == 0 ? 0 : 1;
    }
  }

  int ServiceCommand::run(const std::vector<std::string> &args)
  {
#ifndef __linux__
    vix::cli::util::err_line(
        std::cerr,
        "vix service is currently supported on Linux/systemd only.");

    return 1;
#else
    if (args.empty() || args[0] == "-h" || args[0] == "--help")
      return help();

    const std::string action = args[0];

    if (action == "install")
      return install_service();

    if (action == "health")
      return service_health();

    if (action == "start" ||
        action == "stop" ||
        action == "restart" ||
        action == "status" ||
        action == "logs")
    {
      return service_action(action);
    }

    vix::cli::util::err_line(std::cerr, "unknown service command: " + action);
    vix::cli::util::warn_line(std::cerr, "Tip: vix service --help");

    return 1;
#endif
  }

  int ServiceCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix service <command>\n\n"
        << "Commands:\n"
        << "  install     Generate and install a systemd service\n"
        << "  start       Start the service\n"
        << "  stop        Stop the service\n"
        << "  restart     Restart the service\n"
        << "  status      Show service status\n"
        << "  logs        Show recent service logs\n"
        << "  health      Run configured HTTP health checks\n\n"
        << "Examples:\n"
        << "  vix service install\n"
        << "  vix service restart\n"
        << "  vix service status\n"
        << "  vix service logs\n"
        << "  vix service health\n";
    return 0;
  }
}
