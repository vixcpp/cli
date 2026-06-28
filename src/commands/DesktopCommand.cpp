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

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define VIX_DESK_ISATTY(fd) _isatty(fd)
#define VIX_DESK_FILENO(stream) _fileno(stream)
#else
#include <unistd.h>
#define VIX_DESK_ISATTY(fd) ::isatty(fd)
#define VIX_DESK_FILENO(stream) ::fileno(stream)
#endif

namespace fs = std::filesystem;

namespace
{
  // -------------------------------------------------------------------------
  //  Color handling (ANSI), auto-detected and overridable.
  // -------------------------------------------------------------------------

  struct DesktopTheme
  {
    bool color = true;

    std::string_view reset() const { return color ? "\x1b[0m" : ""; }
    std::string_view dim() const { return color ? "\x1b[2m" : ""; }
    std::string_view bold() const { return color ? "\x1b[1m" : ""; }

    std::string_view green() const { return color ? "\x1b[32m" : ""; }
    std::string_view cyan() const { return color ? "\x1b[36m" : ""; }
    std::string_view yellow() const { return color ? "\x1b[33m" : ""; }
    std::string_view red() const { return color ? "\x1b[31m" : ""; }
    std::string_view gray() const { return color ? "\x1b[90m" : ""; }
  };

  // Honor NO_COLOR (https://no-color.org), FORCE_COLOR, and TTY detection.
  bool desk_detect_color(std::ostream &os)
  {
    if (std::getenv("NO_COLOR") != nullptr)
    {
      return false;
    }

    if (std::getenv("FORCE_COLOR") != nullptr)
    {
      return true;
    }

    std::FILE *target = (&os == &std::cerr) ? stderr : stdout;

    return VIX_DESK_ISATTY(VIX_DESK_FILENO(target)) != 0;
  }

  // -------------------------------------------------------------------------
  //  Output verbosity, controlled by CLI flags (modern-runtime style).
  // -------------------------------------------------------------------------

  enum class DesktopLogMode
  {
    Normal, // banner + lifecycle lines
    Quiet,  // errors only
    Json    // machine-readable single-line events on stdout
  };

  struct DesktopReporter
  {
    DesktopTheme theme;
    DesktopLogMode mode = DesktopLogMode::Normal;

    bool normal() const { return mode == DesktopLogMode::Normal; }
    bool json() const { return mode == DesktopLogMode::Json; }

    void banner(std::string_view title) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << '\n'
                << "  " << theme.green() << theme.bold() << title << theme.reset()
                << '\n';
    }

    // key/value row, padded so values align in a column.
    void row(std::string_view key, std::string_view value,
             std::string_view valueColor = {}) const
    {
      if (!normal())
      {
        return;
      }

      constexpr std::size_t keyWidth = 9;

      std::string label(key);
      if (label.size() < keyWidth)
      {
        label.append(keyWidth - label.size(), ' ');
      }

      std::cout << "  " << theme.dim() << label << theme.reset() << "  "
                << (valueColor.empty() ? theme.reset() : valueColor)
                << value << theme.reset() << '\n';
    }

    void info_line(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.cyan() << "info" << theme.reset()
                << "  " << message << '\n';
    }

    void step(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.gray() << "\u2022 " << theme.reset()
                << message << '\n';
    }

    void hint(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.gray() << message << theme.reset() << '\n';
    }

    void success(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.green() << "\u2714" << theme.reset()
                << " " << message << '\n';
    }

    // Errors always print (even in quiet mode), to stderr.
    void error(std::string_view message) const
    {
      std::cerr << "  " << theme.red() << theme.bold() << "error" << theme.reset()
                << "  " << message << '\n';
    }

    void error_hint(std::string_view message) const
    {
      if (message.empty())
      {
        return;
      }

      std::cerr << "  " << theme.gray() << message << theme.reset() << '\n';
    }

    void event(std::string_view name, std::string_view key = {},
               std::string_view value = {}) const
    {
      if (!json())
      {
        return;
      }

      std::cout << "{\"event\":\"" << name << "\"";

      if (!key.empty())
      {
        std::cout << ",\"" << key << "\":\"" << value << "\"";
      }

      std::cout << "}\n";
      std::cout.flush();
    }
  };

  bool is_help_arg(const std::string &arg)
  {
    return arg == "-h" || arg == "--help" || arg == "help";
  }

