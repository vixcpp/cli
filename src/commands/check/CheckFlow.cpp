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
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;

  namespace
  {
    static bool is_option(std::string_view s)
    {
      return !s.empty() && s.front() == '-';
    }

    static std::optional<std::string> take_attached_value(
        const std::string &arg,
        const char *prefix)
    {
      if (arg.rfind(prefix, 0) != 0)
        return std::nullopt;

      std::string value = arg.substr(std::char_traits<char>::length(prefix));
      if (value.empty())
        return std::nullopt;

      return value;
    }

    static std::optional<std::string> pick_dir_opt_local(const std::vector<std::string> &args)
    {
      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const auto &a = args[i];

        if (a == "-d" || a == "--dir")
        {
          if (i + 1 < args.size() && !is_option(args[i + 1]))
            return args[i + 1];
          return std::nullopt;
        }

        if (auto v = take_attached_value(a, "--dir="))
          return v;
      }

      return std::nullopt;
    }

    static int parse_int_or_default(const std::string &value, int fallback)
    {
      try
      {
        return std::stoi(value);
      }
      catch (...)
      {
        return fallback;
      }
    }

    static int parse_non_negative_int_or_default(const std::string &value, int fallback)
    {
      try
      {
        return std::max(0, std::stoi(value));
      }
      catch (...)
      {
        return fallback;
      }
    }

    static bool looks_like_cpp_file(const std::string &arg)
    {
      const fs::path p(arg);
      return p.extension() == ".cpp";
    }

    static bool is_help_flag(const std::string &arg)
    {
      return arg == "-h" || arg == "--help";
    }
  } // namespace

  Options parse(const std::vector<std::string> &args)
  {
    Options o;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const auto &a = args[i];

      if (is_help_flag(a))
      {
        continue;
      }
      else if (a == "--preset")
      {
        if (i + 1 < args.size())
          o.preset = args[++i];
      }
      else if (auto preset_value = take_attached_value(a, "--preset="))
      {
        o.preset = *preset_value;
      }
      else if (a == "-j" || a == "--jobs")
      {
        if (i + 1 < args.size())
          o.jobs = parse_int_or_default(args[++i], 0);
      }
      else if (auto jobs_value = take_attached_value(a, "--jobs="))
      {
        o.jobs = parse_int_or_default(*jobs_value, 0);
      }
      else if (a == "--quiet" || a == "-q")
      {
        o.quiet = true;
      }
      else if (a == "--verbose")
      {
        o.verbose = true;
      }
      else if (a == "--full")
      {
        o.full = true;
      }
      else if (a == "--log-level" || a == "--loglevel")
      {
        if (i + 1 < args.size())
          o.logLevel = args[++i];
      }
      else if (auto log_level_value = take_attached_value(a, "--log-level="))
      {
        o.logLevel = *log_level_value;
      }
      else if (auto loglevel_value = take_attached_value(a, "--loglevel="))
      {
        o.logLevel = *loglevel_value;
      }
      else if (a == "--san")
      {
        o.enableSanitizers = true;
      }
      else if (a == "--ubsan")
      {
        o.enableUbsanOnly = true;
      }
      else if (a == "--tests")
      {
        o.tests = true;
      }
      else if (a == "--build-preset")
      {
        if (i + 1 < args.size())
          o.buildPreset = args[++i];
      }
      else if (auto build_preset_value = take_attached_value(a, "--build-preset="))
      {
        o.buildPreset = *build_preset_value;
      }
      else if (a == "--ctest-preset")
      {
        if (i + 1 < args.size())
          o.ctestPreset = args[++i];
      }
      else if (auto ctest_preset_value = take_attached_value(a, "--ctest-preset="))
      {
        o.ctestPreset = *ctest_preset_value;
      }
      else if (a == "--ctest-arg")
      {
        if (i + 1 < args.size())
          o.ctestArgs.push_back(args[++i]);
      }
      else if (auto ctest_arg_value = take_attached_value(a, "--ctest-arg="))
      {
        o.ctestArgs.push_back(*ctest_arg_value);
      }
      else if (a == "--run")
      {
        o.runAfterBuild = true;
      }
      else if (a == "--run-timeout")
      {
        if (i + 1 < args.size())
          o.runTimeoutSec = parse_non_negative_int_or_default(args[++i], 0);
      }
      else if (auto run_timeout_value = take_attached_value(a, "--run-timeout="))
      {
        o.runTimeoutSec = parse_non_negative_int_or_default(*run_timeout_value, 0);
      }
      else if (!a.empty() && a != "--" && !is_option(a))
      {
        if (looks_like_cpp_file(a))
        {
          o.singleCpp = true;
          o.cppFile = fs::absolute(fs::path(a));
        }
      }
    }

    if (auto dir_value = pick_dir_opt_local(args))
      o.dir = *dir_value;

    if (o.enableUbsanOnly)
      o.enableSanitizers = false;

    if ((o.enableSanitizers || o.enableUbsanOnly) && !o.singleCpp)
      o.runAfterBuild = true;

    if (o.quiet && o.verbose)
      o.verbose = false;

    return o;
  }

} // namespace vix::commands::CheckCommand::detail
