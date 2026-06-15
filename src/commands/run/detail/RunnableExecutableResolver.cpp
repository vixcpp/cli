/**
 *
 *  @file RunnableExecutableResolver.cpp
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
#include <vix/cli/commands/run/detail/RunnableExecutableResolver.hpp>
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  namespace
  {
    bool is_executable_candidate(const fs::path &p)
    {
      std::error_code ec{};

      if (!fs::is_regular_file(p, ec) || ec)
        return false;

#ifdef _WIN32
      return p.extension() == ".exe";
#else
      const auto perms = fs::status(p, ec).permissions();
      if (ec)
        return false;

      using pr = fs::perms;
      return (perms & pr::owner_exec) != pr::none ||
             (perms & pr::group_exec) != pr::none ||
             (perms & pr::others_exec) != pr::none;
#endif
    }

    bool looks_like_test_binary(const fs::path &p)
    {
      const std::string n = p.filename().string();

      return n.find("_test") != std::string::npos ||
             n.find("_tests") != std::string::npos ||
             n.rfind("test_", 0) == 0;
    }

    bool should_skip_path(const fs::path &p)
    {
      const std::string s = p.string();

      return s.find("CMakeFiles") != std::string::npos ||
             s.find(".vix") != std::string::npos ||
             s.find("/Testing/") != std::string::npos ||
             s.find("\\Testing\\") != std::string::npos;
    }
  }

  std::string runnable_executable_display_path(const fs::path &p)
  {
    std::error_code ec;
    fs::path rel = fs::relative(p, fs::current_path(), ec);

    return ec ? p.string() : rel.string();
  }

  std::vector<fs::path> find_runnable_executables(
      const fs::path &buildDir,
      bool includeTests)
  {
    std::vector<fs::path> candidates;

    std::error_code ec{};
    if (!fs::exists(buildDir, ec) || ec)
      return candidates;

    for (auto it = fs::recursive_directory_iterator(
             buildDir,
             fs::directory_options::skip_permission_denied,
             ec);
         !ec && it != fs::recursive_directory_iterator();
         ++it)
    {
      const fs::path p = it->path();

      if (should_skip_path(p))
        continue;

      if (!is_executable_candidate(p))
        continue;

      if (!includeTests && looks_like_test_binary(p))
        continue;

      candidates.push_back(p);
    }

    auto prefer_bin_and_short_path = [](const fs::path &a, const fs::path &b)
    {
      const std::string as = a.string();
      const std::string bs = b.string();

      const bool aBin =
          as.find("/bin/") != std::string::npos ||
          as.find("\\bin\\") != std::string::npos;

      const bool bBin =
          bs.find("/bin/") != std::string::npos ||
          bs.find("\\bin\\") != std::string::npos;

      if (aBin != bBin)
        return aBin;

      if (as.size() != bs.size())
        return as.size() < bs.size();

      return as < bs;
    };

    std::sort(candidates.begin(), candidates.end(), prefer_bin_and_short_path);
    return candidates;
  }

  std::optional<fs::path> resolve_runnable_executable(
      const fs::path &buildDir,
      const std::string &preferredName)
  {
    const auto candidates = find_runnable_executables(buildDir, false);

    if (!preferredName.empty())
    {
      for (const auto &p : candidates)
      {
#ifdef _WIN32
        const std::string name = p.stem().string();
#else
        const std::string name = p.filename().string();
#endif

        if (name == preferredName)
          return p;
      }
    }

    if (candidates.size() == 1)
      return candidates.front();

    return std::nullopt;
  }

  void print_runnable_executable_candidates(const fs::path &buildDir)
  {
    const auto candidates = find_runnable_executables(buildDir, false);

    if (candidates.empty())
      return;

    hint("Runnable executables found:");

    for (const auto &p : candidates)
      step("  vix run " + runnable_executable_display_path(p));
  }
}