#ifdef VIX_CLI_HAS_UI

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

  std::string trim_left_copy(std::string value)
  {
    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [](unsigned char ch)
            {
              return !std::isspace(ch);
            }));

    return value;
  }

  bool is_vix_run_server_command(const std::string &command)
  {
    const std::string value = trim_left_copy(command);

    return value == "vix run" ||
           value.rfind("vix run ", 0) == 0 ||
           value.find(" vix run ") != std::string::npos;
  }

  bool reject_vix_run_server_command(
      const DesktopReporter &out,
      const std::string &command)
  {
    if (!is_vix_run_server_command(command))
    {
      return false;
    }

    out.error("Use the C++ file directly.");
    out.error_hint("Run: vix desktop run ui_dashboard.cpp --port 8080");

    return true;
  }

  bool consume_value(
      const DesktopReporter &out,
      const std::vector<std::string> &args,
      std::size_t &index,
      const std::string &option,
      std::string &value)
  {
    if (index + 1 >= args.size())
    {
      out.error("Missing value for " + option + ".");
      return false;
    }

    value = args[++index];

    if (value.empty())
    {
      out.error("Value for " + option + " cannot be empty.");
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

    unsigned long port = 0;

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

    long parsed = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end)
    {
      return false;
    }

    if (parsed <= 0 || parsed > 1000000)
    {
      return false;
    }

    out = static_cast<int>(parsed);
    return true;
  }

  bool parse_non_negative_int(const std::string &value, int &out)
  {
    if (value.empty())
    {
      return false;
    }

    long parsed = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end)
    {
      return false;
    }

    if (parsed < 0 || parsed > 100000000)
    {
      return false;
    }

    out = static_cast<int>(parsed);
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
  bool desktop_shell_backend_available()
  {
#if defined(_WIN32)
    return true;
#elif defined(__APPLE__)
    return true;
#elif defined(__linux__)
#if defined(VIX_UI_ENABLE_LINUX_WEBVIEW) && VIX_UI_ENABLE_LINUX_WEBVIEW
    return true;
#else
    return false;
#endif
#else
    return false;
#endif
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

    std::vector<std::string> resourcePaths{};
    bool autoResources{true};

    bool clean{false};
    bool withSqlite{false};
    bool withMySql{false};
    bool localCache{false};
    int jobs{0};

    DesktopLogMode logMode{DesktopLogMode::Normal};
    int colorOverride{-1}; // -1 auto, 0 off, 1 on
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

  // Shared output-flag parsing (--quiet / --json / --color).
  // Returns true if `arg` was an output flag and was consumed.
  bool take_output_flag(const std::string &arg, DesktopOptions &options)
  {
    if (arg == "--quiet" || arg == "-q" || arg == "--silent")
    {
      options.logMode = DesktopLogMode::Quiet;
      return true;
    }

    if (arg == "--json")
    {
      options.logMode = DesktopLogMode::Json;
      return true;
    }

    if (arg == "--no-color" || arg == "--no-colour")
    {
      options.colorOverride = 0;
      return true;
    }

    if (arg == "--color" || arg == "--colour")
    {
      options.colorOverride = 1;
      return true;
    }

    return false;
  }

  int parse_options(
      const DesktopReporter &out,
      const std::vector<std::string> &args,
      DesktopOptions &options)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (is_help_arg(arg))
      {
        return 2;
      }

      if (take_output_flag(arg, options))
      {
        continue;
      }

      if (arg == "--name")
      {
        if (!consume_value(out, args, i, "--name", options.name))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--title")
      {
        if (!consume_value(out, args, i, "--title", options.title))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--app-id")
      {
        if (!consume_value(out, args, i, "--app-id", options.appId))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--app-version" || arg == "--version")
      {
        if (!consume_value(out, args, i, arg, options.appVersion))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--vendor")
      {
        if (!consume_value(out, args, i, "--vendor", options.vendor))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--icon")
      {
        if (!consume_value(out, args, i, "--icon", options.iconPath))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--url")
      {
        if (!consume_value(out, args, i, "--url", options.url))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--readiness-url")
      {
        if (!consume_value(out, args, i, "--readiness-url", options.readinessUrl))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--host")
      {
        if (!consume_value(out, args, i, "--host", options.host))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--port")
      {
        std::string value;

        if (!consume_value(out, args, i, "--port", value))
        {
          return 1;
        }

        if (!parse_port(value, options.port))
        {
          out.error("Invalid desktop port.");
          out.error_hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      if (arg == "--width")
      {
        std::string value;

        if (!consume_value(out, args, i, "--width", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.width))
        {
          out.error("Invalid desktop width.");
          out.error_hint("Width must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (arg == "--height")
      {
        std::string value;

        if (!consume_value(out, args, i, "--height", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.height))
        {
          out.error("Invalid desktop height.");
          out.error_hint("Height must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (arg == "--server")
      {
        std::string value;

        if (!consume_value(out, args, i, "--server", value))
        {
          return 1;
        }

        if (reject_vix_run_server_command(out, value))
        {
          return 1;
        }

        options.serverCommand = std::move(value);
        options.startServer = true;
        continue;
      }

      if (arg == "--cwd" || arg == "--working-directory")
      {
        if (!consume_value(out, args, i, arg, options.serverWorkingDirectory))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--timeout-ms")
      {
        std::string value;
        int timeout = 0;

        if (!consume_value(out, args, i, "--timeout-ms", value))
        {
          return 1;
        }

        if (!parse_non_negative_int(value, timeout))
        {
          out.error("Invalid desktop startup timeout.");
          out.error_hint("Timeout must be a non-negative number of milliseconds.");
          return 1;
        }

        options.startupTimeout = std::chrono::milliseconds(timeout);
        continue;
      }

      if (arg == "--out" || arg == "-o")
      {
        if (!consume_value(out, args, i, arg, options.outputDirectory))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--target")
      {
        if (!consume_value(out, args, i, "--target", options.packageTarget))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--binary" || arg == "--server-binary")
      {
        if (!consume_value(out, args, i, arg, options.binaryPath))
        {
          return 1;
        }

        continue;
      }

      if (arg == "-j" || arg == "--jobs")
      {
        std::string value;

        if (!consume_value(out, args, i, arg, value))
        {
          return 1;
        }

        if (!parse_non_negative_int(value, options.jobs))
        {
          out.error("Invalid jobs value.");
          out.error_hint("Jobs must be a non-negative integer.");
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

      if (arg == "--resources" || arg == "--resource")
      {
        std::string value;

        if (!consume_value(out, args, i, arg, value))
        {
          return 1;
        }

        options.resourcePaths.push_back(value);
        continue;
      }

      if (arg == "--no-auto-resources")
      {
        options.autoResources = false;
        continue;
      }

      std::string value;

      if (parse_prefixed_value(arg, "--name=", value))
      {
        if (value.empty())
        {
          out.error("Value for --name cannot be empty.");
          return 1;
        }

        options.name = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--title=", value))
      {
        if (value.empty())
        {
          out.error("Value for --title cannot be empty.");
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
          out.error("Value for --url cannot be empty.");
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
          out.error("Value for --host cannot be empty.");
          return 1;
        }

        options.host = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--port=", value))
      {
        if (!parse_port(value, options.port))
        {
          out.error("Invalid desktop port.");
          out.error_hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--width=", value))
      {
        if (!parse_positive_int(value, options.width))
        {
          out.error("Invalid desktop width.");
          out.error_hint("Width must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--height=", value))
      {
        if (!parse_positive_int(value, options.height))
        {
          out.error("Invalid desktop height.");
          out.error_hint("Height must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--server=", value))
      {
        if (value.empty())
        {
          out.error("Value for --server cannot be empty.");
          return 1;
        }

        if (reject_vix_run_server_command(out, value))
        {
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
          out.error("Invalid desktop startup timeout.");
          out.error_hint("Timeout must be a non-negative number of milliseconds.");
          return 1;
        }

        options.startupTimeout = std::chrono::milliseconds(timeout);
        continue;
      }

      if (parse_prefixed_value(arg, "--out=", value))
      {
        if (value.empty())
        {
          out.error("Value for --out cannot be empty.");
          return 1;
        }

        options.outputDirectory = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--target=", value))
      {
        if (value.empty())
        {
          out.error("Value for --target cannot be empty.");
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
          out.error("Value for --binary cannot be empty.");
          return 1;
        }

        options.binaryPath = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--resources=", value) ||
          parse_prefixed_value(arg, "--resource=", value))
      {
        if (value.empty())
        {
          out.error("Value for --resources cannot be empty.");
          return 1;
        }

        options.resourcePaths.push_back(value);
        continue;
      }

      if (parse_prefixed_value(arg, "--jobs=", value) ||
          parse_prefixed_value(arg, "-j=", value))
      {
        if (!parse_non_negative_int(value, options.jobs))
        {
          out.error("Invalid jobs value.");
          out.error_hint("Jobs must be a non-negative integer.");
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

        out.error("Unexpected positional argument: " + arg);
        return 1;
      }

      out.error("Unexpected argument: " + arg);
      out.error_hint("Usage: vix desktop <run|build|package> [target] [options]");
      return 1;
    }

    if (options.title == "Vix Desktop" && options.name != "Vix Desktop")
    {
      options.title = options.name;
    }

    return 0;
  }
#endif
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

  int build_server_executable(
      const DesktopReporter &out,
      const DesktopOptions &options,
      fs::path &serverExecutable);

  int run_desktop_app(const DesktopReporter &out, DesktopOptions options)
  {
    if (!options.target.empty() && options.serverCommand.empty())
    {
      fs::path targetPath =
          fs::absolute(options.target).lexically_normal();

      if (is_cpp_file(targetPath))
      {
        fs::path serverExecutable;

        const int buildCode =
            build_server_executable(out, options, serverExecutable);

        if (buildCode != 0)
        {
          return buildCode;
        }

        options.serverCommand =
            shell_quote(serverExecutable.string());

        if (options.serverWorkingDirectory.empty())
        {
          options.serverWorkingDirectory =
              targetPath.parent_path().string();
        }
      }
      else
      {
        if (!fs::exists(targetPath))
        {
          out.error("Desktop server target not found: " + targetPath.string());
          return 1;
        }

        if (!is_regular_executable(targetPath))
        {
          out.error("Desktop server target is not executable: " + targetPath.string());
          return 1;
        }

        options.serverCommand =
            shell_quote(targetPath.string());

        if (options.serverWorkingDirectory.empty())
        {
          options.serverWorkingDirectory =
              targetPath.parent_path().string();
        }
      }

      options.startServer = true;
    }

    if (options.url.empty())
    {
      options.url = local_url(options.host, options.port);
    }

    vix::ui::ShellConfig config = make_shell_config(options);

    vix::ui::AppShell shell(config);

    out.event("starting", "url", shell.target_url());

    out.banner("Vix Desktop");
    out.row("app", options.name);
    out.row("target", shell.target_url(), out.theme.cyan());

    if (options.startServer && !options.serverCommand.empty())
    {
      out.row("mode", "dev (managed server)");
    }
    else
    {
      out.row("mode", "attach");
    }

    const auto shellStart = std::chrono::steady_clock::now();

    vix::ui::Result<void> result = shell.start();

    const auto shellEnd = std::chrono::steady_clock::now();

    const auto shellLifetimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            shellEnd - shellStart)
            .count();

    if (result.is_failed())
    {
      out.error("Desktop shell unavailable.");
      return 1;
    }

    if (shellLifetimeMs < 800)
    {
      out.error("Desktop shell unavailable.");
      return 1;
    }

    out.event("closed");
    return 0;
  }

  int build_server_executable(
      const DesktopReporter &out,
      const DesktopOptions &options,
      fs::path &serverExecutable)
  {
    if (!options.binaryPath.empty())
    {
      serverExecutable = fs::absolute(options.binaryPath).lexically_normal();

      if (!fs::exists(serverExecutable))
      {
        out.error("Server binary not found: " + serverExecutable.string());
        return 1;
      }

      if (!is_regular_executable(serverExecutable))
      {
        out.error("Server binary is not executable: " + serverExecutable.string());
        return 1;
      }

      return 0;
    }

    if (options.target.empty())
    {
      out.error("Desktop build requires a C++ file or --binary.");
      out.error_hint("Usage: vix desktop build app.cpp --name \"My App\"");
      out.error_hint("Or:    vix desktop build --binary ./build/app --name \"My App\"");
      return 1;
    }

    const fs::path targetPath =
        fs::absolute(options.target).lexically_normal();

    if (!fs::exists(targetPath))
    {
      out.error("Desktop target not found: " + targetPath.string());
      return 1;
    }

    if (!is_cpp_file(targetPath))
    {
      if (is_regular_executable(targetPath))
      {
        serverExecutable = targetPath;
        return 0;
      }

      out.error("Desktop build target must be a C++ file or executable binary.");
      out.error_hint("Received: " + targetPath.string());
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

    out.info_line("Building desktop server:");
    out.step(targetPath.string());

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
      out.error("Built server executable was not found.");
      out.error_hint("Resolved path: " + serverExecutable.string());
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
      const DesktopReporter &out,
      const DesktopOptions &options,
      const fs::path &bundleDir,
      const std::string &serverName,
      const std::string &launcherName,
      const std::string &iconName)
  {
    const fs::path manifestPath = bundleDir / "vix-desktop.json";

    std::ofstream file(manifestPath, std::ios::trunc);

    if (!file)
    {
      out.error("Unable to write desktop manifest: " + manifestPath.string());
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

    file << "{\n";
    file << "  \"name\": \"" << json_escape(options.name) << "\",\n";
    file << "  \"title\": \"" << json_escape(options.title) << "\",\n";
    file << "  \"app_id\": \"" << json_escape(options.appId) << "\",\n";
    file << "  \"version\": \"" << json_escape(options.appVersion) << "\",\n";
    file << "  \"vendor\": \"" << json_escape(options.vendor) << "\",\n";
    file << "  \"launcher\": \"./" << json_escape(launcherName) << "\",\n";
    file << "  \"server\": \"./bin/" << json_escape(serverName) << "\",\n";
    file << "  \"host\": \"" << json_escape(options.host) << "\",\n";
    file << "  \"port\": " << options.port << ",\n";
    file << "  \"url\": \"" << json_escape(url) << "\",\n";
    file << "  \"readiness_url\": \"" << json_escape(readinessUrl) << "\",\n";
    file << "  \"width\": " << options.width << ",\n";
    file << "  \"height\": " << options.height << ",\n";
    file << "  \"resizable\": " << bool_text(options.resizable) << ",\n";
    file << "  \"fullscreen\": " << bool_text(options.fullscreen) << ",\n";
    file << "  \"devtools\": " << bool_text(options.devtools) << ",\n";
    file << "  \"icon\": ";

    if (iconName.empty())
    {
      file << "null\n";
    }
    else
    {
      file << "\"./resources/" << json_escape(iconName) << "\"\n";
    }

    file << "}\n";

    return 0;
  }

  int write_launcher_script(
      const DesktopReporter &out,
      const DesktopOptions &options,
      const fs::path &bundleDir,
      const std::string &serverName,
      const std::string &launcherName)
  {
    const fs::path launcherPath = bundleDir / launcherName;

    std::ofstream file(launcherPath, std::ios::trunc);

    if (!file)
    {
      out.error("Unable to write launcher: " + launcherPath.string());
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

    file << "#!/usr/bin/env sh\n";
    file << "set -eu\n";
    file << "APP_DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n";
    file << "cd \"$APP_DIR\"\n";
    file << "exec \"$APP_DIR/bin/vix\" desktop run \\\n";
    file << "  --server \"$APP_DIR/bin/" << serverName << "\" \\\n";
    file << "  --url " << shell_quote(url) << " \\\n";
    file << "  --readiness-url " << shell_quote(readinessUrl) << " \\\n";
    file << "  --host " << shell_quote(options.host) << " \\\n";
    file << "  --port " << options.port << " \\\n";
    file << "  --name " << shell_quote(options.name) << " \\\n";
    file << "  --title " << shell_quote(options.title) << " \\\n";
    file << "  --vendor " << shell_quote(options.vendor) << " \\\n";

    if (!options.appId.empty())
    {
      file << "  --app-id " << shell_quote(options.appId) << " \\\n";
    }

    if (!options.appVersion.empty())
    {
      file << "  --app-version " << shell_quote(options.appVersion) << " \\\n";
    }

    file << "  --width " << options.width << " \\\n";
    file << "  --height " << options.height << " \\\n";
    file << "  --timeout-ms " << options.startupTimeout.count();

    if (options.fullscreen)
    {
      file << " \\\n  --fullscreen";
    }

    if (options.resizable)
    {
      file << " \\\n  --resizable";
    }
    else
    {
      file << " \\\n  --no-resizable";
    }

    if (options.devtools)
    {
      file << " \\\n  --devtools";
    }
    else
    {
      file << " \\\n  --no-devtools";
    }

    file << "\n";

    make_executable(launcherPath);
    return 0;
  }

  int write_linux_desktop_file(
      const DesktopReporter &out,
      const DesktopOptions &options,
      const fs::path &bundleDir,
      const std::string &launcherName,
      const std::string &desktopFileName,
      const std::string &iconName)
  {
    const fs::path desktopPath = bundleDir / desktopFileName;

    std::ofstream file(desktopPath, std::ios::trunc);

    if (!file)
    {
      out.error("Unable to write .desktop file: " + desktopPath.string());
      return 1;
    }

    file << "[Desktop Entry]\n";
    file << "Type=Application\n";
    file << "Name=" << options.name << "\n";
    file << "Comment=" << options.title << "\n";
    file << "Exec=" << (bundleDir / launcherName).string() << "\n";
    file << "Terminal=false\n";
    file << "Categories=Development;\n";

    if (!iconName.empty())
    {
      file << "Icon=" << (bundleDir / "resources" / iconName).string() << "\n";
    }

    make_executable(desktopPath);
    return 0;
  }

  int copy_optional_icon(
      const DesktopReporter &out,
      const DesktopOptions &options,
      const fs::path &resourcesDir,
      std::string &iconName)
  {
    iconName.clear();

    if (options.iconPath.empty())
    {
      return 0;
    }

    const fs::path iconPath =
        fs::absolute(options.iconPath).lexically_normal();

    if (!fs::exists(iconPath))
    {
      out.error("Icon file not found: " + iconPath.string());
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
      out.error("Unable to copy icon: " + ec.message());
      return 1;
    }

    return 0;
  }

  std::vector<std::string> default_desktop_resource_names()
  {
    return {
        "templates",
        "assets",
        "static",
        "public",
        "views",
        "resources",
        "wwwroot"};
  }

  fs::path resolve_desktop_resource_base_directory(
      const DesktopOptions &options,
      const fs::path &serverExecutable)
  {
    if (!options.target.empty())
    {
      std::error_code ec;
      const fs::path targetPath =
          fs::absolute(options.target, ec).lexically_normal();

      if (!ec && fs::exists(targetPath, ec) && fs::is_regular_file(targetPath, ec))
      {
        return targetPath.parent_path();
      }
    }

    if (!options.serverWorkingDirectory.empty())
    {
      std::error_code ec;
      return fs::absolute(options.serverWorkingDirectory, ec).lexically_normal();
    }

    if (!serverExecutable.empty())
    {
      return serverExecutable.parent_path();
    }

    return fs::current_path();
  }

  bool same_path(const fs::path &a, const fs::path &b)
  {
    std::error_code ec;

    if (fs::equivalent(a, b, ec) && !ec)
    {
      return true;
    }

    return fs::absolute(a).lexically_normal() ==
           fs::absolute(b).lexically_normal();
  }

  bool path_is_inside(const fs::path &child, const fs::path &parent)
  {
    std::error_code ec;

    const fs::path childPath =
        fs::weakly_canonical(child, ec);

    if (ec)
    {
      return false;
    }

    const fs::path parentPath =
        fs::weakly_canonical(parent, ec);

    if (ec)
    {
      return false;
    }

    auto childIt = childPath.begin();
    auto parentIt = parentPath.begin();

    for (; parentIt != parentPath.end(); ++parentIt, ++childIt)
    {
      if (childIt == childPath.end() || *childIt != *parentIt)
      {
        return false;
      }
    }

    return true;
  }

  int copy_path_recursive(
      const DesktopReporter &out,
      const fs::path &source,
      const fs::path &destination)
  {
    std::error_code ec;

    if (!fs::exists(source, ec) || ec)
    {
      return 0;
    }

    if (fs::is_regular_file(source, ec) && !ec)
    {
      fs::create_directories(destination.parent_path(), ec);

      if (ec)
      {
        out.error("Unable to create resource directory: " + ec.message());
        return 1;
      }

      fs::copy_file(
          source,
          destination,
          fs::copy_options::overwrite_existing,
          ec);

      if (ec)
      {
        out.error("Unable to copy resource file: " + source.string());
        out.error_hint(ec.message());
        return 1;
      }

      return 0;
    }

    if (!fs::is_directory(source, ec) || ec)
    {
      return 0;
    }

    fs::create_directories(destination, ec);

    if (ec)
    {
      out.error("Unable to create resource directory: " + ec.message());
      return 1;
    }

    for (auto it = fs::recursive_directory_iterator(
             source,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      const fs::path current = it->path();

      std::error_code relEc;
      const fs::path relative =
          fs::relative(current, source, relEc);

      if (relEc)
      {
        continue;
      }

      const fs::path target = destination / relative;

      if (it->is_directory(ec) && !ec)
      {
        fs::create_directories(target, ec);

        if (ec)
        {
          out.error("Unable to create resource subdirectory: " + target.string());
          out.error_hint(ec.message());
          return 1;
        }

        continue;
      }

      if (!it->is_regular_file(ec) || ec)
      {
        continue;
      }

      fs::create_directories(target.parent_path(), ec);

      if (ec)
      {
        out.error("Unable to create resource parent directory: " + ec.message());
        return 1;
      }

      fs::copy_file(
          current,
          target,
          fs::copy_options::overwrite_existing,
          ec);

      if (ec)
      {
        out.error("Unable to copy resource: " + current.string());
        out.error_hint(ec.message());
        return 1;
      }
    }

    if (ec)
    {
      out.error("Unable to copy resource directory: " + source.string());
      out.error_hint(ec.message());
      return 1;
    }

    return 0;
  }

  int copy_desktop_runtime_resources(
      const DesktopReporter &out,
      const DesktopOptions &options,
      const fs::path &bundleDir,
      const fs::path &serverExecutable)
  {
    const fs::path baseDir =
        resolve_desktop_resource_base_directory(options, serverExecutable);

    std::vector<fs::path> sources;

    if (options.autoResources)
    {
      for (const std::string &name : default_desktop_resource_names())
      {
        const fs::path source = baseDir / name;

        std::error_code ec;
        if (fs::exists(source, ec) && !ec)
        {
          sources.push_back(source);
        }
      }
    }

    for (const std::string &resourcePath : options.resourcePaths)
    {
      fs::path source = resourcePath;

      if (source.is_relative())
      {
        source = fs::current_path() / source;
      }

      source = fs::absolute(source).lexically_normal();

      std::error_code ec;
      if (!fs::exists(source, ec) || ec)
      {
        out.error("Resource path not found: " + source.string());
        return 1;
      }

      bool duplicate = false;

      for (const fs::path &existing : sources)
      {
        if (same_path(existing, source))
        {
          duplicate = true;
          break;
        }
      }

      if (!duplicate)
      {
        sources.push_back(source);
      }
    }

    for (const fs::path &source : sources)
    {
      if (path_is_inside(source, bundleDir))
      {
        continue;
      }

      const fs::path destination =
          bundleDir / source.filename();

      const int copyCode =
          copy_path_recursive(out, source, destination);

      if (copyCode != 0)
      {
        return copyCode;
      }

      out.step("resource: " + source.filename().string());
    }

    return 0;
  }

  int build_desktop_bundle(const DesktopReporter &out, const DesktopOptions &options)
  {
    if (options.packageTarget != "dir")
    {
      out.error("Unsupported desktop package target: " + options.packageTarget);
      out.error_hint("Supported now: --target dir");
      out.error_hint("AppImage, deb and rpm should be added as separate packaging backends.");
      return 1;
    }

    fs::path serverExecutable;

    const int serverCode = build_server_executable(out, options, serverExecutable);

    if (serverCode != 0)
    {
      return serverCode;
    }

    const auto selfPath = current_executable_path();

    if (!selfPath)
    {
      out.error("Unable to locate current vix executable for bundling.");
      out.error_hint("The first desktop package backend currently supports Linux.");
      return 1;
    }

    const fs::path bundleDir = resolve_output_directory(options);
    const fs::path binDir = bundleDir / "bin";
    const fs::path resourcesDir = bundleDir / "resources";

    std::error_code ec;

    fs::create_directories(binDir, ec);
    if (ec)
    {
      out.error("Unable to create bundle bin directory: " + ec.message());
      return 1;
    }

    fs::create_directories(resourcesDir, ec);
    if (ec)
    {
      out.error("Unable to create bundle resources directory: " + ec.message());
      return 1;
    }

    out.banner("Vix Desktop \u00b7 package");
    out.row("app", options.name);
    out.row("out", bundleDir.string(), out.theme.cyan());

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
      out.error("Unable to copy server executable: " + ec.message());
      return 1;
    }

    fs::copy_file(
        *selfPath,
        bundledVix,
        fs::copy_options::overwrite_existing,
        ec);

    if (ec)
    {
      out.error("Unable to copy vix desktop runtime: " + ec.message());
      return 1;
    }

    make_executable(bundledServer);
    make_executable(bundledVix);

    const int resourcesCode =
        copy_desktop_runtime_resources(
            out,
            options,
            bundleDir,
            serverExecutable);

    if (resourcesCode != 0)
    {
      return resourcesCode;
    }

    std::string iconName;
    const int iconCode = copy_optional_icon(out, options, resourcesDir, iconName);

    if (iconCode != 0)
    {
      return iconCode;
    }

    const std::string launcherName = appSlug;
    const std::string desktopFileName = appSlug + ".desktop";

    const int manifestCode =
        write_desktop_manifest(
            out,
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
            out,
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
            out,
            options,
            bundleDir,
            launcherName,
            desktopFileName,
            iconName);

    if (desktopCode != 0)
    {
      return desktopCode;
    }

    out.event("packaged", "out", bundleDir.string());
    out.success("Desktop bundle created.");
    out.row("run", (bundleDir / launcherName).string(), out.theme.cyan());

    return 0;
  }

  int run_desktop_command(
      DesktopAction action,
      const std::vector<std::string> &args)
  {
    DesktopOptions options;
    options.action = action;

    // First-pass reporter so parse errors are styled with sensible defaults.
    DesktopReporter out;
    out.theme.color = desk_detect_color(std::cout);

    const int parseResult = parse_options(out, args, options);

    if (parseResult == 2)
    {
      return vix::commands::DesktopCommand::help();
    }

    if (parseResult != 0)
    {
      return parseResult;
    }

    // Apply final verbosity / color now that flags are known.
    out.mode = options.logMode;
    out.theme.color =
        (options.colorOverride == -1) ? desk_detect_color(std::cout)
                                      : (options.colorOverride == 1);

    if (!desktop_shell_backend_available())
    {
      out.error("Desktop shell unavailable.");
      return 1;
    }

    if (action == DesktopAction::Run)
    {
      return run_desktop_app(out, std::move(options));
    }

    return build_desktop_bundle(out, options);
  }
}

#endif // VIX_CLI_HAS_UI

namespace vix::commands
{
  int DesktopCommand::run(const std::vector<std::string> &args)
  {
    if (!args.empty() && is_help_arg(args[0]))
    {
      return help();
    }

#ifdef VIX_CLI_HAS_UI
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

    return run_desktop_command(action, commandArgs);
#else
    DesktopReporter out;
    out.theme.color = desk_detect_color(std::cout);

    out.error("Desktop shell unavailable.");
    out.error_hint("Build with desktop WebView enabled.");

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
        << "  vix desktop run main.cpp --port 8080\n"
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
        << "  --resizable                 Allow resizing (default)\n"
        << "  --no-resizable              Disable resizing\n"
        << "  --devtools                  Enable WebView developer tools when supported\n"
        << "  --no-devtools               Disable developer tools (default)\n\n"

        << "Server options:\n"
        << "  --url <url>                 Target URL to open\n"
        << "  --host <host>               Local host. Default: 127.0.0.1\n"
        << "  --port <port>               Local port. Default: 8080\n"
        << "  --readiness-url <url>       URL checked before opening the shell\n"
        << "  --server <command>          Dev mode server command\n"
        << "  --cwd <dir>                 Working directory for --server\n"
        << "  --working-directory <dir>   Same as --cwd\n"
        << "  --wait                      Wait for server readiness (default)\n"
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
        << "  --resources <path>          Copy an extra runtime resource file or directory\n"
        << "  --resource <path>           Same as --resources\n"
        << "  --no-auto-resources         Do not auto-copy templates/assets/static/public/views/resources\n"
        << "  --local-cache               Use local .vix-scripts cache\n\n"

        << "Output options:\n"
        << "  --quiet, -q                 Only print errors\n"
        << "  --json                      Emit machine-readable lifecycle events\n"
        << "  --no-color                  Disable ANSI colors (also honors NO_COLOR)\n\n";

    return 0;
  }
}
