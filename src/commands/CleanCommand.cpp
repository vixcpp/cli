/**
 *
 *  @file CleanCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/CleanCommand.hpp>
#include <vix/cli/util/Ui.hpp>

#include <filesystem>
#include <iostream>
#include <vector>
#include <string>

namespace fs = std::filesystem;

namespace vix::commands
{
  namespace
  {
    struct Target
    {
      std::string label;
      fs::path path;
    };

    static std::vector<Target> targets()
    {
      return {
          {".vix", fs::current_path() / ".vix"},
          {"build", fs::current_path() / "build"}};
    }

    static bool remove_dir(const fs::path &p)
    {
      std::error_code ec;
      fs::remove_all(p, ec);
      return !ec;
    }
  }

  int CleanCommand::run(const std::vector<std::string> &args)
  {
    (void)args;

    vix::cli::util::section(std::cout, "Clean");

    bool removedAny = false;

    for (const auto &t : targets())
    {
      if (!fs::exists(t.path))
        continue;

      vix::cli::util::info_line(std::cout, "removing " + t.label + "/");

      if (remove_dir(t.path))
      {
        removedAny = true;
      }
      else
      {
        vix::cli::util::warn_line(std::cout, "failed to remove " + t.label);
      }
    }

    vix::cli::util::one_line_spacer(std::cout);

    if (removedAny)
    {
      vix::cli::util::ok_line(std::cout, "Project cache cleaned");
    }
    else
    {
      vix::cli::util::warn_line(std::cout, "Nothing to clean");
    }

    return 0;
  }

  int CleanCommand::help()
  {
    std::cout
        << "vix clean\n"
        << "Remove local project cache directories.\n\n"

        << "Usage\n"
        << "  vix clean\n\n"

        << "What it removes\n"
        << "  • .vix/\n"
        << "  • build/\n\n"

        << "Notes\n"
        << "  • Does NOT remove global cache (~/.vix)\n";

    return 0;
  }
}
