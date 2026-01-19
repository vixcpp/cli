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

#include <filesystem>
#include <string>
#include <vector>
#include <system_error>

namespace vix::commands::TestsCommand::detail
{
  namespace fs = std::filesystem;

  static bool looks_like_path(const std::string &s)
  {
    if (s.empty())
      return false;

    if (s[0] == '-')
      return false;

    if (s.find('/') != std::string::npos)
      return true;

    if (s.rfind("./", 0) == 0 || s.rfind("../", 0) == 0)
      return true;

    std::error_code ec;
    return fs::exists(fs::path(s), ec);
  }

  static fs::path normalize_project_dir(const fs::path &p)
  {
    std::error_code ec;

    fs::path abs = fs::absolute(p, ec);
    if (ec)
      abs = p;

    fs::path canon = fs::weakly_canonical(abs, ec);
    if (ec)
      return abs;

    return canon;
  }

  Options parse(const std::vector<std::string> &args)
  {
    Options opt{};
    opt.forwarded.reserve(args.size() + 8);
    opt.ctestArgs.reserve(16);

    std::vector<std::string> left;
    left.reserve(args.size());

    bool afterSep = false;
    for (const auto &a : args)
    {
      if (!afterSep && a == "--")
      {
        afterSep = true;
        continue;
      }

      if (afterSep)
        opt.ctestArgs.push_back(a);
      else
        left.push_back(a);
    }

    bool projectSet = false;

    for (std::size_t i = 0; i < left.size(); ++i)
    {
      const std::string &a = left[i];

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

      if (!projectSet && looks_like_path(a))
      {
        opt.projectDir = normalize_project_dir(fs::path(a));
        projectSet = true;
        continue;
      }

      opt.forwarded.push_back(a);
    }

    if (!projectSet)
      opt.projectDir = normalize_project_dir(fs::current_path());

    if (opt.list)
      opt.ctestArgs.insert(opt.ctestArgs.begin(), "--show-only");

    if (opt.failFast)
      opt.ctestArgs.push_back("--stop-on-failure");

    if (opt.runAfter)
      opt.forwarded.push_back("--run");

    return opt;
  }

} // namespace vix::commands::TestsCommand::detail
