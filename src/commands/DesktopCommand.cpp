/**
 *
 *  @file DesktopCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#include <vix/cli/commands/DesktopCommand.hpp>
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
  bool is_help_arg(const std::string &arg)
  {
    return arg == "-h" || arg == "--help" || arg == "help";
  }

  bool is_command_arg(const std::string &arg)
  {
    return arg == "run" ||
           arg == "build" ||
           arg == "package";
  }

  bool parse_prefixed_value(
      const std::string &arg,
      const char *prefix,
      std::string &out)
  {
    const std::string p(prefix);

    if (arg.rfind(p, 0) != 0)
    {
      return false;
    }

    out = arg.substr(p.size());
    return true;
  }

  bool consume_value(
      const std::vector<std::string> &args,
      std::size_t &index,
      const std::string &option,
      std::string &out)
  {
    using namespace vix::cli::style;

    if (index + 1 >= args.size())
    {
      error("Missing value for " + option + ".");
      return false;
    }

    out = args[++index];

    if (out.empty())
    {
      error("Value for " + option + " cannot be empty.");
      return false;
    }

    return true;
  }

  bool parse_port(const std::string &value, std::uint16_t &out)
  {
    if (value.empty())
    {
      return false;
    }

    unsigned int port = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    auto result = std::from_chars(begin, end, port);

    if (result.ec != std::errc{} || result.ptr != end)
    {
      return false;
    }

    if (port == 0 || port > 65535)
    {
      return false;
    }

    out = static_cast<std::uint16_t>(port);
    return true;
  }

  bool parse_positive_int(const std::string &value, int &out)
  {
    if (value.empty())
    {
      return false;
    }

    int parsed = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end)
    {
      return false;
    }

    if (parsed <= 0)
    {
      return false;
    }

    out = parsed;
    return true;
  }

  bool parse_non_negative_int(const std::string &value, int &out)
  {
    if (value.empty())
    {
      return false;
    }

    int parsed = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end)
    {
      return false;
    }

    if (parsed < 0)
    {
      return false;
    }

    out = parsed;
    return true;
  }

  std::string shell_quote(const std::string &value)
  {
    std::string out;
    out.reserve(value.size() + 2);

    out += "'";

    for (char ch : value)
    {
      if (ch == '\'')
      {
        out += "'\\''";
      }
      else
      {
        out += ch;
      }
    }

    out += "'";
    return out;
  }

  std::string json_escape(const std::string &value)
  {
    std::string out;
    out.reserve(value.size() + 16);

    for (char ch : value)
    {
      switch (ch)
      {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += ch;
        break;
      }
    }

    return out;
  }

  std::string slugify(std::string value)
  {
    std::string out;
    out.reserve(value.size());

    bool lastDash = false;

    for (char ch : value)
    {
      const unsigned char c = static_cast<unsigned char>(ch);

      if (std::isalnum(c))
      {
        out.push_back(static_cast<char>(std::tolower(c)));
        lastDash = false;
        continue;
      }

      if (!lastDash)
      {
        out.push_back('-');
        lastDash = true;
      }
    }

    while (!out.empty() && out.front() == '-')
    {
      out.erase(out.begin());
    }

    while (!out.empty() && out.back() == '-')
    {
      out.pop_back();
    }

    if (out.empty())
    {
      out = "vix-desktop-app";
    }

    return out;
  }

  std::string bool_text(bool value)
  {
    return value ? "true" : "false";
  }

  bool is_cpp_file(const fs::path &path)
  {
    return path.extension() == ".cpp" ||
           path.extension() == ".cc" ||
           path.extension() == ".cxx" ||
           path.extension() == ".C";
  }

  bool is_regular_executable(const fs::path &path)
  {
    std::error_code ec;

    if (!fs::is_regular_file(path, ec) || ec)
    {
      return false;
    }

#ifdef _WIN32
    return path.extension() == ".exe";
#else
    const fs::perms perms = fs::status(path, ec).permissions();

    if (ec)
    {
      return false;
    }

    using pr = fs::perms;

    return (perms & pr::owner_exec) != pr::none ||
           (perms & pr::group_exec) != pr::none ||
           (perms & pr::others_exec) != pr::none;
#endif
  }

  void make_executable(const fs::path &path)
  {
    std::error_code ec;

    fs::permissions(
        path,
        fs::perms::owner_exec |
            fs::perms::group_exec |
            fs::perms::others_exec,
        fs::perm_options::add,
        ec);
  }

  std::optional<fs::path> current_executable_path()
  {
#ifdef _WIN32
    return std::nullopt;
#else
    std::error_code ec;
    fs::path path = fs::read_symlink("/proc/self/exe", ec);

    if (ec || path.empty())
    {
      return std::nullopt;
    }

    return path;
#endif
  }

  std::string local_url(const std::string &host, std::uint16_t port)
  {
    return "http://" + host + ":" + std::to_string(port);
  }

  enum class DesktopAction
  {
    Run,
    Build,
    Package
  };

  struct DesktopOptions
  {
    DesktopAction action{DesktopAction::Run};

    std::string name{"Vix Desktop"};
    std::string title{"Vix Desktop"};
    std::string appId{};
    std::string appVersion{"1.0.0"};
    std::string vendor{"Vix.cpp"};
    std::string iconPath{};

    std::string target{};
    std::string url{};
    std::string readinessUrl{};
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};

    int width{1200};
    int height{800};

    bool resizable{true};
    bool fullscreen{false};
    bool devtools{false};

    bool startServer{false};
    bool waitForServer{true};
    std::chrono::milliseconds startupTimeout{30000};

    std::string serverCommand{};
    std::string serverWorkingDirectory{};

    std::string outputDirectory{};
    std::string packageTarget{"dir"};
    std::string binaryPath{};

    bool clean{false};
    bool withSqlite{false};
    bool withMySql{false};
    bool localCache{false};
    int jobs{0};
  };

  std::string with_desktop_server_env(
      const DesktopOptions &options,
      const std::string &command)
  {
    if (command.empty())
    {
      return command;
    }

    std::string out;

    out += "SERVER_HOST=";
    out += shell_quote(options.host);
    out += " ";

    out += "SERVER_PORT=";
    out += std::to_string(options.port);
    out += " ";

    out += command;

    return out;
  }

  int parse_options(
      const std::vector<std::string> &args,
      DesktopOptions &options)
  {
    using namespace vix::cli::style;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (is_help_arg(arg))
      {
        return 2;
      }

      if (arg == "--name")
      {
        if (!consume_value(args, i, "--name", options.name))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--title")
      {
        if (!consume_value(args, i, "--title", options.title))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--app-id")
      {
        if (!consume_value(args, i, "--app-id", options.appId))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--app-version" || arg == "--version")
      {
        if (!consume_value(args, i, arg, options.appVersion))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--vendor")
      {
        if (!consume_value(args, i, "--vendor", options.vendor))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--icon")
      {
        if (!consume_value(args, i, "--icon", options.iconPath))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--url")
      {
        if (!consume_value(args, i, "--url", options.url))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--readiness-url")
      {
        if (!consume_value(args, i, "--readiness-url", options.readinessUrl))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--host")
      {
        if (!consume_value(args, i, "--host", options.host))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--port")
      {
        std::string value;

        if (!consume_value(args, i, "--port", value))
        {
          return 1;
        }

        if (!parse_port(value, options.port))
        {
          error("Invalid desktop port.");
          hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      if (arg == "--width")
      {
        std::string value;

        if (!consume_value(args, i, "--width", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.width))
        {
          error("Invalid desktop width.");
          hint("Width must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (arg == "--height")
      {
        std::string value;

        if (!consume_value(args, i, "--height", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.height))
        {
          error("Invalid desktop height.");
          hint("Height must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (arg == "--server")
      {
        if (!consume_value(args, i, "--server", options.serverCommand))
        {
          return 1;
        }

        options.startServer = true;
        continue;
      }

      if (arg == "--cwd" || arg == "--working-directory")
      {
        if (!consume_value(args, i, arg, options.serverWorkingDirectory))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--timeout-ms")
      {
        std::string value;
        int timeout = 0;

        if (!consume_value(args, i, "--timeout-ms", value))
        {
          return 1;
        }

        if (!parse_non_negative_int(value, timeout))
        {
          error("Invalid desktop startup timeout.");
          hint("Timeout must be a non-negative number of milliseconds.");
          return 1;
        }

        options.startupTimeout = std::chrono::milliseconds(timeout);
        continue;
      }

      if (arg == "--out" || arg == "-o")
      {
        if (!consume_value(args, i, arg, options.outputDirectory))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--target")
      {
        if (!consume_value(args, i, "--target", options.packageTarget))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--binary" || arg == "--server-binary")
      {
        if (!consume_value(args, i, arg, options.binaryPath))
        {
          return 1;
        }

        continue;
      }

      if (arg == "-j" || arg == "--jobs")
      {
        std::string value;

        if (!consume_value(args, i, arg, value))
        {
          return 1;
        }

        if (!parse_non_negative_int(value, options.jobs))
        {
          error("Invalid jobs value.");
          hint("Jobs must be a non-negative integer.");
          return 1;
        }

        continue;
      }

      if (arg == "--devtools")
      {
        options.devtools = true;
        continue;
      }

      if (arg == "--no-devtools")
      {
        options.devtools = false;
        continue;
      }

      if (arg == "--fullscreen")
      {
        options.fullscreen = true;
        continue;
      }

      if (arg == "--resizable")
      {
        options.resizable = true;
        continue;
      }

      if (arg == "--no-resizable")
      {
        options.resizable = false;
        continue;
      }

      if (arg == "--wait")
      {
        options.waitForServer = true;
        continue;
      }

      if (arg == "--no-wait")
      {
        options.waitForServer = false;
        continue;
      }

      if (arg == "--clean")
      {
        options.clean = true;
        continue;
      }

      if (arg == "--with-sqlite")
      {
        options.withSqlite = true;
        continue;
      }

      if (arg == "--with-mysql")
      {
        options.withMySql = true;
        continue;
      }

      if (arg == "--local-cache")
      {
        options.localCache = true;
        continue;
      }

      std::string value;

      if (parse_prefixed_value(arg, "--name=", value))
      {
        if (value.empty())
        {
          error("Value for --name cannot be empty.");
          return 1;
        }

        options.name = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--title=", value))
      {
        if (value.empty())
        {
          error("Value for --title cannot be empty.");
          return 1;
        }

        options.title = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--app-id=", value))
      {
        options.appId = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--app-version=", value) ||
          parse_prefixed_value(arg, "--version=", value))
      {
        options.appVersion = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--vendor=", value))
      {
        options.vendor = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--icon=", value))
      {
        options.iconPath = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--url=", value))
      {
        if (value.empty())
        {
          error("Value for --url cannot be empty.");
          return 1;
        }

        options.url = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--readiness-url=", value))
      {
        options.readinessUrl = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--host=", value))
      {
        if (value.empty())
        {
          error("Value for --host cannot be empty.");
          return 1;
        }

        options.host = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--port=", value))
      {
        if (!parse_port(value, options.port))
        {
          error("Invalid desktop port.");
          hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--width=", value))
      {
        if (!parse_positive_int(value, options.width))
        {
          error("Invalid desktop width.");
          hint("Width must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--height=", value))
      {
        if (!parse_positive_int(value, options.height))
        {
          error("Invalid desktop height.");
          hint("Height must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--server=", value))
      {
        if (value.empty())
        {
          error("Value for --server cannot be empty.");
          return 1;
        }

        options.serverCommand = value;
        options.startServer = true;
        continue;
      }

      if (parse_prefixed_value(arg, "--cwd=", value) ||
          parse_prefixed_value(arg, "--working-directory=", value))
      {
        options.serverWorkingDirectory = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--timeout-ms=", value))
      {
        int timeout = 0;

        if (!parse_non_negative_int(value, timeout))
        {
          error("Invalid desktop startup timeout.");
          hint("Timeout must be a non-negative number of milliseconds.");
          return 1;
        }

        options.startupTimeout = std::chrono::milliseconds(timeout);
        continue;
      }

      if (parse_prefixed_value(arg, "--out=", value))
      {
        if (value.empty())
        {
          error("Value for --out cannot be empty.");
          return 1;
        }

        options.outputDirectory = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--target=", value))
      {
        if (value.empty())
        {
          error("Value for --target cannot be empty.");
          return 1;
        }

        options.packageTarget = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--binary=", value) ||
          parse_prefixed_value(arg, "--server-binary=", value))
      {
        if (value.empty())
        {
          error("Value for --binary cannot be empty.");
          return 1;
        }

        options.binaryPath = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--jobs=", value) ||
          parse_prefixed_value(arg, "-j=", value))
      {
        if (!parse_non_negative_int(value, options.jobs))
        {
          error("Invalid jobs value.");
          hint("Jobs must be a non-negative integer.");
          return 1;
        }

        continue;
      }

      if (!arg.empty() && arg[0] != '-')
      {
        if (options.target.empty())
        {
          options.target = arg;
          continue;
        }

        error("Unexpected positional argument: " + arg);
        return 1;
      }

      error("Unexpected argument: " + arg);
      hint("Usage: vix desktop <run|build|package> [target] [options]");
      return 1;
    }

    if (options.title == "Vix Desktop" && options.name != "Vix Desktop")
    {
      options.title = options.name;
    }

    return 0;
  }
}

#ifdef VIX_CLI_HAS_UI

#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/ui/platform/Platform.hpp>
#include <vix/ui/shell/AppShell.hpp>
#include <vix/ui/shell/ShellConfig.hpp>

namespace
{
  vix::ui::ShellConfig make_shell_config(const DesktopOptions &options)
  {
    vix::ui::ShellConfig config;

    config
        .set_name(options.name)
        .set_title(options.title)
        .set_vendor(options.vendor)
        .set_host(options.host)
        .set_port(options.port)
        .set_platform(vix::ui::Platform::current())
        .set_width(options.width)
        .set_height(options.height)
        .set_resizable(options.resizable)
        .set_fullscreen(options.fullscreen)
        .set_devtools(options.devtools)
        .set_start_server(options.startServer)
        .set_wait_for_server(options.waitForServer)
        .set_startup_timeout(options.startupTimeout);

    if (!options.appId.empty())
    {
      config.set_app_id(options.appId);
    }

    if (!options.appVersion.empty())
    {
      config.set_app_version(options.appVersion);
    }

    if (!options.iconPath.empty())
    {
      config.set_icon_path(options.iconPath);
    }

    if (!options.url.empty())
    {
      config.set_url(options.url);
    }

    if (!options.readinessUrl.empty())
    {
      config.set_readiness_url(options.readinessUrl);
    }

    if (!options.serverCommand.empty())
    {
      config.set_server_command(
          with_desktop_server_env(options, options.serverCommand));
    }

    if (!options.serverWorkingDirectory.empty())
    {
      config.set_server_working_directory(options.serverWorkingDirectory);
    }

    return config;
  }

  int run_desktop_app(DesktopOptions options)
  {
    using namespace vix::cli::style;

    if (!options.target.empty() && options.serverCommand.empty())
    {
      fs::path targetPath = options.target;

      if (is_cpp_file(targetPath))
      {
        options.serverCommand =
            "vix run --force-server " + shell_quote(targetPath.string());
      }
      else
      {
        options.serverCommand = shell_quote(targetPath.string());
      }

      options.startServer = true;
    }

    if (options.url.empty())
    {
      options.url = local_url(options.host, options.port);
    }

    vix::ui::ShellConfig config = make_shell_config(options);

    vix::ui::AppShell shell(config);

    info("Preparing Vix desktop shell.");
    step("target: " + shell.target_url());

    vix::ui::Result<void> result = shell.start();

    if (result.is_failed())
    {
      error("Unable to start Vix desktop shell.");
      hint(result.error_message().empty()
               ? "Unknown desktop shell error."
               : result.error_message());

      return 1;
    }

    success("Vix desktop shell closed.");
    return 0;
  }

  int build_server_executable(
      const DesktopOptions &options,
      fs::path &serverExecutable)
  {
    using namespace vix::cli::style;

    if (!options.binaryPath.empty())
    {
      serverExecutable = fs::absolute(options.binaryPath).lexically_normal();

      if (!fs::exists(serverExecutable))
      {
        error("Server binary not found: " + serverExecutable.string());
        return 1;
      }

      if (!is_regular_executable(serverExecutable))
      {
        error("Server binary is not executable: " + serverExecutable.string());
        return 1;
      }

      return 0;
    }

    if (options.target.empty())
    {
      error("Desktop build requires a C++ file or --binary.");
      hint("Usage: vix desktop build app.cpp --name \"My App\"");
      hint("Or:    vix desktop build --binary ./build/app --name \"My App\"");
      return 1;
    }

    const fs::path targetPath =
        fs::absolute(options.target).lexically_normal();

    if (!fs::exists(targetPath))
    {
      error("Desktop target not found: " + targetPath.string());
      return 1;
    }

    if (!is_cpp_file(targetPath))
    {
      if (is_regular_executable(targetPath))
      {
        serverExecutable = targetPath;
        return 0;
      }

      error("Desktop build target must be a C++ file or executable binary.");
      hint("Received: " + targetPath.string());
      return 1;
    }

    vix::commands::RunCommand::detail::Options runOptions;

    runOptions.singleCpp = true;
    runOptions.cppFile = targetPath;
    runOptions.forceServerLike = true;
    runOptions.clean = options.clean;
    runOptions.jobs = options.jobs;
    runOptions.withSqlite = options.withSqlite;
    runOptions.withMySql = options.withMySql;
    runOptions.localCache = options.localCache;

    info("Building desktop server:");
    step(targetPath.string());

    const int buildCode =
        vix::commands::RunCommand::detail::build_script_executable(
            runOptions,
            serverExecutable);

    if (buildCode != 0)
    {
      return buildCode;
    }

    if (!fs::exists(serverExecutable))
    {
      error("Built server executable was not found.");
      hint("Resolved path: " + serverExecutable.string());
      return 1;
    }

    return 0;
  }

  fs::path resolve_output_directory(const DesktopOptions &options)
  {
    if (!options.outputDirectory.empty())
    {
      return fs::absolute(options.outputDirectory).lexically_normal();
    }

    return fs::absolute(fs::path("dist") / slugify(options.name))
        .lexically_normal();
  }

  int write_desktop_manifest(
      const DesktopOptions &options,
      const fs::path &bundleDir,
      const std::string &serverName,
      const std::string &launcherName,
      const std::string &iconName)
  {
    using namespace vix::cli::style;

    const fs::path manifestPath = bundleDir / "vix-desktop.json";

    std::ofstream out(manifestPath, std::ios::trunc);

    if (!out)
    {
      error("Unable to write desktop manifest: " + manifestPath.string());
      return 1;
    }

    const std::string url =
        options.url.empty()
            ? local_url(options.host, options.port)
            : options.url;

    const std::string readinessUrl =
        options.readinessUrl.empty()
            ? url
            : options.readinessUrl;

    out << "{\n";
    out << "  \"name\": \"" << json_escape(options.name) << "\",\n";
    out << "  \"title\": \"" << json_escape(options.title) << "\",\n";
    out << "  \"app_id\": \"" << json_escape(options.appId) << "\",\n";
    out << "  \"version\": \"" << json_escape(options.appVersion) << "\",\n";
    out << "  \"vendor\": \"" << json_escape(options.vendor) << "\",\n";
    out << "  \"launcher\": \"./" << json_escape(launcherName) << "\",\n";
    out << "  \"server\": \"./bin/" << json_escape(serverName) << "\",\n";
    out << "  \"host\": \"" << json_escape(options.host) << "\",\n";
    out << "  \"port\": " << options.port << ",\n";
    out << "  \"url\": \"" << json_escape(url) << "\",\n";
    out << "  \"readiness_url\": \"" << json_escape(readinessUrl) << "\",\n";
    out << "  \"width\": " << options.width << ",\n";
    out << "  \"height\": " << options.height << ",\n";
    out << "  \"resizable\": " << bool_text(options.resizable) << ",\n";
    out << "  \"fullscreen\": " << bool_text(options.fullscreen) << ",\n";
    out << "  \"devtools\": " << bool_text(options.devtools) << ",\n";
    out << "  \"icon\": ";

    if (iconName.empty())
    {
      out << "null\n";
    }
    else
    {
      out << "\"./resources/" << json_escape(iconName) << "\"\n";
    }

    out << "}\n";

    return 0;
  }

  int write_launcher_script(
      const DesktopOptions &options,
      const fs::path &bundleDir,
      const std::string &serverName,
      const std::string &launcherName)
  {
    using namespace vix::cli::style;

    const fs::path launcherPath = bundleDir / launcherName;

    std::ofstream out(launcherPath, std::ios::trunc);

    if (!out)
    {
      error("Unable to write launcher: " + launcherPath.string());
      return 1;
    }

    const std::string url =
        options.url.empty()
            ? local_url(options.host, options.port)
            : options.url;

    const std::string readinessUrl =
        options.readinessUrl.empty()
            ? url
            : options.readinessUrl;

    out << "#!/usr/bin/env sh\n";
    out << "set -eu\n";
    out << "APP_DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n";
    out << "cd \"$APP_DIR\"\n";
    out << "exec \"$APP_DIR/bin/vix\" desktop run \\\n";
    out << "  --server \"$APP_DIR/bin/" << serverName << "\" \\\n";
    out << "  --url " << shell_quote(url) << " \\\n";
    out << "  --readiness-url " << shell_quote(readinessUrl) << " \\\n";
    out << "  --host " << shell_quote(options.host) << " \\\n";
    out << "  --port " << options.port << " \\\n";
    out << "  --name " << shell_quote(options.name) << " \\\n";
    out << "  --title " << shell_quote(options.title) << " \\\n";
    out << "  --vendor " << shell_quote(options.vendor) << " \\\n";

    if (!options.appId.empty())
    {
      out << "  --app-id " << shell_quote(options.appId) << " \\\n";
    }

    if (!options.appVersion.empty())
    {
      out << "  --app-version " << shell_quote(options.appVersion) << " \\\n";
    }

    out << "  --width " << options.width << " \\\n";
    out << "  --height " << options.height << " \\\n";
    out << "  --timeout-ms " << options.startupTimeout.count();

    if (options.fullscreen)
    {
      out << " \\\n  --fullscreen";
    }

    if (options.resizable)
    {
      out << " \\\n  --resizable";
    }
    else
    {
      out << " \\\n  --no-resizable";
    }

    if (options.devtools)
    {
      out << " \\\n  --devtools";
    }
    else
    {
      out << " \\\n  --no-devtools";
    }

    out << "\n";

    make_executable(launcherPath);
    return 0;
  }

  int write_linux_desktop_file(
      const DesktopOptions &options,
      const fs::path &bundleDir,
      const std::string &launcherName,
      const std::string &desktopFileName,
      const std::string &iconName)
  {
    using namespace vix::cli::style;

    const fs::path desktopPath = bundleDir / desktopFileName;

    std::ofstream out(desktopPath, std::ios::trunc);

    if (!out)
    {
      error("Unable to write .desktop file: " + desktopPath.string());
      return 1;
    }

    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=" << options.name << "\n";
    out << "Comment=" << options.title << "\n";
    out << "Exec=" << (bundleDir / launcherName).string() << "\n";
    out << "Terminal=false\n";
    out << "Categories=Development;\n";

    if (!iconName.empty())
    {
      out << "Icon=" << (bundleDir / "resources" / iconName).string() << "\n";
    }

    make_executable(desktopPath);
    return 0;
  }

  int copy_optional_icon(
      const DesktopOptions &options,
      const fs::path &resourcesDir,
      std::string &iconName)
  {
    using namespace vix::cli::style;

    iconName.clear();

    if (options.iconPath.empty())
    {
      return 0;
    }

    const fs::path iconPath =
        fs::absolute(options.iconPath).lexically_normal();

    if (!fs::exists(iconPath))
    {
      error("Icon file not found: " + iconPath.string());
      return 1;
    }

    iconName = iconPath.filename().string();

    std::error_code ec;
    fs::copy_file(
        iconPath,
        resourcesDir / iconName,
        fs::copy_options::overwrite_existing,
        ec);

    if (ec)
    {
      error("Unable to copy icon: " + ec.message());
      return 1;
    }

    return 0;
  }

  int build_desktop_bundle(const DesktopOptions &options)
  {
    using namespace vix::cli::style;

    if (options.packageTarget != "dir")
    {
      error("Unsupported desktop package target: " + options.packageTarget);
      hint("Supported now: --target dir");
      hint("AppImage, deb and rpm should be added as separate packaging backends.");
      return 1;
    }

    fs::path serverExecutable;

    const int serverCode = build_server_executable(options, serverExecutable);

    if (serverCode != 0)
    {
      return serverCode;
    }

    const auto selfPath = current_executable_path();

    if (!selfPath)
    {
      error("Unable to locate current vix executable for bundling.");
      hint("The first desktop package backend currently supports Linux.");
      return 1;
    }

    const fs::path bundleDir = resolve_output_directory(options);
    const fs::path binDir = bundleDir / "bin";
    const fs::path resourcesDir = bundleDir / "resources";

    std::error_code ec;

    fs::create_directories(binDir, ec);
    if (ec)
    {
      error("Unable to create bundle bin directory: " + ec.message());
      return 1;
    }

    fs::create_directories(resourcesDir, ec);
    if (ec)
    {
      error("Unable to create bundle resources directory: " + ec.message());
      return 1;
    }

    const std::string appSlug = slugify(options.name);

    std::string serverName = serverExecutable.filename().string();

    if (serverName.empty())
    {
      serverName = appSlug + "-server";
    }

    const fs::path bundledServer = binDir / serverName;
    const fs::path bundledVix = binDir / "vix";

    fs::copy_file(
        serverExecutable,
        bundledServer,
        fs::copy_options::overwrite_existing,
        ec);

    if (ec)
    {
      error("Unable to copy server executable: " + ec.message());
      return 1;
    }

    fs::copy_file(
        *selfPath,
        bundledVix,
        fs::copy_options::overwrite_existing,
        ec);

    if (ec)
    {
      error("Unable to copy vix desktop runtime: " + ec.message());
      return 1;
    }

    make_executable(bundledServer);
    make_executable(bundledVix);

    std::string iconName;
    const int iconCode = copy_optional_icon(options, resourcesDir, iconName);

    if (iconCode != 0)
    {
      return iconCode;
    }

    const std::string launcherName = appSlug;
    const std::string desktopFileName = appSlug + ".desktop";

    const int manifestCode =
        write_desktop_manifest(
            options,
            bundleDir,
            serverName,
            launcherName,
            iconName);

    if (manifestCode != 0)
    {
      return manifestCode;
    }

    const int launcherCode =
        write_launcher_script(
            options,
            bundleDir,
            serverName,
            launcherName);

    if (launcherCode != 0)
    {
      return launcherCode;
    }

    const int desktopCode =
        write_linux_desktop_file(
            options,
            bundleDir,
            launcherName,
            desktopFileName,
            iconName);

    if (desktopCode != 0)
    {
      return desktopCode;
    }

    success("Desktop bundle created.");
    step(bundleDir.string());
    hint("Run:");
    step((bundleDir / launcherName).string());

    return 0;
  }

  int run_desktop_command(
      DesktopAction action,
      const std::vector<std::string> &args)
  {
    DesktopOptions options;
    options.action = action;

    const int parseResult = parse_options(args, options);

    if (parseResult == 2)
    {
      return vix::commands::DesktopCommand::help();
    }

    if (parseResult != 0)
    {
      return parseResult;
    }

    if (action == DesktopAction::Run)
    {
      return run_desktop_app(std::move(options));
    }

    return build_desktop_bundle(options);
  }
}

#endif // VIX_CLI_HAS_UI

namespace vix::commands
{
  int DesktopCommand::run(const std::vector<std::string> &args)
  {
    using namespace vix::cli::style;

    if (!args.empty() && is_help_arg(args[0]))
    {
      return help();
    }

    DesktopAction action = DesktopAction::Run;
    std::vector<std::string> commandArgs = args;

    if (!commandArgs.empty() && is_command_arg(commandArgs[0]))
    {
      const std::string command = commandArgs[0];
      commandArgs.erase(commandArgs.begin());

      if (command == "run")
      {
        action = DesktopAction::Run;
      }
      else if (command == "build")
      {
        action = DesktopAction::Build;
      }
      else if (command == "package")
      {
        action = DesktopAction::Package;
      }
    }
    else if (!commandArgs.empty() && commandArgs[0].rfind("-", 0) == 0)
    {
      action = DesktopAction::Run;
    }
    else if (!commandArgs.empty())
    {
      /*
       * Simple default:
       *
       *   vix desktop ui.cpp
       *
       * behaves like:
       *
       *   vix desktop run ui.cpp
       */
      action = DesktopAction::Run;
    }

