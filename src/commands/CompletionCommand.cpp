/**
 *
 *  @file CompletionCommand.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/CompletionCommand.hpp>
#include <vix/cli/commands/Dispatch.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

namespace vix::commands
{
  namespace
  {
    std::vector<std::string> collect_commands()
    {
      std::vector<std::string> names;

      const auto &entries = vix::cli::dispatch::global().entries();
      names.reserve(entries.size());

      for (const auto &[name, _] : entries)
      {
        names.push_back(name);
      }

      std::sort(names.begin(), names.end());
      return names;
    }

    int print_bash()
    {
      const auto commands = collect_commands();

      std::cout << "_vix_completions()\n";
      std::cout << "{\n";
      std::cout << "  local cur prev\n";
      std::cout << "  cur=\"${COMP_WORDS[COMP_CWORD]}\"\n";
      std::cout << "  prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n\n";

      std::cout << "  local commands=\"";
      for (size_t i = 0; i < commands.size(); ++i)
      {
        if (i != 0)
          std::cout << " ";
        std::cout << commands[i];
      }
      std::cout << "\"\n\n";

      std::cout << "  if [[ ${COMP_CWORD} -eq 1 ]]; then\n";
      std::cout << "    COMPREPLY=( $(compgen -W \"$commands\" -- \"$cur\") )\n";
      std::cout << "    return 0\n";
      std::cout << "  fi\n\n";

      std::cout << "  case \"${COMP_WORDS[1]}\" in\n";
      std::cout << "    help)\n";
      std::cout << "      COMPREPLY=( $(compgen -W \"$commands\" -- \"$cur\") )\n";
      std::cout << "      return 0\n";
      std::cout << "      ;;\n";
      std::cout << "  esac\n\n";

      std::cout << "  COMPREPLY=()\n";
      std::cout << "}\n\n";

      std::cout << "complete -o default -F _vix_completions vix\n";
      return 0;
    }
  }

  int CompletionCommand::run(const std::vector<std::string> &args)
  {
    if (args.empty() || args[0] == "bash")
      return print_bash();

    if (args[0] == "--help" || args[0] == "-h")
      return help();

    std::cerr << "vix completion: unsupported shell '" << args[0] << "'\n";
    std::cerr << "Supported: bash\n";
    return 1;
  }

  int CompletionCommand::help()
  {
    std::cout
        << "vix completion [bash]\n\n"
        << "Generate shell completion script.\n\n"
        << "Examples:\n"
        << "  vix completion bash > ~/.vix-completion.bash\n"
        << "  source ~/.vix-completion.bash\n";
    return 0;
  }
}
