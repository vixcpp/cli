/**
 *
 *  @file CloudCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/CloudCommand.hpp>
#include <vix/cli/cmake/CMakeBuild.hpp>
#include <vix/cli/util/Hash.hpp>
#include <vix/requests/requests.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <new>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  namespace
  {
    constexpr const char *default_cloud_url = "https://api.softadastra.com";
    constexpr const char *legacy_default_cloud_url = "http://127.0.0.1:8080";
    constexpr const char *frontend_cloud_url = "https://cloud.softadastra.com";

    struct GlobalCloudConfig
    {
      std::string cloud_url;
      std::string session_id;
      std::string user_id;
      std::string email;
      std::string display_name;
    };

    struct ProjectCloudConfig
    {
      std::string cloud_url;
      std::string workspace_id;
      std::string project_id;
      std::string workspace_name;
      std::string project_name;
    };

    struct ApiResult
    {
      bool ok{false};
      int status{0};
      json data = json::object();
      std::string error;
      std::string message;
    };

    struct CloudContext
    {
      GlobalCloudConfig global;
      ProjectCloudConfig project;
      std::string cloud_url;
    };

    struct CloudPublishOptions
    {
      std::string package_name;
      std::string version;
      std::string visibility{"private"};
      std::string description;
      std::string repository_url;
      fs::path archive_path;
      fs::path manifest_path;
      bool dry_run{false};
      bool json_output{false};
      bool help{false};
    };

    struct PreparedArchive
    {
      fs::path path;
      bool generated{false};
      std::uintmax_t size{0};
      std::string checksum_sha256;
    };

    std::string trim_copy(std::string s)
    {
      auto isws = [](unsigned char c)
      { return std::isspace(c) != 0; };
      while (!s.empty() && isws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
      while (!s.empty() && isws(static_cast<unsigned char>(s.back())))
        s.pop_back();
      return s;
    }

    std::string lower_copy(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    std::string strip_trailing_slash(std::string value)
    {
      while (value.size() > 1 && value.back() == '/')
        value.pop_back();
      return value;
    }

    std::string normalize_cloud_url(std::string value)
    {
      value = strip_trailing_slash(value);
      if (value.empty() || value == legacy_default_cloud_url || value == frontend_cloud_url)
        return default_cloud_url;
      return value;
    }

    namespace terminal
    {
      constexpr const char *reset = "\033[0m";
      constexpr const char *bold = "\033[1m";

      constexpr const char *red = "\033[31m";
      constexpr const char *green = "\033[32m";
      constexpr const char *yellow = "\033[33m";
      constexpr const char *cyan = "\033[36m";
      constexpr const char *white = "\033[97m";

      constexpr const char *dim = "\033[37m";

      bool stream_is_terminal(std::ostream &stream)
      {
        int descriptor = -1;
        if (&stream == &std::cout)
#ifdef _WIN32
          descriptor = _fileno(stdout);
#else
          descriptor = fileno(stdout);
#endif
        else if (&stream == &std::cerr)
#ifdef _WIN32
          descriptor = _fileno(stderr);
#else
          descriptor = fileno(stderr);
#endif

        if (descriptor < 0)
          return false;

#ifdef _WIN32
        return _isatty(descriptor) != 0;
#else
        return isatty(descriptor) != 0;
#endif
      }

#ifdef _WIN32
      void enable_virtual_terminal(std::ostream &stream)
      {
        const DWORD standard_handle = (&stream == &std::cerr) ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
        HANDLE handle = GetStdHandle(standard_handle);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
          return;

        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode) == 0)
          return;
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
      }
#endif

      bool color_enabled(std::ostream &stream)
      {
        const char *no_color = vix::utils::vix_getenv("NO_COLOR");
        if (no_color && *no_color)
          return false;

        const char *term = vix::utils::vix_getenv("TERM");
        if (term && std::string(term) == "dumb")
          return false;

        if (!stream_is_terminal(stream))
          return false;

#ifdef _WIN32
        static bool stdout_enabled = false;
        static bool stderr_enabled = false;
        bool &enabled = (&stream == &std::cerr) ? stderr_enabled : stdout_enabled;
        if (!enabled)
        {
          enable_virtual_terminal(stream);
          enabled = true;
        }
#endif
        return true;
      }

      void code(std::ostream &stream, const char *value)
      {
        if (color_enabled(stream))
          stream << value;
      }

      void symbol(std::ostream &stream, const char *glyph, const char *color)
      {
        code(stream, color);
        stream << glyph;
        code(stream, reset);
      }

      void header(const std::string &title, const std::string &subtitle = {})
      {
        symbol(std::cout, "◆", cyan);
        std::cout << " ";
        code(std::cout, bold);
        code(std::cout, cyan);
        std::cout << title;
        code(std::cout, reset);
        std::cout << "\n";

        if (!subtitle.empty())
          std::cout << "  " << subtitle << "\n";
      }

      void field(const std::string &label, const std::string &value)
      {
        if (value.empty())
          return;

        const auto flags = std::cout.flags();
        const auto fill = std::cout.fill();

        std::cout << "  ";
        std::cout << std::left << std::setw(16) << label;

        std::cout.flags(flags);
        std::cout.fill(fill);

        code(std::cout, bold);
        code(std::cout, white);
        std::cout << value;
        code(std::cout, reset);
        std::cout << "\n";
      }
      void status(std::ostream &stream, const char *glyph, const char *color, const std::string &message)
      {
        stream << "  ";
        symbol(stream, glyph, color);
        stream << " " << message << "\n";
      }

      void success(const std::string &message)
      {
        status(std::cout, "✓", green, message);
      }

      void error(const std::string &message)
      {
        status(std::cerr, "×", red, message);
      }

      void warning(const std::string &message)
      {
        status(std::cout, "!", yellow, message);
      }

      void hint(const std::string &message)
      {
        std::cout << "  ";
        symbol(std::cout, "→", cyan);
        std::cout << " " << message << "\n";
      }

      void progress(const std::string &message)
      {
        std::cout << "  ";
        symbol(std::cout, "●", cyan);
        std::cout << " " << message << "\n";
      }

      void section(const std::string &title)
      {
        std::cout << "\n  ";
        code(std::cout, bold);
        std::cout << title;
        code(std::cout, reset);
        std::cout << "\n";
      }

      void command(const std::string &usage, const std::string &description = {})
      {
        std::cout << "  ";
        code(std::cout, cyan);
        std::cout << usage;
        code(std::cout, reset);
        if (!description.empty())
        {
          std::cout << "\n      ";
          code(std::cout, dim);
          std::cout << description;
          code(std::cout, reset);
        }
        std::cout << "\n";
      }

      void check(const std::string &label, bool ok, const std::string &detail)
      {
        const auto flags = std::cout.flags();
        const auto fill = std::cout.fill();
        std::cout << "  ";
        symbol(std::cout, ok ? "✓" : "×", ok ? green : red);
        std::cout << " " << std::left << std::setw(18) << label;
        std::cout.flags(flags);
        std::cout.fill(fill);
        code(std::cout, ok ? dim : yellow);
        std::cout << detail;
        code(std::cout, reset);
        std::cout << "\n";
      }
    }

    void cloud_header(const std::string &title, const std::string &subtitle = {})
    {
      terminal::header(title, subtitle);
    }

    void cloud_step(const std::string &label, const std::string &value)
    {
      if (value.empty())
        return;
      terminal::field(label, value);
    }

    void cloud_success(const std::string &message)
    {
      terminal::success(message);
    }

    void cloud_error(const std::string &message)
    {
      terminal::error(message);
    }

    void cloud_hint(const std::string &message)
    {
      terminal::hint(message);
    }

    std::string cloud_bad_alloc_message()
    {
      return "Softadastra Cloud returned a response the CLI could not buffer. Check that the Cloud API endpoint is serving JSON. The API URL is https://api.softadastra.com.";
    }

    std::string slugify(std::string value)
    {
      std::string out;
      bool dash = false;
      for (char ch : value)
      {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c))
        {
          out.push_back(static_cast<char>(std::tolower(c)));
          dash = false;
        }
        else if (!dash && !out.empty())
        {
          out.push_back('-');
          dash = true;
        }
      }
      while (!out.empty() && out.back() == '-')
        out.pop_back();
      return out.empty() ? "project" : out;
    }

    fs::path home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      return home && *home ? fs::path(home) : fs::current_path();
    }

    fs::path global_config_path()
    {
      return home_dir() / ".vix" / "cloud" / "config.json";
    }

    fs::path project_config_path()
    {
      return fs::current_path() / ".vix" / "cloud.json";
    }

    bool has_flag(const std::vector<std::string> &args, const std::string &name)
    {
      return std::find(args.begin(), args.end(), name) != args.end();
    }

    std::optional<std::string> arg_value(const std::vector<std::string> &args, const std::string &name)
    {
      const std::string prefix = name + "=";
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        if (args[i] == name && i + 1 < args.size())
          return args[i + 1];
        if (args[i].rfind(prefix, 0) == 0)
          return args[i].substr(prefix.size());
      }
      return std::nullopt;
    }

    std::string prompt_line(const std::string &label, const std::string &fallback = {})
    {
      terminal::symbol(std::cout, "?", terminal::cyan);
      std::cout << " " << label;
      if (!fallback.empty())
      {
        std::cout << " ";
        terminal::code(std::cout, terminal::dim);
        std::cout << "[" << fallback << "]";
        terminal::code(std::cout, terminal::reset);
      }
      std::cout << ": " << std::flush;

      std::string value;
      std::getline(std::cin, value);
      value = trim_copy(value);
      return value.empty() ? fallback : value;
    }

    std::string prompt_password()
    {
      terminal::symbol(std::cout, "?", terminal::cyan);
      std::cout << " Password: " << std::flush;
      std::string password;
#ifndef _WIN32
      termios oldt{};
      if (tcgetattr(STDIN_FILENO, &oldt) == 0)
      {
        termios newt = oldt;
        newt.c_lflag &= static_cast<unsigned int>(~ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        std::getline(std::cin, password);
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cout << "\n";
        return password;
      }
#endif
      std::getline(std::cin, password);
      return password;
    }

    bool read_json_file(const fs::path &path, json &out)
    {
      std::ifstream in(path);
      if (!in)
        return false;
      try
      {
        in >> out;
        return true;
      }
      catch (...)
      {
        return false;
      }
    }

    std::optional<std::string> read_text_file(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return std::nullopt;
      return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::optional<std::vector<unsigned char>> read_binary_file(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return std::nullopt;
      return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    bool write_json_file(const fs::path &path, const json &value)
    {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);
      std::ofstream out(path);
      if (!out)
        return false;
      out << value.dump(2) << "\n";
      return true;
    }

    std::optional<GlobalCloudConfig> load_global_config()
    {
      json value;
      if (!read_json_file(global_config_path(), value) || !value.is_object())
        return std::nullopt;
      GlobalCloudConfig cfg;
      cfg.cloud_url = normalize_cloud_url(value.value("cloud_url", default_cloud_url));
      cfg.session_id = value.value("session_id", "");
      if (value.contains("user") && value["user"].is_object())
      {
        cfg.user_id = value["user"].value("id", "");
        cfg.email = value["user"].value("email", "");
        cfg.display_name = value["user"].value("display_name", value["user"].value("name", ""));
      }
      return cfg;
    }

    bool save_global_config(const GlobalCloudConfig &cfg)
    {
      return write_json_file(global_config_path(), json{
                                                       {"cloud_url", cfg.cloud_url},
                                                       {"session_id", cfg.session_id},
                                                       {"user", {{"id", cfg.user_id}, {"email", cfg.email}, {"display_name", cfg.display_name}}}});
    }

    std::optional<ProjectCloudConfig> load_project_config()
    {
      json value;
      if (!read_json_file(project_config_path(), value) || !value.is_object())
        return std::nullopt;
      ProjectCloudConfig cfg;
      cfg.cloud_url = normalize_cloud_url(value.value("cloud_url", default_cloud_url));
      cfg.workspace_id = value.value("workspace_id", "");
      cfg.project_id = value.value("project_id", "");
      cfg.workspace_name = value.value("workspace_name", "");
      cfg.project_name = value.value("project_name", "");
      return cfg;
    }

    bool save_project_config(const ProjectCloudConfig &cfg)
    {
      return write_json_file(project_config_path(), json{
                                                        {"cloud_url", cfg.cloud_url},
                                                        {"workspace_id", cfg.workspace_id},
                                                        {"project_id", cfg.project_id},
                                                        {"workspace_name", cfg.workspace_name},
                                                        {"project_name", cfg.project_name}});
    }

    vix::requests::RequestOptions request_options(const std::optional<GlobalCloudConfig> &cfg = std::nullopt)
    {
      vix::requests::RequestOptions options;
      options.timeout = vix::requests::Timeout(
          std::chrono::seconds(10),
          std::chrono::seconds(20),
          std::chrono::seconds(30));
      options.headers.set("Accept", "application/json");
      if (cfg && !cfg->session_id.empty())
      {
        options.headers.set("Authorization", "Bearer " + cfg->session_id);
        options.headers.set("X-Session-Id", cfg->session_id);
      }
      return options;
    }

    ApiResult parse_response(const vix::requests::Response &response)
    {
      ApiResult result;
      result.status = response.status_code();
      json payload;
      const auto contentType = response.content_type().value_or("");
      if (!response.text().empty() &&
          contentType.find("application/json") == std::string::npos &&
          contentType.find("+json") == std::string::npos)
      {
        result.error = "invalid_cloud_response";
        result.message = "Softadastra Cloud API returned " +
                         (contentType.empty() ? std::string("a non-JSON response") : ("Content-Type " + contentType)) +
                         ". Check that the CLI is using https://api.softadastra.com, not the frontend domain.";
        return result;
      }

      try
      {
        payload = response.text().empty() ? json::object() : json::parse(response.text());
      }
      catch (...)
      {
        result.error = "invalid_cloud_response";
        result.message = "Softadastra Cloud API returned invalid JSON. Check that the CLI is using https://api.softadastra.com, not the frontend domain.";
        return result;
      }
      result.ok = response.ok() && payload.value("ok", false);
      if (payload.contains("data"))
        result.data = payload["data"];
      result.error = payload.value("error", response.ok() ? std::string{} : "http_error");
      result.message = payload.value("message", response.ok() ? std::string{} : ("HTTP " + std::to_string(response.status_code())));
      return result;
    }

    ApiResult api_post(const std::string &cloud_url, const std::string &path, const json &body, const std::optional<GlobalCloudConfig> &cfg = std::nullopt)
    {
      try
      {
        vix::requests::Client client;
        const auto response = client.post(strip_trailing_slash(cloud_url) + path, vix::requests::json_body(body.dump()), request_options(cfg));
        return parse_response(response);
      }
      catch (const std::bad_alloc &)
      {
        return ApiResult{false, 0, json::object(), "network_error", cloud_bad_alloc_message()};
      }
      catch (const std::exception &ex)
      {
        return ApiResult{false, 0, json::object(), "network_error", ex.what()};
      }
    }

    ApiResult api_post_binary(const std::string &cloud_url, const std::string &path, std::vector<unsigned char> body, const std::optional<GlobalCloudConfig> &cfg = std::nullopt)
    {
      try
      {
        vix::requests::Client client;
        auto options = request_options(cfg);
        options.headers.set("Content-Type", "application/gzip");
        const auto response = client.post(strip_trailing_slash(cloud_url) + path, vix::requests::binary_body(std::move(body), "application/gzip"), options);
        return parse_response(response);
      }
      catch (const std::bad_alloc &)
      {
        return ApiResult{false, 0, json::object(), "network_error", cloud_bad_alloc_message()};
      }
      catch (const std::exception &ex)
      {
        return ApiResult{false, 0, json::object(), "network_error", ex.what()};
      }
    }

    ApiResult api_get(const std::string &cloud_url, const std::string &path, const std::optional<GlobalCloudConfig> &cfg = std::nullopt)
    {
      try
      {
        vix::requests::Client client;
        const auto response = client.get(strip_trailing_slash(cloud_url) + path, request_options(cfg));
        return parse_response(response);
      }
      catch (const std::bad_alloc &)
      {
        return ApiResult{false, 0, json::object(), "network_error", cloud_bad_alloc_message()};
      }
      catch (const std::exception &ex)
      {
        return ApiResult{false, 0, json::object(), "network_error", ex.what()};
      }
    }

    std::string api_error_text(const ApiResult &result)
    {
      if (!result.error.empty() && !result.message.empty())
        return result.error + ": " + result.message;
      if (!result.message.empty())
        return result.message;
      if (!result.error.empty())
        return result.error;
      if (result.status > 0)
        return "HTTP " + std::to_string(result.status);
      return "Cloud request failed.";
    }

    std::string permission_error_text(const ApiResult &result, const std::string &permissionMessage)
    {
      if (result.status == 401)
        return "Authentication failed. Run vix login again.";
      if (result.status == 403)
        return permissionMessage;
      if (result.status == 404)
        return "Linked workspace or project was not found.";
      return api_error_text(result);
    }

    void print_api_error(const ApiResult &result)
    {
      if (result.status == 403)
      {
        cloud_error("You do not have permission to perform this action in this workspace.");
        return;
      }

      cloud_error(result.message.empty() ? "Cloud request failed." : result.message);
      if (!result.error.empty())
        cloud_step("Error", result.error);
      if (result.status > 0)
        cloud_step("HTTP status", std::to_string(result.status));
      if (result.status == 401)
        cloud_hint("Run: vix login");
    }

    std::optional<GlobalCloudConfig> require_connected()
    {
      auto cfg = load_global_config();
      if (!cfg || cfg->session_id.empty())
      {
        cloud_error("Not connected to Softadastra Cloud.");
        cloud_hint("Run: vix login");
        return std::nullopt;
      }
      return cfg;
    }

    std::optional<CloudContext> load_cloud_context(std::string &message)
    {
      auto global = load_global_config();
      if (!global || global->cloud_url.empty() || global->session_id.empty())
      {
        message = "Authentication failed. Run vix login again.";
        return std::nullopt;
      }

      if (global->user_id.empty())
      {
        auto me = api_post(global->cloud_url, "/api/auth/me", json{{"session_id", global->session_id}}, global);
        if (!me.ok)
        {
          message = permission_error_text(me, "Authentication failed. Run vix login again.");
          return std::nullopt;
        }
        const auto user = me.data.value("user", json::object());
        global->user_id = user.value("id", "");
        global->email = user.value("email", global->email);
        global->display_name = user.value("display_name", global->display_name);
      }

      auto project = load_project_config();
      if (!project || project->workspace_id.empty() || project->project_id.empty() || project->cloud_url.empty())
      {
        message = "This project is not linked to Softadastra Cloud. Run vix cloud init first.";
        return std::nullopt;
      }

      if (global->user_id.empty())
      {
        message = "Authentication failed. Run vix login again.";
        return std::nullopt;
      }

      CloudContext ctx;
      ctx.global = *global;
      ctx.project = *project;
      ctx.cloud_url = strip_trailing_slash(project->cloud_url.empty() ? global->cloud_url : project->cloud_url);
      return ctx;
    }

    bool executable_available(const std::string &exe)
    {
      std::string output;
      const auto result = vix::cli::build::run_process_capture({exe, "--version"}, {}, output);
      return result.exitCode == 0;
    }

    std::string git_value(const std::vector<std::string> &argv)
    {
      std::string output;
      const auto result = vix::cli::build::run_process_capture(argv, {}, output);
      if (result.exitCode != 0)
        return {};
      return trim_copy(output);
    }

    std::string json_error_output(const std::string &error, const std::string &message)
    {
      return json{{"ok", false}, {"error", error}, {"message", message}}.dump(2);
    }

    int print_cloud_publish_error(const CloudPublishOptions &opt, const std::string &error, const std::string &message)
    {
      if (opt.json_output)
      {
        std::cout << json_error_output(error, message) << "\n";
      }
      else
      {
        cloud_error(message);
        if (!error.empty())
          cloud_step("Error", error);
      }
      return 1;
    }

    CloudPublishOptions parse_cloud_publish_options(const std::vector<std::string> &args)
    {
      CloudPublishOptions opt;
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];
        auto take = [&](const std::string &name) -> std::string
        {
          if (i + 1 >= args.size())
            throw std::runtime_error(name + " requires a value");
          ++i;
          return args[i];
        };

        if (a == "--help" || a == "-h")
          opt.help = true;
        else if (a == "--dry-run")
          opt.dry_run = true;
        else if (a == "--json")
          opt.json_output = true;
        else if (a == "--package")
          opt.package_name = take("--package");
        else if (a.rfind("--package=", 0) == 0)
          opt.package_name = a.substr(std::string("--package=").size());
        else if (a == "--version")
          opt.version = take("--version");
        else if (a.rfind("--version=", 0) == 0)
          opt.version = a.substr(std::string("--version=").size());
        else if (a == "--visibility")
          opt.visibility = take("--visibility");
        else if (a.rfind("--visibility=", 0) == 0)
          opt.visibility = a.substr(std::string("--visibility=").size());
        else if (a == "--description")
          opt.description = take("--description");
        else if (a.rfind("--description=", 0) == 0)
          opt.description = a.substr(std::string("--description=").size());
        else if (a == "--repository-url")
          opt.repository_url = take("--repository-url");
        else if (a.rfind("--repository-url=", 0) == 0)
          opt.repository_url = a.substr(std::string("--repository-url=").size());
        else if (a == "--archive")
          opt.archive_path = fs::path(take("--archive"));
        else if (a.rfind("--archive=", 0) == 0)
          opt.archive_path = fs::path(a.substr(std::string("--archive=").size()));
        else if (a == "--manifest")
          opt.manifest_path = fs::path(take("--manifest"));
        else if (a.rfind("--manifest=", 0) == 0)
          opt.manifest_path = fs::path(a.substr(std::string("--manifest=").size()));
        else if (!a.empty())
          throw std::runtime_error("unknown cloud publish flag: " + a);
      }
      return opt;
    }

    std::string toml_like_value(const std::string &content, const std::string &key)
    {
      std::istringstream in(content);
      std::string line;
      const std::string prefix = key + " =";
      while (std::getline(in, line))
      {
        line = trim_copy(line);
        if (line.rfind(prefix, 0) != 0)
          continue;
        std::string value = trim_copy(line.substr(prefix.size()));
        if (!value.empty() && value.front() == '"')
          value.erase(value.begin());
        if (!value.empty() && value.back() == '"')
          value.pop_back();
        return value;
      }
      return {};
    }

    json load_optional_json(const fs::path &path)
    {
      json value;
      if (read_json_file(path, value) && value.is_object())
        return value;
      return json::object();
    }

    void fill_package_metadata_from_files(CloudPublishOptions &opt, json &manifest)
    {
      const fs::path root = fs::current_path();
      const fs::path vix_json_path = root / "vix.json";
      const fs::path vix_app_path = root / "vix.app";

      json vix_json = load_optional_json(vix_json_path);
      if (!vix_json.empty())
      {
        manifest["vix_json"] = vix_json;
        const std::string name = vix_json.value("name", "");
        const std::string ns = vix_json.value("namespace", "");
        if (opt.package_name.empty() && !name.empty())
          opt.package_name = ns.empty() ? name : (ns + "/" + name);
        if (opt.version.empty())
          opt.version = vix_json.value("version", "");
        if (opt.description.empty())
          opt.description = vix_json.value("description", "");
        if (opt.repository_url.empty())
          opt.repository_url = vix_json.value("repository", vix_json.value("repository_url", ""));
      }

      if (auto content = read_text_file(vix_app_path))
      {
        const std::string app_name = toml_like_value(*content, "name");
        const std::string app_version = toml_like_value(*content, "version");
        json app_info = json::object();
        if (!app_name.empty())
          app_info["name"] = app_name;
        if (!app_version.empty())
          app_info["version"] = app_version;
        if (!app_info.empty())
          manifest["vix_app"] = app_info;
        if (opt.package_name.empty() && !app_name.empty())
          opt.package_name = app_name;
        if (opt.version.empty() && !app_version.empty())
          opt.version = app_version;
      }
    }

    std::string latest_git_semver_version()
    {
      std::string output;
      const auto result = vix::cli::build::run_process_capture({"git", "tag", "--list", "v[0-9]*", "--sort=-v:refname"}, {}, output);
      if (result.exitCode != 0)
        return {};
      std::istringstream in(output);
      std::string line;
      while (std::getline(in, line))
      {
        line = trim_copy(line);
        if (line.size() > 1 && line[0] == 'v')
          return line.substr(1);
      }
      return {};
    }

    fs::path make_temp_archive_path()
    {
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      fs::path dir = fs::temp_directory_path() / ("vix-cloud-publish-" + std::to_string(now));
      std::error_code ec;
      fs::create_directories(dir, ec);
      return dir / "package.tar.gz";
    }

    std::optional<PreparedArchive> prepare_archive(const CloudPublishOptions &opt, std::string &message)
    {
      PreparedArchive archive;
      if (!opt.archive_path.empty())
      {
        archive.path = fs::absolute(opt.archive_path);
        archive.generated = false;
        if (!fs::exists(archive.path) || !fs::is_regular_file(archive.path))
        {
          message = "Archive file not found: " + archive.path.string();
          return std::nullopt;
        }
      }
      else
      {
#if defined(_WIN32)
        if (!executable_available("tar"))
        {
          message = "Cloud publish archive creation is not available on this platform yet. Pass --archive <path>.";
          return std::nullopt;
        }
#endif
        if (!executable_available("tar"))
        {
          message = "tar is required to create a package archive. Pass --archive <path>.";
          return std::nullopt;
        }
        archive.path = make_temp_archive_path();
        archive.generated = true;
        std::vector<std::string> argv{
            "tar", "-czf", archive.path.string(),
            "-C", fs::current_path().string(),
            "--exclude=.git", "--exclude=build", "--exclude=build-*", "--exclude=.vix",
            "--exclude=node_modules", "--exclude=.cache", "--exclude=tmp",
            "--exclude=*.o", "--exclude=*.a", "--exclude=*.so", "--exclude=*.dll", "--exclude=*.exe",
            "--exclude=database.sqlite", "--exclude=storage", "."};
        std::string output;
        const auto result = vix::cli::build::run_process_capture(argv, {}, output);
        if (result.exitCode != 0)
        {
          message = output.empty() ? "Could not create package archive." : trim_copy(output);
          return std::nullopt;
        }
      }

      std::error_code ec;
      archive.size = fs::file_size(archive.path, ec);
      if (ec)
      {
        message = "Unable to read archive size.";
        return std::nullopt;
      }
      auto checksum = vix::cli::util::sha256_file(archive.path);
      if (!checksum)
      {
        message = "Unable to calculate archive checksum.";
        return std::nullopt;
      }
      archive.checksum_sha256 = *checksum;
      return archive;
    }

    json build_cloud_manifest(CloudPublishOptions &opt, const CloudContext &ctx)
    {
      json manifest = json::object();
      if (!opt.manifest_path.empty())
      {
        json provided;
        if (read_json_file(opt.manifest_path, provided) && provided.is_object())
          manifest = provided;
      }
      fill_package_metadata_from_files(opt, manifest);
      manifest["name"] = opt.package_name;
      manifest["version"] = opt.version;
      manifest["project_id"] = ctx.project.project_id;
      manifest["workspace_id"] = ctx.project.workspace_id;
      manifest["source"] = "vix";
      manifest["generated_by"] = "vix-cli";
      return manifest;
    }

    std::optional<json> find_package_by_name(const CloudContext &ctx, const std::string &name, ApiResult *lastResult = nullptr)
    {
      auto listed = api_post(ctx.cloud_url, "/api/packages/list", json{{"workspace_id", ctx.project.workspace_id}}, ctx.global);
      if (lastResult)
        *lastResult = listed;
      if (!listed.ok)
        return std::nullopt;
      const auto packages = listed.data.value("packages", json::array());
      for (const auto &pkg : packages)
      {
        if (pkg.value("name", "") == name)
          return std::optional<json>{pkg};
      }
      return std::nullopt;
    }

    std::string publish_permission_message(const ApiResult &result)
    {
      if (result.status == 401)
        return "Authentication failed. Run vix login again.";
      if (result.status == 403)
        return "You do not have permission to publish packages in this workspace.";
      if (result.status == 404)
        return "Linked workspace or project was not found. Run vix cloud init again.";
      if (result.status == 409 && result.error == "package_version_already_exists")
        return "This package version already exists in Softadastra Cloud. Use a new version.";
      return api_error_text(result);
    }

    std::optional<json> choose_item(const json &items, const std::string &label)
    {
      if (!items.is_array() || items.empty())
        return std::nullopt;

      terminal::section(label);
      for (std::size_t i = 0; i < items.size(); ++i)
      {
        const std::string name = items[i].value("name", "");
        const std::string id = items[i].value("id", "");

        std::cout << "  ";
        terminal::code(std::cout, terminal::cyan);
        std::cout << (i + 1) << ".";
        terminal::code(std::cout, terminal::reset);
        std::cout << " ";
        terminal::code(std::cout, terminal::bold);
        std::cout << (name.empty() ? id : name);
        terminal::code(std::cout, terminal::reset);
        if (!name.empty() && !id.empty())
        {
          std::cout << "  ";
          terminal::code(std::cout, terminal::dim);
          std::cout << id;
          terminal::code(std::cout, terminal::reset);
        }
        std::cout << "\n";
      }

      const std::string answer = prompt_line("Choose number");
      try
      {
        const std::size_t index = static_cast<std::size_t>(std::stoul(answer));
        if (index >= 1 && index <= items.size())
          return std::optional<json>{items[index - 1]};
      }
      catch (...)
      {
      }

      terminal::warning("No valid selection was made.");
      return std::nullopt;
    }


    std::string url_encode(const std::string &value)
    {
      std::ostringstream out;
      out << std::uppercase << std::hex;
      for (const char raw : value)
      {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) != 0 || raw == '-' || raw == '_' || raw == '.' || raw == '~')
        {
          out << raw;
        }
        else
        {
          out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
      }
      return out.str();
    }

    std::string url_decode(const std::string &value)
    {
      std::string out;
      out.reserve(value.size());
      for (std::size_t i = 0; i < value.size(); ++i)
      {
        if (value[i] == '%' && i + 2 < value.size())
        {
          const auto hex = value.substr(i + 1, 2);
          char *end = nullptr;
          const auto decoded = std::strtol(hex.c_str(), &end, 16);
          if (end && *end == '\0')
          {
            out.push_back(static_cast<char>(decoded));
            i += 2;
            continue;
          }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
      }
      return out;
    }

    std::string random_hex(std::size_t bytes)
    {
      static constexpr char digits[] = "0123456789abcdef";
      std::random_device rng;
      std::string out;
      out.reserve(bytes * 2);
      for (std::size_t i = 0; i < bytes; ++i)
      {
        const auto value = static_cast<unsigned int>(rng()) & 0xffU;
        out.push_back(digits[(value >> 4U) & 0x0fU]);
        out.push_back(digits[value & 0x0fU]);
      }
      return out;
    }

    std::optional<GlobalCloudConfig> persist_login_session(const std::string &cloud_url, const ApiResult &result, const std::string &email_fallback)
    {
      GlobalCloudConfig cfg;
      cfg.cloud_url = normalize_cloud_url(cloud_url);
      cfg.session_id = result.data.value("session", json::object()).value("id", "");
      const auto user = result.data.value("user", json::object());
      cfg.user_id = user.value("id", "");
      cfg.email = user.value("email", email_fallback);
      cfg.display_name = user.value("display_name", user.value("name", ""));
      if (cfg.session_id.empty())
        return std::nullopt;
      if (!save_global_config(cfg))
        return std::nullopt;
      return cfg;
    }

    bool save_login_session(const std::string &cloud_url, const ApiResult &result, const std::string &email_fallback)
    {
      const auto cfg = persist_login_session(cloud_url, result, email_fallback);
      if (!cfg)
      {
        if (result.data.value("session", json::object()).value("id", "").empty())
          cloud_error("Cloud login response did not include a session.");
        else
          cloud_error("Unable to write " + global_config_path().string());
        return false;
      }
      cloud_success("Login successful.");
      cloud_step("Account", cfg->email.empty() ? "unknown" : cfg->email);
      cloud_step("Cloud URL", cfg->cloud_url);
      return true;
    }

    json login_session_json(const GlobalCloudConfig &cfg)
    {
      return json{
          {"ok", true},
          {"data", {
                       {"cloud_url", cfg.cloud_url},
                       {"session", {{"id", cfg.session_id}}},
                       {"user", {{"id", cfg.user_id}, {"email", cfg.email}, {"display_name", cfg.display_name}}},
                   }}};
    }

    json api_result_json(const ApiResult &result)
    {
      return json{
          {"ok", false},
          {"error", result.error.empty() ? "cloud_error" : result.error},
          {"message", result.message.empty() ? api_error_text(result) : result.message},
          {"status", result.status}};
    }

    bool open_browser(const std::string &url)
    {
      std::string output;
#ifdef _WIN32
      const auto result = vix::cli::build::run_process_capture({"rundll32", "url.dll,FileProtocolHandler", url}, {}, output);
#elif defined(__APPLE__)
      const auto result = vix::cli::build::run_process_capture({"open", url}, {}, output);
#else
      const char *browser = vix::utils::vix_getenv("BROWSER");
      if (browser && *browser)
      {
        const auto custom = vix::cli::build::run_process_capture({browser, url}, {}, output);
        if (custom.exitCode == 0)
          return true;
      }
      const auto result = vix::cli::build::run_process_capture({"xdg-open", url}, {}, output);
#endif
      return result.exitCode == 0;
    }

    struct BrowserCallback
    {
      bool ok{false};
      std::string code;
      std::string state;
      std::string error;
    };

#ifndef _WIN32
    class LocalCallbackServer
    {
    public:
      LocalCallbackServer() = default;
      ~LocalCallbackServer()
      {
        close_socket();
      }

      LocalCallbackServer(const LocalCallbackServer &) = delete;
      LocalCallbackServer &operator=(const LocalCallbackServer &) = delete;

      bool start()
      {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
        {
          error_ = "Unable to create local callback socket.";
          return false;
        }

        int enabled = 1;
        (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        if (::bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
        {
          error_ = "Unable to bind local callback socket.";
          close_socket();
          return false;
        }

        if (::listen(fd_, 1) != 0)
        {
          error_ = "Unable to listen on local callback socket.";
          close_socket();
          return false;
        }

        socklen_t length = sizeof(address);
        if (::getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &length) != 0)
        {
          error_ = "Unable to read local callback port.";
          close_socket();
          return false;
        }

        port_ = ntohs(address.sin_port);
        return port_ != 0;
      }

      std::string redirect_uri() const
      {
        return "http://127.0.0.1:" + std::to_string(port_) + "/callback";
      }

      const std::string &error() const
      {
        return error_;
      }

      BrowserCallback wait_for_callback(std::chrono::seconds timeout)
      {
        BrowserCallback result;
        if (fd_ < 0)
        {
          result.error = error_.empty() ? "Local callback server is not running." : error_;
          return result;
        }

        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(fd_, &reads);
        timeval tv{};
        tv.tv_sec = static_cast<long>(timeout.count());
        tv.tv_usec = 0;

        const int ready = ::select(fd_ + 1, &reads, nullptr, nullptr, &tv);
        if (ready <= 0)
        {
          result.error = ready == 0 ? "Timed out waiting for browser login." : "Failed while waiting for browser login.";
          return result;
        }

        sockaddr_in peer{};
        socklen_t peer_length = sizeof(peer);
        const int client = ::accept(fd_, reinterpret_cast<sockaddr *>(&peer), &peer_length);
        if (client < 0)
        {
          result.error = "Unable to accept browser callback.";
          return result;
        }

        std::array<char, 4096> buffer{};
        const ssize_t received = ::recv(client, buffer.data(), buffer.size() - 1, 0);
        if (received <= 0)
        {
          send_response(client, false, "The CLI did not receive a valid callback.");
          ::close(client);
          result.error = "Browser callback was empty.";
          return result;
        }

        const std::string request(buffer.data(), static_cast<std::size_t>(received));
        const auto line_end = request.find("\r\n");
        const std::string request_line = request.substr(0, line_end == std::string::npos ? request.find('\n') : line_end);
        result = parse_request_line(request_line);
        send_response(client, result.ok, result.ok ? "You can return to the terminal." : result.error);
        ::close(client);
        return result;
      }

    private:
      static BrowserCallback parse_request_line(const std::string &line)
      {
        BrowserCallback result;
        const auto first_space = line.find(' ');
        const auto second_space = first_space == std::string::npos ? std::string::npos : line.find(' ', first_space + 1);
        if (first_space == std::string::npos || second_space == std::string::npos || line.substr(0, first_space) != "GET")
        {
          result.error = "Browser callback used an invalid HTTP request.";
          return result;
        }

        const std::string target = line.substr(first_space + 1, second_space - first_space - 1);
        const auto query_pos = target.find('?');
        const std::string path = query_pos == std::string::npos ? target : target.substr(0, query_pos);
        if (path != "/callback")
        {
          result.error = "Browser callback used an invalid path.";
          return result;
        }

        std::string query = query_pos == std::string::npos ? std::string{} : target.substr(query_pos + 1);
        while (!query.empty())
        {
          const auto amp = query.find('&');
          const std::string part = query.substr(0, amp);
          const auto eq = part.find('=');
          const std::string key = url_decode(eq == std::string::npos ? part : part.substr(0, eq));
          const std::string value = eq == std::string::npos ? std::string{} : url_decode(part.substr(eq + 1));
          if (key == "code")
            result.code = value;
          else if (key == "state")
            result.state = value;
          else if (key == "error")
            result.error = value;
          if (amp == std::string::npos)
            break;
          query = query.substr(amp + 1);
        }

        if (!result.error.empty())
          return result;
        if (result.code.empty() || result.state.empty())
        {
          result.error = "Browser callback did not include code and state.";
          return result;
        }
        result.ok = true;
        return result;
      }

      static void send_response(int client, bool ok, const std::string &message)
      {
        const std::string title = ok ? "Softadastra Cloud login complete" : "Softadastra Cloud login failed";
        const std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + title +
                                 "</title><style>body{font-family:system-ui,sans-serif;background:#0f172a;color:#f8fafc;margin:0;display:grid;place-items:center;min-height:100vh}.box{max-width:520px;padding:32px}p{color:#cbd5e1}</style></head><body><main class=\"box\"><h1>" + title +
                                 "</h1><p>" + message + "</p></main></body></html>";
        const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                                     std::to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
        (void)::send(client, response.data(), response.size(), 0);
      }

      void close_socket()
      {
        if (fd_ >= 0)
        {
          ::close(fd_);
          fd_ = -1;
        }
      }

      int fd_{-1};
      std::uint16_t port_{0};
      std::string error_;
    };
#endif

    int login_manual(const std::string &cloud_url, std::string email, std::string password, bool json_output = false)
    {
      if (email.empty() && !json_output)
        email = prompt_line("Email");
      if (password.empty() && !json_output)
        password = prompt_password();
      if (email.empty() || password.empty())
      {
        if (json_output)
          std::cout << json{{"ok", false}, {"error", "credentials_required"}, {"message", "Email and password are required."}}.dump() << "\n";
        else
          cloud_error("Email and password are required.");
        return 1;
      }

      const auto result = api_post(cloud_url, "/api/auth/login", json{{"email", email}, {"password", password}});
      if (!result.ok)
      {
        if (json_output)
          std::cout << api_result_json(result).dump() << "\n";
        else
          print_api_error(result);
        return 1;
      }

      const auto cfg = persist_login_session(cloud_url, result, email);
      if (!cfg)
      {
        if (json_output)
          std::cout << json{{"ok", false}, {"error", "session_save_failed"}, {"message", "Unable to save the Cloud session."}}.dump() << "\n";
        else if (result.data.value("session", json::object()).value("id", "").empty())
          cloud_error("Cloud login response did not include a session.");
        else
          cloud_error("Unable to write " + global_config_path().string());
        return 1;
      }

      if (json_output)
        std::cout << login_session_json(*cfg).dump() << "\n";
      else
      {
        cloud_success("Login successful.");
        cloud_step("Account", cfg->email.empty() ? "unknown" : cfg->email);
        cloud_step("Cloud URL", cfg->cloud_url);
      }
      return 0;
    }

    int login_with_browser(const std::string &cloud_url)
    {
#ifdef _WIN32
      terminal::warning("Browser login is not available on this platform yet.");
      return 1;
#else
      LocalCallbackServer server;
      if (!server.start())
      {
        cloud_error(server.error());
        return 1;
      }

      const std::string state = random_hex(32);
      const std::string redirect_uri = server.redirect_uri();
      const std::string login_url = std::string(frontend_cloud_url) + "/cli/login?state=" + url_encode(state) +
                                    "&redirect_uri=" + url_encode(redirect_uri);

      cloud_step("Login URL", login_url);
      if (open_browser(login_url))
      {
        cloud_success("Browser opened. Complete sign in to continue.");
      }
      else
      {
        terminal::warning("Could not open the browser automatically.");
        cloud_hint("Open the Login URL above in your browser.");
      }

      cloud_step("Callback", redirect_uri);
      cloud_hint("Waiting for browser login...");

      const auto callback = server.wait_for_callback(std::chrono::seconds(180));
      if (!callback.ok)
      {
        cloud_error(callback.error.empty() ? "Browser login failed." : callback.error);
        return 1;
      }
      if (callback.state != state)
      {
        cloud_error("Browser login returned an invalid state value.");
        return 1;
      }

      const auto result = api_post(
          cloud_url,
          "/api/auth/cli/exchange",
          json{{"code", callback.code}, {"state", callback.state}, {"redirect_uri", redirect_uri}});
      if (!result.ok)
      {
        print_api_error(result);
        return 1;
      }
      return save_login_session(cloud_url, result, {}) ? 0 : 1;
#endif
    }
  }

  int CloudCommand::login(const std::vector<std::string> &args)
  {
    const bool json_output = has_flag(args, "--json");
    const std::string cloud_url = normalize_cloud_url(arg_value(args, "--url").value_or(default_cloud_url));
    std::string email = arg_value(args, "--email").value_or("");
    std::string password = arg_value(args, "--password").value_or("");

    const bool manual_flag = has_flag(args, "--manual") || has_flag(args, "--password") || has_flag(args, "--email") ||
                             arg_value(args, "--email").has_value() || arg_value(args, "--password").has_value();
    const bool browser_flag = has_flag(args, "--browser");

    if (json_output && !manual_flag)
    {
      std::cout << json{{"ok", false}, {"error", "login_method_required"}, {"message", "Use --email and --password when --json is set."}}.dump() << "\n";
      return 1;
    }

    if (!json_output)
      cloud_header("Softadastra Cloud", "Sign in to continue");

    if (manual_flag)
      return login_manual(cloud_url, std::move(email), std::move(password), json_output);

    if (browser_flag)
      return login_with_browser(cloud_url);

    terminal::section("Choose a login method");
    std::cout << "  ";
    terminal::code(std::cout, terminal::cyan);
    std::cout << "1.";
    terminal::code(std::cout, terminal::reset);
    std::cout << " Open browser\n";
    std::cout << "  ";
    terminal::code(std::cout, terminal::cyan);
    std::cout << "2.";
    terminal::code(std::cout, terminal::reset);
    std::cout << " Enter email and password\n";

    const std::string choice = prompt_line("Choose login method", "1");
    if (choice == "2" || lower_copy(choice) == "manual" || lower_copy(choice) == "password")
      return login_manual(cloud_url, {}, {});

    return login_with_browser(cloud_url);
  }

  int CloudCommand::logout(const std::vector<std::string> &)
  {
    auto cfg = load_global_config().value_or(GlobalCloudConfig{});
    if (cfg.cloud_url.empty())
      cfg.cloud_url = default_cloud_url;
    cfg.session_id.clear();
    save_global_config(cfg);
    cloud_header("Softadastra Cloud", "Authentication and project operations");
    cloud_success("Logged out.");
    return 0;
  }

  int CloudCommand::status(const std::vector<std::string> &)
  {
    auto cfg = load_global_config();
    if (!cfg || cfg->session_id.empty())
    {
      cloud_header("Softadastra Cloud", "Authentication and project operations");
      cloud_error("Not connected to Softadastra Cloud.");
      cloud_hint("Run: vix login");
      return 0;
    }
    auto result = api_post(cfg->cloud_url, "/api/auth/me", json{{"session_id", cfg->session_id}}, cfg);
    if (!result.ok)
    {
      print_api_error(result);
      return 1;
    }
    const auto user = result.data.value("user", json::object());
    cloud_header("Softadastra Cloud", "Authentication and project operations");
    cloud_success("Session active.");
    cloud_step("Account", user.value("email", cfg->email));
    cloud_step("Cloud URL", cfg->cloud_url);
    return 0;
  }

  int CloudCommand::init(const std::vector<std::string> &args)
  {
    auto cfg = require_connected();
    if (!cfg)
      return 1;

    cloud_header("Cloud project link", "Select a workspace and project for this directory");

    const std::string cloud_url = strip_trailing_slash(arg_value(args, "--url").value_or(cfg->cloud_url));
    auto workspaces = api_post(cloud_url, "/api/workspaces/list", json{{"owner_user_id", cfg->user_id}}, cfg);
    if (!workspaces.ok)
    {
      print_api_error(workspaces);
      return 1;
    }
    auto workspace = choose_item(workspaces.data.value("workspaces", json::array()), "Workspaces");
    if (!workspace)
    {
      cloud_error("No workspace selected.");
      return 1;
    }
    const std::string workspace_id = workspace->value("id", "");
    const std::string workspace_name = workspace->value("name", workspace_id);
    auto projects = api_post(cloud_url, "/api/projects/list", json{{"workspace_id", workspace_id}}, cfg);
    if (!projects.ok)
    {
      print_api_error(projects);
      return 1;
    }
    std::optional<json> project = choose_item(projects.data.value("projects", json::array()), "Projects");
    if (!project)
    {
      const std::string create = prompt_line("No project selected. Create project? (y/N)", "N");
      if (create != "y" && create != "Y")
        return 1;
      const std::string name = prompt_line("Project name", fs::current_path().filename().string());
      const std::string slug = prompt_line("Project slug", slugify(name));
      auto created = api_post(cloud_url, "/api/projects", json{{"workspace_id", workspace_id}, {"owner_user_id", cfg->user_id}, {"name", name}, {"slug", slug}, {"description", ""}, {"repository_url", ""}, {"default_branch", "main"}}, cfg);
      if (!created.ok)
      {
        print_api_error(created);
        return 1;
      }
      project = created.data.value("project", json::object());
    }
    ProjectCloudConfig linked;
    linked.cloud_url = cloud_url;
    linked.workspace_id = workspace_id;
    linked.project_id = project->value("id", "");
    linked.workspace_name = workspace_name;
    linked.project_name = project->value("name", linked.project_id);
    if (linked.workspace_id.empty() || linked.project_id.empty())
    {
      cloud_error("Invalid cloud project selection.");
      return 1;
    }
    if (!save_project_config(linked))
    {
      cloud_error("Unable to write " + project_config_path().string());
      return 1;
    }
    cloud_success("Project linked.");
    cloud_step("Workspace", linked.workspace_name);
    cloud_step("Project", linked.project_name);
    cloud_step("Cloud URL", linked.cloud_url);
    return 0;
  }

  int CloudCommand::sync(const std::vector<std::string> &)
  {
    auto cfg = require_connected();
    if (!cfg)
      return 1;
    auto linked = load_project_config();
    if (!linked || linked->workspace_id.empty() || linked->project_id.empty())
    {
      cloud_error("Project is not linked to Softadastra Cloud.");
      cloud_hint("Run: vix cloud init");
      return 1;
    }
    cloud_header("Softadastra Cloud", "Local project link");
    cloud_success("Project link is active.");
    cloud_step("Workspace", linked->workspace_name);
    cloud_step("Project", linked->project_name);
    return 0;
  }

  int CloudCommand::upload_lockfile(const std::vector<std::string> &args)
  {
    const bool json_output = has_flag(args, "--json");

    std::string context_error;
    auto ctx = load_cloud_context(context_error);
    if (!ctx)
    {
      cloud_error(context_error);
      return 1;
    }

    fs::path lockfile = fs::current_path() / "vix.lock";
    if (auto file_arg = arg_value(args, "--file"))
      lockfile = fs::path(*file_arg);

    if (!fs::exists(lockfile) || !fs::is_regular_file(lockfile))
    {
      cloud_error("No vix.lock file found.");
      cloud_hint("Run this command from a linked Vix project or pass --file <path>.");
      return 1;
    }

    const auto content = read_text_file(lockfile);
    if (!content)
    {
      cloud_error("Unable to read lockfile: " + lockfile.string());
      return 1;
    }

    const auto checksum = vix::cli::util::sha256_file(lockfile);
    if (!checksum)
    {
      cloud_error("Unable to calculate lockfile checksum.");
      return 1;
    }

    const json payload{
        {"workspace_id", ctx->project.workspace_id},
        {"project_id", ctx->project.project_id},
        {"uploaded_by_user_id", ctx->global.user_id},
        {"lockfile_json", *content},
        {"checksum_sha256", *checksum},
        {"source", "vix"}};

    if (!json_output)
    {
      cloud_header("Softadastra Cloud", "Lockfile history");
      cloud_step("Project", ctx->project.project_name.empty() ? ctx->project.project_id : ctx->project.project_name);
      cloud_step("File", lockfile.string());
      terminal::progress("Uploading lockfile...");
    }

    const auto result = api_post(ctx->cloud_url, "/api/lockfiles/upload", payload, ctx->global);
    if (!result.ok)
    {
      cloud_error(permission_error_text(result, "You do not have permission to upload lockfiles for this project."));
      return 1;
    }

    if (json_output)
    {
      std::cout << json{{"ok", true}, {"checksum_sha256", *checksum}, {"project_id", ctx->project.project_id}}.dump(2) << "\n";
      return 0;
    }

    cloud_success("Lockfile uploaded.");
    cloud_step("Checksum", *checksum);
    return 0;
  }

  bool CloudCommand::submit_build_report(const CloudBuildReport &report, std::string &message)
  {
    std::string context_error;
    auto ctx = load_cloud_context(context_error);
    if (!ctx)
    {
      message = context_error;
      return false;
    }

    const std::string branch = git_value({"git", "rev-parse", "--abbrev-ref", "HEAD"});
    const std::string commit = git_value({"git", "rev-parse", "HEAD"});

    const json summary{
        {"message", report.summary_message.empty() ? "Build completed" : report.summary_message},
        {"target", report.target},
        {"profile", report.profile}};

    const json diagnostics = report.status == "success"
                                 ? json::array()
                                 : json::array({json{{"message", report.summary_message.empty() ? "Build failed" : report.summary_message}}});

    const json payload{
        {"workspace_id", ctx->project.workspace_id},
        {"project_id", ctx->project.project_id},
        {"submitted_by_user_id", ctx->global.user_id},
        {"status", report.status},
        {"target", report.target},
        {"profile", report.profile},
        {"branch", branch},
        {"commit_sha", commit},
        {"toolchain", report.toolchain},
        {"summary_json", summary.dump()},
        {"diagnostics_json", diagnostics.dump()},
        {"duration_ms", report.duration_ms},
        {"warnings_count", report.warnings_count},
        {"errors_count", report.errors_count}};

    const auto result = api_post(ctx->cloud_url, "/api/build_reports/submit", payload, ctx->global);
    if (!result.ok)
    {
      message = permission_error_text(result, "You do not have permission to submit build reports for this project.");
      return false;
    }

    message.clear();
    return true;
  }

  int CloudCommand::publish(const std::vector<std::string> &args)
  {
    CloudPublishOptions opt;
    try
    {
      opt = parse_cloud_publish_options(args);
    }
    catch (const std::exception &ex)
    {
      CloudPublishOptions fallback;
      fallback.json_output = has_flag(args, "--json");
      return print_cloud_publish_error(fallback, "invalid_arguments", ex.what());
    }

    if (opt.help)
    {
      cloud_header("Cloud publish", "Publish a package version from the current Vix project");

      terminal::section("Usage");
      terminal::command("vix publish --cloud [options]");
      terminal::command("vix cloud publish [options]");

      terminal::section("Options");
      terminal::command("--package <name>", "Package name, for example vix/http");
      terminal::command("--version <version>", "Package version");
      terminal::command("--visibility <private|public>", "Package visibility");
      terminal::command("--description <text>", "Package description");
      terminal::command("--repository-url <url>", "Source repository URL");
      terminal::command("--archive <path>", "Use an existing package.tar.gz");
      terminal::command("--manifest <path>", "Use an existing manifest JSON file");
      terminal::command("--dry-run", "Prepare and inspect the publish operation without uploading");
      terminal::command("--json", "Emit machine-readable JSON");
      return 0;
    }

    std::string context_error;
    auto ctx = load_cloud_context(context_error);
    if (!ctx)
    {
      const std::string msg = context_error == "Authentication failed. Run vix login again."
                                  ? "Not connected to Softadastra Cloud. Run vix login first."
                                  : context_error;
      return print_cloud_publish_error(opt, "not_ready", msg);
    }

    opt.visibility = lower_copy(trim_copy(opt.visibility));
    if (opt.visibility.empty())
      opt.visibility = "private";
    if (opt.visibility != "private" && opt.visibility != "public")
      return print_cloud_publish_error(opt, "invalid_visibility", "Visibility must be private or public.");

    json manifest = build_cloud_manifest(opt, *ctx);

    if (opt.package_name.empty())
      return print_cloud_publish_error(opt, "missing_package", "Could not determine package name. Pass --package <name>.");

    if (opt.version.empty())
      opt.version = latest_git_semver_version();

    if (opt.version.empty())
      return print_cloud_publish_error(opt, "missing_version", "Could not determine package version. Pass --version <version>.");

    manifest["name"] = opt.package_name;
    manifest["version"] = opt.version;

    std::string archive_error;
    auto archive = prepare_archive(opt, archive_error);
    if (!archive)
      return print_cloud_publish_error(opt, "archive_error", archive_error);

    if (opt.dry_run)
    {
      if (opt.json_output)
      {
        std::cout << json{
                         {"ok", true},
                         {"dry_run", true},
                         {"cloud_url", ctx->cloud_url},
                         {"workspace_id", ctx->project.workspace_id},
                         {"project_id", ctx->project.project_id},
                         {"package", opt.package_name},
                         {"version", opt.version},
                         {"manifest_json", manifest.dump()},
                         {"archive_path", archive->path.string()},
                         {"archive_size", archive->size},
                         {"checksum_sha256", archive->checksum_sha256}}
                         .dump(2)
                  << "\n";
      }
      else
      {
        cloud_header("Cloud publish", "Dry run");
        cloud_success("Package is ready to publish.");
        cloud_step("Cloud URL", ctx->cloud_url);
        cloud_step("Workspace", ctx->project.workspace_name.empty() ? ctx->project.workspace_id : ctx->project.workspace_name);
        cloud_step("Project", ctx->project.project_name.empty() ? ctx->project.project_id : ctx->project.project_name);
        cloud_step("Package", opt.package_name);
        cloud_step("Version", opt.version);
        cloud_step("Visibility", opt.visibility);
        cloud_step("Archive", archive->path.string());
        cloud_step("Archive size", std::to_string(archive->size) + " bytes");
        cloud_step("Checksum", archive->checksum_sha256);

        terminal::section("Manifest");
        terminal::code(std::cout, terminal::dim);
        std::cout << manifest.dump(2) << "\n";
        terminal::code(std::cout, terminal::reset);
      }
      return 0;
    }

    if (!opt.json_output)
    {
      cloud_header("Cloud publish", "Publishing package version");
      cloud_step("Package", opt.package_name);
      cloud_step("Version", opt.version);
      cloud_step("Workspace", ctx->project.workspace_name.empty() ? ctx->project.workspace_id : ctx->project.workspace_name);
      cloud_step("Archive size", std::to_string(archive->size) + " bytes");
      cloud_step("Checksum", archive->checksum_sha256);
      terminal::progress("Resolving package record...");
    }

    ApiResult listResult;
    std::optional<json> package = find_package_by_name(*ctx, opt.package_name, &listResult);
    if (!package && !listResult.ok)
      return print_cloud_publish_error(opt, listResult.error.empty() ? "package_list_failed" : listResult.error, publish_permission_message(listResult));

    if (!package)
    {
      auto created = api_post(ctx->cloud_url, "/api/packages", json{{"workspace_id", ctx->project.workspace_id}, {"owner_user_id", ctx->global.user_id}, {"created_by_user_id", ctx->global.user_id}, {"name", opt.package_name}, {"description", opt.description}, {"repository_url", opt.repository_url}, {"visibility", opt.visibility}},
                              ctx->global);
      if (!created.ok)
      {
        if (created.status == 409 || created.error == "package_already_exists")
        {
          package = find_package_by_name(*ctx, opt.package_name, nullptr);
        }
        else
        {
          return print_cloud_publish_error(opt, created.error.empty() ? "package_create_failed" : created.error, publish_permission_message(created));
        }
      }
      else
      {
        package = created.data.value("package", json::object());
        if (!opt.json_output)
          cloud_success("Package record created.");
      }
    }

    if (!package || package->value("id", "").empty())
      return print_cloud_publish_error(opt, "package_not_found", "Package could not be found or created in Softadastra Cloud.");

    const std::string package_id = package->value("id", "");
    auto bytes = read_binary_file(archive->path);
    if (!bytes)
      return print_cloud_publish_error(opt, "archive_read_failed", "Unable to read package archive.");

    vix::requests::Params params;
    params.set("published_by_user_id", ctx->global.user_id);
    params.set("checksum_sha256", archive->checksum_sha256);
    params.set("manifest_json", manifest.dump());

    const std::string upload_path = "/api/package_versions/upload/" +
                                    vix::requests::url_encode(ctx->project.workspace_id) + "/" +
                                    vix::requests::url_encode(package_id) + "/" +
                                    vix::requests::url_encode(opt.version) + "?" +
                                    params.to_query_string();

    if (!opt.json_output)
      terminal::progress("Uploading archive...");

    auto uploaded = api_post_binary(ctx->cloud_url, upload_path, std::move(*bytes), ctx->global);
    if (!uploaded.ok)
      return print_cloud_publish_error(opt, uploaded.error.empty() ? "package_upload_failed" : uploaded.error, publish_permission_message(uploaded));

    if (opt.json_output)
    {
      std::cout << json{
                       {"ok", true},
                       {"package", opt.package_name},
                       {"version", opt.version},
                       {"workspace_id", ctx->project.workspace_id},
                       {"project_id", ctx->project.project_id},
                       {"package_id", package_id},
                       {"checksum_sha256", archive->checksum_sha256},
                       {"archive_size", archive->size}}
                       .dump(2)
                << "\n";
    }
    else
    {
      cloud_success("Package version published.");
      cloud_step("Package ID", package_id);
      cloud_hint("The version is now available from Softadastra Cloud.");
    }

    return 0;
  }

  int CloudCommand::doctor(const std::vector<std::string> &)
  {
    cloud_header("Softadastra Cloud doctor", "Connectivity and project readiness");

    auto cfg = load_global_config();
    if (!cfg || cfg->session_id.empty())
    {
      terminal::check("Cloud config", cfg.has_value(), cfg ? "loaded" : "missing");
      terminal::check("Session", false, "not authenticated");
      terminal::check("Project link", false, "not checked");
      cloud_hint("Run: vix login");
      return 1;
    }

    auto health = api_get(cfg->cloud_url, "/api/health", cfg);
    terminal::check("Cloud API", health.ok, health.ok ? cfg->cloud_url : "request failed");
    if (!health.ok)
      print_api_error(health);

    auto me = api_post(cfg->cloud_url, "/api/auth/me", json{{"session_id", cfg->session_id}}, cfg);
    terminal::check("Session", me.ok, me.ok ? "authenticated" : "invalid or expired");
    if (!me.ok)
    {
      print_api_error(me);
      return 1;
    }

    auto linked = load_project_config();
    const bool lockfile_found = fs::exists(fs::current_path() / "vix.lock");
    if (!linked || linked->workspace_id.empty() || linked->project_id.empty())
    {
      terminal::check("Project link", false, "not linked");
      terminal::check("Lockfile", lockfile_found, lockfile_found ? "vix.lock found" : "vix.lock missing");
      cloud_hint("Run: vix cloud init");
      return 1;
    }

    terminal::check("Project link", true, linked->project_name.empty() ? linked->project_id : linked->project_name);

    const std::string url = linked->cloud_url.empty() ? cfg->cloud_url : linked->cloud_url;
    auto workspace = api_post(url, "/api/workspaces/show", json{{"id", linked->workspace_id}}, cfg);
    auto project = api_post(url, "/api/projects/show", json{{"workspace_id", linked->workspace_id}, {"id", linked->project_id}}, cfg);

    const bool permission_denied = workspace.status == 403 || project.status == 403;
    const bool ready = workspace.ok && project.ok;

    terminal::check("Workspace", workspace.ok, workspace.ok ? "reachable" : (workspace.status == 403 ? "permission denied" : "request failed"));
    terminal::check("Project", project.ok, project.ok ? "reachable" : (project.status == 403 ? "permission denied" : "request failed"));
    terminal::check("Permissions", ready, ready ? "ready" : (permission_denied ? "permission denied" : "backend check failed"));
    terminal::check("Lockfile", lockfile_found, lockfile_found ? "vix.lock found" : "vix.lock missing");
    terminal::check("Lockfile upload", ready, ready ? "ready" : (permission_denied ? "permission denied" : "not ready"));
    terminal::check("Build reports", ready, ready ? "ready" : (permission_denied ? "permission denied" : "not ready"));

    if (ready)
      cloud_success("Cloud integration is ready.");
    else
      cloud_hint("Review the failed checks above before retrying.");

    return ready ? 0 : 1;
  }

  int CloudCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty() || args[0] == "status")
      return status(args.empty() ? args : std::vector<std::string>(args.begin() + 1, args.end()));
    if (args[0] == "init")
      return init(std::vector<std::string>(args.begin() + 1, args.end()));
    if (args[0] == "sync")
      return sync(std::vector<std::string>(args.begin() + 1, args.end()));
    if (args[0] == "publish")
      return publish(std::vector<std::string>(args.begin() + 1, args.end()));
    if ((args[0] == "lockfile" || args[0] == "lock") && args.size() > 1 && args[1] == "upload")
      return upload_lockfile(std::vector<std::string>(args.begin() + 2, args.end()));
    if (args[0] == "doctor")
      return doctor(std::vector<std::string>(args.begin() + 1, args.end()));
    if (args[0] == "--help" || args[0] == "-h")
      return help();
    cloud_error("Unknown cloud command: " + args[0]);
    cloud_hint("Run: vix cloud --help");
    help();
    return 1;
  }

  int CloudCommand::help()
  {
    cloud_header("Softadastra Cloud", "Runtime operations for packages, lockfiles and build reports");

    terminal::section("Authentication");
    terminal::command("vix login [--url <url>] [--email <email>] [--password <password>]", "Connect the Vix CLI to Softadastra Cloud");
    terminal::command("vix logout", "Remove the active local session");
    terminal::command("vix cloud status", "Check the active Cloud session");

    terminal::section("Project operations");
    terminal::command("vix cloud init [--url <url>]", "Link the current directory to a workspace project");
    terminal::command("vix cloud sync", "Verify the local project link");
    terminal::command("vix cloud lockfile upload [--file <path>] [--json]", "Upload the current dependency lockfile");
    terminal::command("vix cloud lock upload [--file <path>] [--json]", "Alias for lockfile upload");
    terminal::command("vix cloud publish [options]", "Publish a package version");
    terminal::command("vix publish --cloud [options]", "Publish through the top-level publish command");
    terminal::command("vix doctor --cloud", "Check Cloud connectivity, permissions and project readiness");

    terminal::section("Configuration");
    cloud_step("Global session", "~/.vix/cloud/config.json");
    cloud_step("Project link", ".vix/cloud.json");
    return 0;
  }

}
