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
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vix::commands
{
  namespace
  {
    constexpr const char *default_cloud_url = "http://127.0.0.1:8080";

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

    std::string strip_trailing_slash(std::string value)
    {
      while (value.size() > 1 && value.back() == '/')
        value.pop_back();
      return value;
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
      std::cout << label;
      if (!fallback.empty())
        std::cout << " [" << fallback << "]";
      std::cout << ": ";
      std::string value;
      std::getline(std::cin, value);
      value = trim_copy(value);
      return value.empty() ? fallback : value;
    }

    std::string prompt_password()
    {
      std::cout << "Password: ";
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
      cfg.cloud_url = strip_trailing_slash(value.value("cloud_url", default_cloud_url));
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
      cfg.cloud_url = strip_trailing_slash(value.value("cloud_url", default_cloud_url));
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
      try
      {
        payload = response.text().empty() ? json::object() : json::parse(response.text());
      }
      catch (...)
      {
        result.message = "Invalid JSON response from cloud.";
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
        std::cerr << "You do not have permission to perform this action in this workspace.\n";
        return;
      }
      if (!result.error.empty())
        std::cerr << result.error << ": ";
      std::cerr << (result.message.empty() ? "Cloud request failed." : result.message) << "\n";
    }

    std::optional<GlobalCloudConfig> require_connected()
    {
      auto cfg = load_global_config();
      if (!cfg || cfg->session_id.empty())
      {
        std::cerr << "Not connected to Softadastra Cloud. Run `vix login` first.\n";
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

    std::string git_value(const std::vector<std::string> &argv)
    {
      std::string output;
      const auto result = vix::cli::build::run_process_capture(argv, {}, output);
      if (result.exitCode != 0)
        return {};
      return trim_copy(output);
    }

    std::string item_label(const json &item)
    {
      const std::string name = item.value("name", "");
      const std::string id = item.value("id", "");
      return name.empty() ? id : name + " (" + id + ")";
    }

    std::optional<json> choose_item(const json &items, const std::string &label)
    {
      if (!items.is_array() || items.empty())
        return std::nullopt;
      std::cout << label << ":\n";
      for (std::size_t i = 0; i < items.size(); ++i)
        std::cout << "  " << (i + 1) << ". " << item_label(items[i]) << "\n";
      const std::string answer = prompt_line("Choose number");
      try
      {
        const std::size_t index = static_cast<std::size_t>(std::stoul(answer));
        if (index >= 1 && index <= items.size())
          return items[index - 1];
      }
      catch (...)
      {
      }
      return std::nullopt;
    }
  }

  int CloudCommand::login(const std::vector<std::string> &args)
  {
    const std::string cloud_url = strip_trailing_slash(arg_value(args, "--url").value_or(default_cloud_url));
    std::string email = arg_value(args, "--email").value_or("");
    std::string password = arg_value(args, "--password").value_or("");
    if (email.empty())
      email = prompt_line("Email");
    if (password.empty())
      password = prompt_password();
    if (email.empty() || password.empty())
    {
      std::cerr << "Email and password are required.\n";
      return 1;
    }
    const auto result = api_post(cloud_url, "/api/auth/login", json{{"email", email}, {"password", password}});
    if (!result.ok)
    {
      print_api_error(result);
      return 1;
    }
    GlobalCloudConfig cfg;
    cfg.cloud_url = cloud_url;
    cfg.session_id = result.data.value("session", json::object()).value("id", "");
    const auto user = result.data.value("user", json::object());
    cfg.user_id = user.value("id", "");
    cfg.email = user.value("email", email);
    cfg.display_name = user.value("display_name", user.value("name", ""));
    if (cfg.session_id.empty())
    {
      std::cerr << "Cloud login response did not include a session.\n";
      return 1;
    }
    if (!save_global_config(cfg))
    {
      std::cerr << "Unable to write " << global_config_path() << "\n";
      return 1;
    }
    std::cout << "Login successful.\n";
    std::cout << "Connected to Softadastra Cloud as " << cfg.email << ".\n";
    return 0;
  }

  int CloudCommand::logout(const std::vector<std::string> &)
  {
    auto cfg = load_global_config().value_or(GlobalCloudConfig{});
    if (cfg.cloud_url.empty())
      cfg.cloud_url = default_cloud_url;
    cfg.session_id.clear();
    save_global_config(cfg);
    std::cout << "Logged out from Softadastra Cloud.\n";
    return 0;
  }

  int CloudCommand::status(const std::vector<std::string> &)
  {
    auto cfg = load_global_config();
    if (!cfg || cfg->session_id.empty())
    {
      std::cout << "Not connected to Softadastra Cloud.\n";
      return 0;
    }
    auto result = api_post(cfg->cloud_url, "/api/auth/me", json{{"session_id", cfg->session_id}}, cfg);
    if (!result.ok)
    {
      print_api_error(result);
      return 1;
    }
    const auto user = result.data.value("user", json::object());
    std::cout << "Connected as " << user.value("email", cfg->email) << "\n";
    std::cout << "Cloud URL: " << cfg->cloud_url << "\n";
    return 0;
  }

  int CloudCommand::init(const std::vector<std::string> &args)
  {
    auto cfg = require_connected();
    if (!cfg)
      return 1;
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
      std::cerr << "No workspace selected.\n";
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
      std::cerr << "Invalid cloud project selection.\n";
      return 1;
    }
    if (!save_project_config(linked))
    {
      std::cerr << "Unable to write " << project_config_path() << "\n";
      return 1;
    }
    std::cout << "Project linked to Softadastra Cloud.\n";
    std::cout << "Workspace: " << linked.workspace_name << "\n";
    std::cout << "Project: " << linked.project_name << "\n";
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
      std::cerr << "Project is not linked to Softadastra Cloud. Run `vix cloud init`.\n";
      return 1;
    }
    std::cout << "Project is linked to Softadastra Cloud.\n";
    std::cout << "Workspace: " << linked->workspace_name << "\n";
    std::cout << "Project: " << linked->project_name << "\n";
    return 0;
  }

  int CloudCommand::upload_lockfile(const std::vector<std::string> &args)
  {
    const bool json_output = has_flag(args, "--json");

    std::string context_error;
    auto ctx = load_cloud_context(context_error);
    if (!ctx)
    {
      std::cerr << context_error << "\n";
      return 1;
    }

    fs::path lockfile = fs::current_path() / "vix.lock";
    if (auto file_arg = arg_value(args, "--file"))
      lockfile = fs::path(*file_arg);

    if (!fs::exists(lockfile) || !fs::is_regular_file(lockfile))
    {
      std::cerr << "No vix.lock file found. Run this command from a linked Vix project or pass --file <path>.\n";
      return 1;
    }

    const auto content = read_text_file(lockfile);
    if (!content)
    {
      std::cerr << "Unable to read lockfile: " << lockfile << "\n";
      return 1;
    }

    const auto checksum = vix::cli::util::sha256_file(lockfile);
    if (!checksum)
    {
      std::cerr << "Unable to calculate lockfile checksum.\n";
      return 1;
    }

    const json payload{
        {"workspace_id", ctx->project.workspace_id},
        {"project_id", ctx->project.project_id},
        {"uploaded_by_user_id", ctx->global.user_id},
        {"lockfile_json", *content},
        {"checksum_sha256", *checksum},
        {"source", "vix"}};

    const auto result = api_post(ctx->cloud_url, "/api/lockfiles/upload", payload, ctx->global);
    if (!result.ok)
    {
      std::cerr << permission_error_text(result, "You do not have permission to upload lockfiles for this project.") << "\n";
      return 1;
    }

    if (json_output)
    {
      std::cout << json{{"ok", true}, {"checksum_sha256", *checksum}, {"project_id", ctx->project.project_id}}.dump(2) << "\n";
      return 0;
    }

    std::cout << "Lockfile uploaded to Softadastra Cloud.\n";
    std::cout << "Project: " << (ctx->project.project_name.empty() ? ctx->project.project_id : ctx->project.project_name) << "\n";
    std::cout << "Checksum: " << *checksum << "\n";
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

  int CloudCommand::doctor(const std::vector<std::string> &)
  {
    std::cout << "Softadastra Cloud doctor\n\n";
    auto cfg = load_global_config();
    if (!cfg || cfg->session_id.empty())
    {
      std::cout << "Cloud URL: missing\nSession: missing\nProject: not checked\n";
      return 1;
    }
    auto health = api_get(cfg->cloud_url, "/api/health", cfg);
    std::cout << "Cloud URL: " << (health.ok ? "OK" : "failed") << "\n";
    auto me = api_post(cfg->cloud_url, "/api/auth/me", json{{"session_id", cfg->session_id}}, cfg);
    std::cout << "Session: " << (me.ok ? "OK" : "failed") << "\n";
    if (!me.ok)
    {
      print_api_error(me);
      return 1;
    }
    auto linked = load_project_config();
    if (!linked || linked->workspace_id.empty() || linked->project_id.empty())
    {
      std::cout << "Workspace: not linked\nProject: not linked\n";
      std::cout << "Lockfile: " << (fs::exists(fs::current_path() / "vix.lock") ? "found" : "missing") << "\n";
      return 1;
    }
    const std::string url = linked->cloud_url.empty() ? cfg->cloud_url : linked->cloud_url;
    auto workspace = api_post(url, "/api/workspaces/show", json{{"id", linked->workspace_id}}, cfg);
    auto project = api_post(url, "/api/projects/show", json{{"workspace_id", linked->workspace_id}, {"id", linked->project_id}}, cfg);
    const bool permissionDenied = workspace.status == 403 || project.status == 403;
    const bool ready = workspace.ok && project.ok;
    std::cout << "Workspace: " << (workspace.ok ? "OK" : (workspace.status == 403 ? "permission denied" : "failed")) << "\n";
    std::cout << "Project: " << (project.ok ? "OK" : (project.status == 403 ? "permission denied" : "failed")) << "\n";
    std::cout << "Permissions: " << (ready ? "OK" : (permissionDenied ? "permission denied" : "check backend response")) << "\n";
    std::cout << "Lockfile: " << (fs::exists(fs::current_path() / "vix.lock") ? "found" : "missing") << "\n";
    std::cout << "Can upload lockfile: " << (ready ? "OK" : (permissionDenied ? "permission denied" : "not ready")) << "\n";
    std::cout << "Build reports: " << (ready ? "ready" : (permissionDenied ? "permission denied" : "not ready")) << "\n";
    return (workspace.ok && project.ok) ? 0 : 1;
  }

  int CloudCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty() || args[0] == "status")
      return status(args.empty() ? args : std::vector<std::string>(args.begin() + 1, args.end()));
    if (args[0] == "init")
      return init(std::vector<std::string>(args.begin() + 1, args.end()));
    if (args[0] == "sync")
      return sync(std::vector<std::string>(args.begin() + 1, args.end()));
    if ((args[0] == "lockfile" || args[0] == "lock") && args.size() > 1 && args[1] == "upload")
      return upload_lockfile(std::vector<std::string>(args.begin() + 2, args.end()));
    if (args[0] == "doctor")
      return doctor(std::vector<std::string>(args.begin() + 1, args.end()));
    if (args[0] == "--help" || args[0] == "-h")
      return help();
    std::cerr << "unknown cloud command: " << args[0] << "\n";
    help();
    return 1;
  }

  int CloudCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix login [--url <url>] [--email <email>] [--password <password>]\n"
        << "  vix logout\n"
        << "  vix cloud status\n"
        << "  vix cloud init [--url <url>]\n"
        << "  vix cloud sync\n"
        << "  vix cloud lockfile upload [--file <path>] [--json]\n"
        << "  vix cloud lock upload [--file <path>] [--json]\n"
        << "  vix doctor --cloud\n\n"
        << "Cloud config:\n"
        << "  Global session: ~/.vix/cloud/config.json\n"
        << "  Project link:   .vix/cloud.json\n";
    return 0;
  }
}
