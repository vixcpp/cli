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

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <vix/ai/agent/agent.hpp>

namespace vix::commands::AgentCommand
{
  namespace
  {
    struct Options
    {
      std::string subcommand;
      std::string input;
      std::string workspace{"."};
      std::string provider;
      std::string model;
      std::string model_url;

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

      if (options.subcommand == "-h" || options.subcommand == "--help" ||
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
          options.input = join_args(args, i + 1);
          break;
        }

        if (arg.rfind("-", 0) == 0)
        {
          return false;
        }

        if (options.subcommand == "analyze" || options.subcommand == "scan")
        {
          options.workspace = arg;

          if (i + 1 < args.size())
          {
            options.input = join_args(args, i + 1);
          }

          break;
        }

        options.input = join_args(args, i);
        break;
      }

      if (options.subcommand == "ask" && options.input.empty())
      {
        return false;
      }

      if (options.subcommand == "analyze" && options.input.empty())
      {
        options.input = "Analyze this project and explain the most important parts.";
      }

      return true;
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

    [[nodiscard]] int run_scan(const Options &options)
    {
      auto config = make_config(options);

      auto workspace =
          vix::ai::agent::AgentWorkspace::open(options.workspace, config);

      if (!workspace)
      {
        std::cerr << "Agent workspace error: "
                  << workspace.error().message()
                  << '\n';
        return 1;
      }

      vix::ai::agent::FileScanPolicy policy(config);
      vix::ai::agent::ProjectScanner scanner(workspace.value(), policy);

      auto scan = scanner.scan();

      if (!scan)
      {
        std::cerr << "Agent scan error: "
                  << scan.error().message()
                  << '\n';
        return 1;
      }

      std::cout << "Workspace: " << scan.value().root << '\n';
      std::cout << "Files: " << scan.value().files.size() << '\n';
      std::cout << "Skipped: " << scan.value().skipped << '\n';
      std::cout << "Truncated: "
                << (scan.value().truncated ? "yes" : "no")
                << "\n\n";

      for (const auto &file : scan.value().files)
      {
        std::cout << "- " << file.relative_path
                  << " (" << file.size << " bytes)"
                  << '\n';
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
        std::cerr << "Agent config error: "
                  << err.message()
                  << '\n';
        return 1;
      }

      vix::ai::agent::Agent agent(config);

      vix::ai::agent::AgentRequest request;
      request.workspace = options.workspace;
      request.input = options.input;
      request.mode = mode;

      request.allow_tools = true;
      request.allow_file_read = options.allow_file_read;
      request.allow_process = options.allow_process;
      request.allow_file_write = options.allow_file_write;
      request.use_cache = options.use_cache;

      auto response = agent.run(request);

      if (!response)
      {
        std::cerr << "Agent error: "
                  << response.error().message()
                  << '\n';
        return 1;
      }

      std::cout << response.value().text << '\n';

      if (!response.value().run_id.empty())
      {
        std::cout << "\nRun id: " << response.value().run_id << '\n';
      }

      if (response.value().from_cache)
      {
        std::cout << "From cache: yes\n";
      }

      if (!response.value().tools.empty())
      {
        std::cout << "Tools used: "
                  << response.value().tools.size()
                  << '\n';

        for (const auto &tool : response.value().tools)
        {
          std::cout << "- " << tool.name
                    << " "
                    << (tool.ok ? "ok" : "failed")
                    << '\n';
        }
      }

      return 0;
    }
  }

  int help()
  {
    std::cout
        << "Usage:\n"
        << "  vix agent ask <prompt> [options]\n"
        << "  vix agent analyze [workspace] [prompt] [options]\n"
        << "  vix agent scan [workspace]\n"
        << "\n"
        << "Options:\n"
        << "  -w, --workspace <path>   Workspace directory\n"
        << "      --provider <name>    Model provider, default from VIX_AGENT_PROVIDER or ollama\n"
        << "      --model <name>       Model name, default from VIX_AGENT_MODEL or llama3\n"
        << "      --model-url <url>    Model endpoint, default from VIX_AGENT_MODEL_URL\n"
        << "      --allow-process      Allow safe command.run tool\n"
        << "      --no-file-read       Disable workspace file reading\n"
        << "      --no-cache           Disable cache\n"
        << "      --no-memory          Disable run history\n"
        << "\n"
        << "Examples:\n"
        << "  vix agent ask \"Explain Vix.cpp in simple words\"\n"
        << "  vix agent analyze .\n"
        << "  vix agent scan .\n"
        << "  vix agent ask \"Run vix tests if useful\" --allow-process\n";

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
