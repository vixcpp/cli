/**
 *
 *  @file RunDetail.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/commands/run/RunDetail.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace vix::commands::RunCommand::detail
{
  namespace
  {
    std::string read_cmake_cache_value(const fs::path &cacheFile, const std::string &key)
    {
      std::ifstream in(cacheFile);
      if (!in.is_open())
        return {};

      const std::string prefix = key + ":";
      std::string line;

      while (std::getline(in, line))
      {
        if (line.rfind(prefix, 0) != 0)
          continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos)
          continue;

        return line.substr(eq + 1);
      }

      return {};
    }
  } // namespace

  std::string guess_project_name_from_dir(const fs::path &projectDir)
  {
    std::string name = projectDir.filename().string();
    if (name.empty())
      name = "app";
    return name;
  }

  std::string resolve_build_type_from_cache_or_default(
      const fs::path &buildDir,
      const std::string &fallback)
  {
    const fs::path cacheFile = buildDir / "CMakeCache.txt";
    if (!fs::exists(cacheFile))
      return fallback;

    std::string bt = read_cmake_cache_value(cacheFile, "CMAKE_BUILD_TYPE");
    if (bt.empty())
      return fallback;

    return bt;
  }

  fs::path compute_runtime_executable_path(
      const fs::path &buildDir,
      const std::string &projectName,
      const std::string &configName)
  {
    std::string exeName = projectName;
#ifdef _WIN32
    exeName += ".exe";
#endif

    std::vector<fs::path> candidates;
    candidates.push_back(buildDir / exeName);
    candidates.push_back(buildDir / "bin" / exeName);
    candidates.push_back(buildDir / configName / exeName);
    candidates.push_back(buildDir / "bin" / configName / exeName);
    candidates.push_back(buildDir / "src" / exeName);
    candidates.push_back(buildDir / "src" / configName / exeName);

    for (const auto &candidate : candidates)
    {
      std::error_code ec;
      if (fs::exists(candidate, ec) && !ec)
        return candidate;
    }

    const fs::path cacheFile = buildDir / "CMakeCache.txt";
    if (fs::exists(cacheFile))
    {
      std::string outDir =
          read_cmake_cache_value(cacheFile, "CMAKE_RUNTIME_OUTPUT_DIRECTORY_" + configName);

      if (outDir.empty())
        outDir = read_cmake_cache_value(cacheFile, "CMAKE_RUNTIME_OUTPUT_DIRECTORY");

      if (!outDir.empty())
      {
        fs::path base = fs::path(outDir);
        if (base.is_relative())
          base = buildDir / base;

        const fs::path candidate = base / exeName;
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec)
          return candidate;
      }
    }

    return buildDir / exeName;
  }

  void apply_sanitizer_env_if_needed(
      bool enableSanitizers,
      bool enableUbsanOnly,
      bool enableThreadSanitizer)
  {
    if (!want_any_sanitizer(
            enableSanitizers,
            enableUbsanOnly,
            enableThreadSanitizer))
    {
      return;
    }

#ifdef _WIN32
    (void)enableUbsanOnly;
#else
    if (enableThreadSanitizer)
    {
      ::setenv(
          "TSAN_OPTIONS",
          "halt_on_error=1:"
          "second_deadlock_stack=1:"
          "history_size=7:"
          "color=never",
          1);
      return;
    }

    ::setenv(
        "UBSAN_OPTIONS",
        "halt_on_error=1:print_stacktrace=1:color=never",
        1);

    if (!enableUbsanOnly)
    {
      ::setenv(
          "ASAN_OPTIONS",
          "abort_on_error=1:"
          "detect_leaks=1:"
          "symbolize=1:"
          "allocator_may_return_null=1:"
          "fast_unwind_on_malloc=0:"
          "strict_init_order=1:"
          "check_initialization_order=1:"
          "color=never:"
          "quiet=1",
          1);
    }
#endif
  }
} // namespace vix::commands::RunCommand::detail
