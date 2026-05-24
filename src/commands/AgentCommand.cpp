/**
 *
 *  @file AgentCommand.cpp
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
#include <vix/cli/commands/AgentCommand.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <vix/ai/agent/agent.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/build/BuildStyle.hpp>

namespace vix::commands::AgentCommand
{
  namespace
  {
    namespace build = vix::cli::build;
    namespace style = vix::cli::style;

    struct Options
    {
      std::string subcommand;
      std::string input;
      std::string workspace{"."};
      std::string provider;
      std::string model;
      std::string model_url;

      std::uint64_t timeout_ms{0};

      bool allow_process{false};
      bool allow_file_read{true};
      bool allow_file_write{false};
      bool use_cache{true};
      bool persist_memory{true};
      bool show_help{false};
    };

    [[nodiscard]] std::string join_args(
        const std::vector<std::string> &args,
        std::size_t start)
    {
      std::string out;

      for (std::size_t i = start; i < args.size(); ++i)
      {
        if (!out.empty())
        {
          out += " ";
        }

        out += args[i];
      }

      return out;
    }

    [[nodiscard]] bool read_value(
        const std::vector<std::string> &args,
        std::size_t &index,
        std::string &out)
    {
      if (index + 1 >= args.size())
      {
        return false;
      }

      out = args[index + 1];
      ++index;
      return true;
    }

    [[nodiscard]] bool parse_uint64(
        std::string_view value,
        std::uint64_t &out)
    {
      if (value.empty())
      {
        return false;
      }

      for (char c : value)
      {
        if (c < '0' || c > '9')
        {
          return false;
        }
      }

      try
      {
        out = static_cast<std::uint64_t>(
            std::stoull(std::string(value)));
      }
      catch (...)
      {
        return false;
      }

      return true;
    }

    [[nodiscard]] bool read_uint64_value(
        const std::vector<std::string> &args,
        std::size_t &index,
        std::uint64_t &out)
    {
      std::string value;

      if (!read_value(args, index, value))
      {
        return false;
      }

      return parse_uint64(value, out);
    }

    [[nodiscard]] std::string format_timeout(std::uint64_t timeout_ms)
    {
      return std::to_string(timeout_ms) + "ms";
    }

    [[nodiscard]] bool parse_options(
        const std::vector<std::string> &args,
        Options &options)
    {
      if (args.empty())
      {
        options.show_help = true;
        return true;
      }

      options.subcommand = args[0];

      if (options.subcommand == "-h" ||
          options.subcommand == "--help" ||
          options.subcommand == "help")
      {
        options.show_help = true;
        return true;
      }

      if (options.subcommand != "ask" &&
          options.subcommand != "analyze" &&
          options.subcommand != "scan")
      {
        return false;
      }

      std::vector<std::string> positional;

      for (std::size_t i = 1; i < args.size(); ++i)
      {
        const std::string &arg = args[i];

        if (arg == "--workspace" || arg == "-w")
        {
          if (!read_value(args, i, options.workspace))
          {
            return false;
          }

          continue;
        }

        if (arg == "--provider")
        {
          if (!read_value(args, i, options.provider))
          {
            return false;
          }

          continue;
        }

        if (arg == "--model")
        {
          if (!read_value(args, i, options.model))
          {
            return false;
          }

          continue;
        }

        if (arg == "--model-url")
        {
          if (!read_value(args, i, options.model_url))
          {
            return false;
          }

          continue;
        }

        if (arg == "--timeout")
        {
          if (!read_uint64_value(args, i, options.timeout_ms))
          {
            return false;
          }

          continue;
        }

        if (arg == "--allow-process")
        {
          options.allow_process = true;
          continue;
        }

        if (arg == "--no-file-read")
        {
          options.allow_file_read = false;
          continue;
        }

        if (arg == "--no-cache")
        {
          options.use_cache = false;
          continue;
        }

        if (arg == "--no-memory")
        {
          options.persist_memory = false;
          continue;
        }

        if (arg == "--")
        {
          for (std::size_t j = i + 1; j < args.size(); ++j)
          {
            positional.push_back(args[j]);
          }

          break;
        }

        if (arg.rfind("-", 0) == 0)
        {
          return false;
        }

        positional.push_back(arg);
      }

      if (options.subcommand == "ask")
      {
        if (positional.empty())
        {
          return false;
        }

        options.input = join_args(positional, 0);
        return true;
      }

      if (options.subcommand == "analyze")
      {
        if (!positional.empty())
        {
          options.workspace = positional[0];

          if (positional.size() > 1)
          {
            options.input = join_args(positional, 1);
          }
        }

        if (options.input.empty())
        {
          options.input =
              "Analyze this project and explain the most important parts.";
        }

        return true;
      }

      if (options.subcommand == "scan")
      {
        if (!positional.empty())
        {
          options.workspace = positional[0];
        }

        return true;
      }

      return false;
    }

    [[nodiscard]] vix::ai::agent::AgentConfig make_config(
        const Options &options)
    {
      auto config = vix::ai::agent::AgentConfigLoader::from_environment();

      if (!options.provider.empty())
      {
        config.provider = options.provider;
      }

      if (!options.model.empty())
      {
        config.model = options.model;
      }

      if (!options.model_url.empty())
      {
        config.model_url = options.model_url;
      }

      if (options.timeout_ms > 0)
      {
        config.timeout_ms = options.timeout_ms;
      }

      config.allow_file_read = options.allow_file_read;
      config.allow_process = options.allow_process;
      config.allow_file_write = options.allow_file_write;
      config.use_cache = options.use_cache;
      config.persist_memory = options.persist_memory;

      if (config.allow_process)
      {
        config.allowed_programs = {
            "vix",
            "cmake",
            "ninja",
            "git",
            "ls",
            "cat",
            "echo"};
      }

      return config;
    }

    void print_agent_header(
        const std::string &action,
        const Options &options,
        const vix::ai::agent::AgentConfig &config)
    {
      std::vector<std::pair<std::string, std::string>> meta;

      meta.emplace_back("provider", config.provider);
      meta.emplace_back("model", config.model);
      meta.emplace_back("timeout", format_timeout(config.timeout_ms));
      meta.emplace_back("workspace", options.workspace);

      if (!config.model_url.empty())
      {
        meta.emplace_back("endpoint", config.model_url);
      }

      build::print_task_header_full(
          std::cout,
          action,
          "agent",
          "",
          meta);
    }

    void print_agent_progress(bool success)
    {
      const std::string status = success ? "done" : "failed";
      const char *line_color = success ? style::CYAN : style::RED;
      const char *status_color = success ? style::GREEN : style::RED;

      std::cout << "  "
                << line_color
                << "agent"
                << style::RESET
                << " "
                << line_color
                << "[============================]"
                << style::RESET
                << " "
                << status_color
                << status
                << style::RESET
                << "\n";
    }

    void print_agent_response(
        const vix::ai::agent::AgentResponse &response)
    {
      if (!response.text.empty())
      {
        std::cout << "\n";
        std::cout << response.text << "\n";
      }

      if (!response.run_id.empty() ||
          response.from_cache ||
          !response.tools.empty())
      {
        std::cout << "\n";
        std::cout << "  "
                  << style::CYAN
                  << "details:"
                  << style::RESET
                  << "\n";
      }

      if (!response.run_id.empty())
      {
        std::cout << "    "
                  << style::GRAY
                  << "run id: "
                  << style::RESET
                  << response.run_id
                  << "\n";
      }

      if (response.from_cache)
      {
        std::cout << "    "
                  << style::GRAY
                  << "cache: "
                  << style::RESET
                  << style::GREEN
                  << "hit"
                  << style::RESET
                  << "\n";
      }

      if (!response.tools.empty())
      {
        std::cout << "    "
                  << style::GRAY
                  << "tools: "
                  << style::RESET
                  << response.tools.size()
                  << "\n";

        for (const auto &tool : response.tools)
        {
          std::cout << "      "
                    << (tool.ok ? style::GREEN : style::RED)
                    << (tool.ok ? "✔ " : "✖ ")
                    << style::RESET
                    << tool.name
                    << "\n";
        }
      }
    }

    [[nodiscard]] int run_scan(const Options &options)
    {
      auto config = make_config(options);

      print_agent_header("Scanning", options, config);

      const auto start = std::chrono::steady_clock::now();

      auto workspace =
          vix::ai::agent::AgentWorkspace::open(options.workspace, config);

      if (!workspace)
      {
        const auto end = std::chrono::steady_clock::now();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start)
                .count();

        print_agent_progress(false);
        build::print_task_failure_timed(
            std::cout,
            "Agent workspace error",
            ms);

        style::error(std::string(workspace.error().message()));
        return 1;
      }

      vix::ai::agent::FileScanPolicy policy(config);
      vix::ai::agent::ProjectScanner scanner(workspace.value(), policy);

      auto scan = scanner.scan();

      const auto end = std::chrono::steady_clock::now();
      const auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              end - start)
              .count();

      if (!scan)
      {
        print_agent_progress(false);
        build::print_task_failure_timed(
            std::cout,
            "Agent scan failed",
            ms);

        style::error(std::string(scan.error().message()));
        return 1;
      }

      print_agent_progress(true);
      build::print_task_success_timed(
          std::cout,
          "Scanned workspace",
          ms);

      std::cout << "\n";
      std::cout << "  "
                << style::CYAN
                << "summary:"
                << style::RESET
                << "\n";

      std::cout << "    "
                << style::GRAY
                << "workspace: "
                << style::RESET
                << scan.value().root
                << "\n";

      std::cout << "    "
                << style::GRAY
                << "files: "
                << style::RESET
                << scan.value().files.size()
                << "\n";

      std::cout << "    "
                << style::GRAY
                << "skipped: "
                << style::RESET
                << scan.value().skipped
                << "\n";

      std::cout << "    "
                << style::GRAY
                << "truncated: "
                << style::RESET
                << (scan.value().truncated ? "yes" : "no")
                << "\n";

      if (!scan.value().files.empty())
      {
        std::cout << "\n";
        std::cout << "  "
                  << style::CYAN
                  << "files:"
                  << style::RESET
                  << "\n";

        for (const auto &file : scan.value().files)
        {
          std::cout << "    "
                    << style::GRAY
                    << "• "
                    << style::RESET
                    << file.relative_path
                    << " "
                    << style::GRAY
                    << "("
                    << file.size
                    << " bytes)"
                    << style::RESET
                    << "\n";
        }
      }

      return 0;
    }

    [[nodiscard]] int run_agent_request(
        const Options &options,
        vix::ai::agent::AgentRequestMode mode)
    {
      auto config = make_config(options);

      auto err = vix::ai::agent::AgentConfigValidator::validate(config);

      if (err)
      {
        style::error("Agent config error: " +
                     std::string(err.message()));
        return 1;
      }

      const std::string action =
          mode == vix::ai::agent::AgentRequestMode::Analyze
              ? "Analyzing"
              : "Asking";

      print_agent_header(action, options, config);

      vix::ai::agent::Agent agent(config);

      vix::ai::agent::AgentRequest request;
      request.workspace = options.workspace;
      request.input = options.input;
      request.mode = mode;

      if (mode == vix::ai::agent::AgentRequestMode::Analyze)
      {
        request.context =
            "You are analyzing a local C++ project.\n"
            "The user wants a real explanation of the project architecture.\n"
            "Do not return example JSON.\n"
            "Do not invent unrelated technologies.\n"
            "Explain what this repository appears to contain, based on the scanned workspace and available project files.\n"
            "Focus on modules, folders, build system, CLI commands, runtime components, and how the pieces fit together.\n";
      }

      request.allow_tools = true;
      request.allow_file_read = options.allow_file_read;
      request.allow_process = options.allow_process;
      request.allow_file_write = options.allow_file_write;
      request.use_cache = options.use_cache;
      request.timeout_ms = config.timeout_ms;

      const auto start = std::chrono::steady_clock::now();

      auto response = agent.run(request);

      const auto end = std::chrono::steady_clock::now();
      const auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              end - start)
              .count();

      if (!response)
      {
        print_agent_progress(false);

        build::print_task_failure_timed(
            std::cout,
            "Agent request failed",
            ms);

        std::cout << "\n";
        style::error(std::string(response.error().message()));

        if (config.provider == "ollama")
        {
          style::hint(
              "If the model is slow on CPU, try a smaller prompt or `--timeout 300000`.");
          style::hint(
              "For a lighter local demo, run `ollama pull qwen2.5-coder:1.5b`.");
          style::hint(
              "Then use `--model qwen2.5-coder:1.5b`.");
        }

        return 1;
      }

      print_agent_progress(true);

      build::print_task_success_timed(
          std::cout,
          "Completed agent request",
          ms);

      print_agent_response(response.value());

      return 0;
    }
  }

  int help()
  {
    std::cout
        << "Usage:\n"
        << "  vix agent ask <prompt> [options]\n"
        << "  vix agent analyze [workspace] [prompt] [options]\n"
        << "  vix agent scan [workspace] [options]\n"
        << "\n"
        << "Options:\n"
        << "  -w, --workspace <path>     Workspace directory\n"
        << "      --provider <name>      Model provider, default from VIX_AGENT_PROVIDER or ollama\n"
        << "      --model <name>         Model name, default from VIX_AGENT_MODEL or llama3\n"
        << "      --model-url <url>      Model endpoint, default from VIX_AGENT_MODEL_URL\n"
        << "      --timeout <ms>         Model request timeout in milliseconds\n"
        << "      --allow-process        Allow safe command.run tool\n"
        << "      --no-file-read         Disable workspace file reading\n"
        << "      --no-cache             Disable cache\n"
        << "      --no-memory            Disable run history and memory persistence\n"
        << "\n"
        << "Examples:\n"
        << "  vix agent ask \"Explain Vix.cpp in simple words\"\n"
        << "  vix agent ask \"Explain Vix.cpp\" --timeout 120000\n"
        << "  vix agent ask \"Explain this code\" --model qwen2.5-coder:1.5b --timeout 120000\n"
        << "  vix agent analyze .\n"
        << "  vix agent scan .\n"
        << "  vix agent ask \"Run vix tests if useful\" --allow-process\n"
        << "\n"
        << "Ollama demo:\n"
        << "  ollama pull qwen2.5-coder:1.5b\n"
        << "  vix agent ask \"Explain Vix.cpp\" --model qwen2.5-coder:1.5b --timeout 120000\n";

    return 0;
  }

  int run(const std::vector<std::string> &args)
  {
    Options options;

    if (!parse_options(args, options))
    {
      return help();
    }

    if (options.show_help)
    {
      return help();
    }

    if (options.subcommand == "scan")
    {
      return run_scan(options);
    }

    if (options.subcommand == "analyze")
    {
      return run_agent_request(
          options,
          vix::ai::agent::AgentRequestMode::Analyze);
    }

    return run_agent_request(
        options,
        vix::ai::agent::AgentRequestMode::Chat);
  }
}
