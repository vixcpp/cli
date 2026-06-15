/**
 *
 *  @file RunnableExecutableResolver.hpp
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
#ifndef VIX_RUN_RUNNABLE_EXECUTABLE_RESOLVER_HPP
#define VIX_RUN_RUNNABLE_EXECUTABLE_RESOLVER_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::commands::RunCommand::detail
{
  /**
   * @brief Find executable files produced inside a CMake build directory.
   *
   * This function scans the build directory recursively and returns files that
   * are runnable on the current platform. CMake internal folders and Vix cache
   * folders are ignored.
   *
   * @param buildDir Build directory to scan.
   * @param includeTests Whether test binaries should be included.
   * @return List of runnable executable paths.
   */
  std::vector<std::filesystem::path> find_runnable_executables(
      const std::filesystem::path &buildDir,
      bool includeTests = false);

  /**
   * @brief Resolve the executable that should be run from a build directory.
   *
   * If a preferred target name is provided, the resolver first tries to match an
   * executable with that name. If no preferred name is provided, or no match is
   * found, the resolver returns the only runnable executable when exactly one
   * exists.
   *
   * When multiple runnable executables exist and no unique target can be chosen,
   * this function returns std::nullopt.
   *
   * @param buildDir Build directory to scan.
   * @param preferredName Optional executable or target name to prefer.
   * @return Resolved executable path, or std::nullopt if resolution is ambiguous.
   */
  std::optional<std::filesystem::path> resolve_runnable_executable(
      const std::filesystem::path &buildDir,
      const std::string &preferredName = {});

  /**
   * @brief Print runnable executable candidates for the user.
   *
   * The output is intended for diagnostics when Vix cannot automatically choose
   * one executable from a build directory.
   *
   * @param buildDir Build directory to scan.
   */
  void print_runnable_executable_candidates(
      const std::filesystem::path &buildDir);

  /**
   * @brief Return a user-facing path for a runnable executable.
   *
   * The resolver prefers a path relative to the current working directory when
   * possible. If that cannot be computed, it falls back to the absolute path.
   *
   * @param p Executable path.
   * @return Display path suitable for CLI hints.
   */
  std::string runnable_executable_display_path(
      const std::filesystem::path &p);
}

#endif // VIX_RUN_RUNNABLE_EXECUTABLE_RESOLVER_HPP
