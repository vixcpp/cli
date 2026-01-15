/**
 *
 *  @file TestsFlow.cpp
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
#include <vix/cli/commands/tests/TestsDetail.hpp>

namespace vix::commands::TestsCommand::detail
{
  static fs::path pick_project_dir_from_args_or_cwd(const std::vector<std::string> &args)
  {
    for (const auto &a : args)
    {
      // first non-flag token is treated as a path
      if (!a.empty() && a[0] != '-')
        return fs::absolute(fs::path(a));
    }
    return fs::current_path();
  }

  Options parse(const std::vector<std::string> &args)
  {
    Options opt{};
    opt.forwarded.reserve(args.size() + 12);

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &a = args[i];

      if (a == "--watch")
      {
        opt.watch = true;
        continue;
      }
      if (a == "--list")
      {
        opt.list = true;
        continue;
      }
      if (a == "--fail-fast")
      {
        opt.failFast = true;
        continue;
      }
      if (a == "--run")
      {
        opt.runAfter = true;
        continue;
      }

      // everything else is forwarded to `vix check`
      opt.forwarded.push_back(a);
    }

    // resolve project dir BEFORE injecting "--tests"
    opt.projectDir = pick_project_dir_from_args_or_cwd(opt.forwarded);
    // Always run tests (alias)
    opt.forwarded.insert(opt.forwarded.begin(), "--tests");
    // Map tests flags -> ctest args
    if (opt.list)
    {
      opt.forwarded.push_back("--ctest-arg");
      opt.forwarded.push_back("-N");
    }

    if (opt.failFast)
    {
      opt.forwarded.push_back("--ctest-arg");
      opt.forwarded.push_back("--stop-on-failure");
    }

    if (opt.runAfter)
    {
      opt.forwarded.push_back("--run");
    }

    return opt;
  }
}
