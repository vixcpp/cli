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

#include <iostream>
#include <string>
#include <vector>

namespace
{
  bool is_help_arg(const std::string &arg)
  {
    return arg == "-h" || arg == "--help" || arg == "help";
  }
}

#ifdef VIX_CLI_HAS_UI

#include <vix/ui/platform/Platform.hpp>
#include <vix/ui/shell/AppShell.hpp>
#include <vix/ui/shell/ShellConfig.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
  struct DesktopOptions
  {
    std::string name{"Vix Desktop"};
    std::string title{"Vix Desktop"};
    std::string appId{};
    std::string appVersion{};
    std::string vendor{"Vix.cpp"};
    std::string iconPath{};

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
  };

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

  std::string shell_quote_env_value(const std::string &value)
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
    out += shell_quote_env_value(options.host);
    out += " ";

    out += "SERVER_PORT=";
    out += std::to_string(options.port);
    out += " ";

    out += command;

    return out;
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

      if (arg == "--app-version")
      {
        if (!consume_value(args, i, "--app-version", options.appVersion))
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

      if (parse_prefixed_value(arg, "--app-version=", value))
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

      error("Unexpected argument: " + arg);
      hint("Usage: vix desktop run [options]");
      return 1;
    }

    return 0;
  }

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

  int run_desktop_command(const std::vector<std::string> &args)
  {
    using namespace vix::cli::style;

    DesktopOptions options;

    const int parseResult = parse_options(args, options);

    if (parseResult == 2)
    {
      return vix::commands::DesktopCommand::help();
    }

    if (parseResult != 0)
    {
      return parseResult;
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

    std::vector<std::string> commandArgs = args;

    if (!commandArgs.empty() && commandArgs[0] == "run")
    {
      commandArgs.erase(commandArgs.begin());
    }
    else if (!commandArgs.empty() && commandArgs[0] != "run")
    {
      error("Unknown desktop command: " + commandArgs[0]);
      hint("Usage: vix desktop run [options]");
      hint("Run: vix desktop --help");
      return 1;
    }

#ifdef VIX_CLI_HAS_UI
    return run_desktop_command(commandArgs);
#else
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
        << "  vix desktop [options]\n"
        << "  vix desktop run [options]\n\n"

        << "Description:\n"
        << "  Open a Vix web UI application inside a desktop shell.\n"
        << "  The command builds a vix::ui::ShellConfig and starts vix::ui::AppShell.\n"
        << "  It can either open an existing URL or start a local server command first.\n\n"

        << "Options:\n"
        << "  --url <url>                 Target URL to open\n"
        << "  --url=<url>                 Same as --url <url>\n"
        << "  --host <host>               Local host used when --url is not set. Default: 127.0.0.1\n"
        << "  --host=<host>               Same as --host <host>\n"
        << "  --port <port>               Local port used when --url is not set. Default: 8080\n"
        << "  --port=<port>               Same as --port <port>\n"
        << "  --readiness-url <url>       URL checked before opening the shell\n"
        << "  --readiness-url=<url>       Same as --readiness-url <url>\n\n"

        << "Window options:\n"
        << "  --name <name>               Application name. Default: Vix Desktop\n"
        << "  --title <title>             Window title. Default: Vix Desktop\n"
        << "  --width <px>                Window width. Default: 1200\n"
        << "  --height <px>               Window height. Default: 800\n"
        << "  --icon <path>               Desktop window icon path\n"
        << "  --fullscreen                Start fullscreen\n"
        << "  --resizable                 Allow resizing, default\n"
        << "  --no-resizable              Disable resizing\n"
        << "  --devtools                  Enable WebView developer tools when supported\n"
        << "  --no-devtools               Disable developer tools, default\n\n"

        << "Local server options:\n"
        << "  --server <command>          Start this command before opening the shell\n"
        << "  --server=<command>          Same as --server <command>\n"
        << "  --cwd <dir>                 Working directory for --server\n"
        << "  --cwd=<dir>                 Same as --cwd <dir>\n"
        << "  --working-directory <dir>   Same as --cwd <dir>\n"
        << "  --wait                      Wait for the server to become reachable, default\n"
        << "  --no-wait                   Do not wait for server readiness\n"
        << "  --timeout-ms <ms>           Server startup timeout. Default: 30000\n\n"

        << "Metadata options:\n"
        << "  --app-id <id>               Stable desktop application id\n"
        << "  --app-version <version>     Application version metadata\n"
        << "  --vendor <name>             Vendor name. Default: Vix.cpp\n\n"

        << "Examples:\n"
        << "  vix desktop run --url http://127.0.0.1:8080\n"
        << "  vix desktop run --port 8080 --title \"My Vix App\"\n"
        << "  vix desktop run --server \"vix run main.cpp\" --port 8080\n"
        << "  vix desktop run --server \"vix run main.cpp -- --port 8080\" --url http://127.0.0.1:8080\n";

    return 0;
  }
}
