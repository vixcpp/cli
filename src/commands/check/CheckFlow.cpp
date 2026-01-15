/**
 *
 *  @file CheckFlow.cpp
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
#include <vix/cli/commands/check/CheckDetail.hpp>
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <optional>
#include <string_view>

using namespace vix::cli::style;

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;

  static std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
  {
    auto is_opt = [](std::string_view s)
    { return !s.empty() && s.front() == '-'; };

    for (size_t i = 0; i < args.size(); ++i)
    {
      const auto &a = args[i];

      if (a == "-d" || a == "--dir")
      {
        if (i + 1 < args.size() && !is_opt(args[i + 1]))
          return args[i + 1];
        return std::nullopt;
      }

      constexpr const char pfx[] = "--dir=";
      if (a.rfind(pfx, 0) == 0)
      {
        std::string v = a.substr(sizeof(pfx) - 1);
        if (v.empty())
          return std::nullopt;
        return v;
      }
    }
    return std::nullopt;
  }

  Options parse(const std::vector<std::string> &args)
  {
    Options o;

    for (size_t i = 0; i < args.size(); ++i)
    {
      const auto &a = args[i];

      if (a == "--preset" && i + 1 < args.size())
        o.preset = args[++i];

      else if ((a == "-j" || a == "--jobs") && i + 1 < args.size())
      {
        try
        {
          o.jobs = std::stoi(args[++i]);
        }
        catch (...)
        {
          o.jobs = 0;
        }
      }
      else if (a == "--quiet" || a == "-q")
        o.quiet = true;
      else if (a == "--verbose")
        o.verbose = true;
      else if ((a == "--log-level" || a == "--loglevel") && i + 1 < args.size())
        o.logLevel = args[++i];
      else if (a.rfind("--log-level=", 0) == 0)
        o.logLevel = a.substr(std::string("--log-level=").size());

      // script sanitizers
      else if (a == "--san")
        o.enableSanitizers = true;
      else if (a == "--ubsan")
        o.enableUbsanOnly = true;

      // project checks
      else if (a == "--tests")
        o.tests = true;
      else if (a == "--build-preset" && i + 1 < args.size())
        o.buildPreset = args[++i];
      else if (a == "--ctest-preset" && i + 1 < args.size())
        o.ctestPreset = args[++i];

      // extra ctest args
      else if (a == "--ctest-arg" && i + 1 < args.size())
      {
        o.ctestArgs.push_back(args[++i]);
      }

      // runtime check
      else if (a == "--run")
      {
        o.runAfterBuild = true;
      }
      else if (a == "--run-timeout" && i + 1 < args.size())
      {
        try
        {
          o.runTimeoutSec = std::max(0, std::stoi(args[++i]));
        }
        catch (...)
        {
          o.runTimeoutSec = 0;
        }
      }

      // positional
      else if (!a.empty() && a != "--" && a[0] != '-')
      {
        fs::path p{a};
        if (p.extension() == ".cpp")
        {
          o.singleCpp = true;
          o.cppFile = fs::absolute(p);
        }
      }
    }

    if (auto d = pick_dir_opt_local(args))
      o.dir = *d;

    if (o.enableUbsanOnly)
      o.enableSanitizers = false;

    return o;
  }

} // namespace vix::commands::CheckCommand::detail