#ifdef VIX_CLI_HAS_UI
    return run_desktop_command(action, commandArgs);
#else
    (void)action;
    (void)commandArgs;

    error("Vix desktop is not available in this build.");
    hint("Build the CLI with the vix::ui module enabled.");
    hint("Expected compile definition: VIX_CLI_HAS_UI=1");

    return 1;
#endif
  }

  int DesktopCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix desktop run [target.cpp] [options]\n"
        << "  vix desktop build <target.cpp|binary> [options]\n"
        << "  vix desktop package <target.cpp|binary> [options]\n"
        << "  vix desktop [target.cpp] [options]\n\n"

        << "Description:\n"
        << "  Open, build, or package a Vix web UI application as a desktop app.\n"
        << "  `run` is for development. `build` and `package --target dir` create\n"
        << "  a distributable desktop folder with a launcher and compiled server.\n\n"

        << "Simple examples:\n"
        << "  vix desktop run ui_dashboard.cpp\n"
        << "  vix desktop ui_dashboard.cpp\n"
        << "  vix desktop build ui_dashboard.cpp --name \"Vix UI Dashboard\"\n"
        << "  vix desktop package ui_dashboard.cpp --target dir --name \"Vix UI Dashboard\"\n\n"

        << "Run examples:\n"
        << "  vix desktop run --url http://127.0.0.1:8080\n"
        << "  vix desktop run --server \"vix run --force-server main.cpp\" --port 8080\n"
        << "  vix desktop run ./build/my_server --title \"My App\"\n\n"

        << "Build/package examples:\n"
        << "  vix desktop build ui_dashboard.cpp --name \"Vix UI Dashboard\"\n"
        << "  vix desktop build ui_dashboard.cpp --out dist/dashboard\n"
        << "  vix desktop build --binary ./build/ui_dashboard --name \"Vix UI Dashboard\"\n"
        << "  vix desktop package ui_dashboard.cpp --target dir --icon assets/icon.png\n\n"

        << "Common options:\n"
        << "  --name <name>               Application name. Default: Vix Desktop\n"
        << "  --title <title>             Window title. Default: same as name\n"
        << "  --app-id <id>               Stable desktop application id\n"
        << "  --app-version <version>     Application version. Default: 1.0.0\n"
        << "  --version <version>         Same as --app-version\n"
        << "  --vendor <name>             Vendor name. Default: Vix.cpp\n"
        << "  --icon <path>               Desktop icon path\n\n"

        << "Window options:\n"
        << "  --width <px>                Window width. Default: 1200\n"
        << "  --height <px>               Window height. Default: 800\n"
        << "  --fullscreen                Start fullscreen\n"
        << "  --resizable                 Allow resizing, default\n"
        << "  --no-resizable              Disable resizing\n"
        << "  --devtools                  Enable WebView developer tools when supported\n"
        << "  --no-devtools               Disable developer tools, default\n\n"

        << "Server options:\n"
        << "  --url <url>                 Target URL to open\n"
        << "  --host <host>               Local host. Default: 127.0.0.1\n"
        << "  --port <port>               Local port. Default: 8080\n"
        << "  --readiness-url <url>       URL checked before opening the shell\n"
        << "  --server <command>          Dev mode server command\n"
        << "  --cwd <dir>                 Working directory for --server\n"
        << "  --working-directory <dir>   Same as --cwd\n"
        << "  --wait                      Wait for server readiness, default\n"
        << "  --no-wait                   Do not wait for server readiness\n"
        << "  --timeout-ms <ms>           Server startup timeout. Default: 30000\n\n"

        << "Build/package options:\n"
        << "  --out <dir>                 Output directory. Default: dist/<app-name>\n"
        << "  --target <target>           Package target. Supported now: dir\n"
        << "  --binary <path>             Use an already-built server binary\n"
        << "  --server-binary <path>      Same as --binary\n"
        << "  --clean                     Clean/rebuild script cache before build\n"
        << "  -j, --jobs <n>              Parallel build jobs\n"
        << "  --with-sqlite               Enable SQLite support for script build\n"
        << "  --with-mysql                Enable MySQL support for script build\n"
        << "  --local-cache               Use local .vix-scripts cache\n\n";

    return 0;
  }
}
