/**
 *
 *  @file Process.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_PROCESS_HPP
#define VIX_CLI_PROCESS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vix::cli::process
{
  namespace fs = std::filesystem;

  enum class LinkerMode
  {
    Auto,
    Default,
    Mold,
    Lld
  };

  enum class LauncherMode
  {
    Auto,
    None,
    Sccache,
    Ccache
  };

  struct Options
  {
    // required by spec
    std::string preset = "dev-ninja"; // dev | dev-ninja | release
    std::string targetTriple;         // --target <triple>
    std::string sysroot;
    bool linkStatic = false; // --static

    // build controls
    int jobs = 0;       // -j / --jobs
    bool clean = false; // --clean (force reconfigure)
    bool quiet = false; // -q / --quiet
    std::string dir;    // --dir/-d (optional)

    // performance switches
    bool fast = false;    // --fast
    bool useCache = true; // --no-cache
    LinkerMode linker = LinkerMode::Auto;
    LauncherMode launcher = LauncherMode::Auto;
    bool status = true;        // --no-status
    bool dryUpToDate = true;   // --no-up-to-date
    bool cmakeVerbose = false; // --cmake-verbose
    std::string buildTarget;   // --build-target <name>
    std::vector<std::string> cmakeArgs;
  };

  struct ExecResult
  {
    int exitCode = 0;
    std::string displayCommand;
    bool producedOutput = false;
    std::string capturedFirstLine;
  };

  struct Preset
  {
    std::string name;
    std::string generator;    // "Ninja"
    std::string buildType;    // "Debug"/"Release"
    std::string buildDirName; // "build-dev-ninja"
  };

  struct Plan
  {
    fs::path projectDir;
    Preset preset;
    fs::path buildDir;
    fs::path configureLog;
    fs::path buildLog;
    fs::path sigFile;
    fs::path toolchainFile;

    std::vector<std::pair<std::string, std::string>> cmakeVars;
    std::string signature;

    std::optional<std::string> launcher;
    std::optional<std::string> fastLinkerFlag;
    std::string projectFingerprint;
  };

  [[nodiscard]] int normalize_exit_code(int raw) noexcept;

} // namespace vix::cli::process

#endif
