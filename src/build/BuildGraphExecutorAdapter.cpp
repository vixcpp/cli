/**
 *
 *  @file BuildGraphExecutorAdapter.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  CLI adapters for target-aware graph execution
 *
 */

#include <vix/cli/build/BuildGraphExecutorAdapter.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <vix/cli/cmake/CMakeBuild.hpp>

namespace vix::cli::build
{
  namespace
  {
    static bool graph_debug_logs_enabled()
    {
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (!level || !*level)
        return false;

      std::string value(level);

      for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return value == "debug" || value == "trace";
    }

    static std::string read_text_file_or_empty(const fs::path &path)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        return {};

      return std::string(
          std::istreambuf_iterator<char>(in),
          std::istreambuf_iterator<char>());
    }
  } // namespace

  BuildGraphExecutorNinjaResult execute_graph_ninja_target(
      const BuildGraphExecutorNinjaRequest &request,
      bool quiet)
  {
    BuildGraphExecutorNinjaResult result;

    const process::ExecResult execResult =
        run_process_live_to_log(
            request.command,
            {},
            request.buildDir / "build.log",
            quiet,
            /*cmakeVerbose=*/false,
            /*progressOnly=*/false);

    result.started = true;
    result.exitCode = execResult.exitCode;
    result.producedOutput = execResult.producedOutput;
    result.displayCommand = execResult.displayCommand;

    if (execResult.exitCode != 0 && quiet)
    {
      result.output =
          read_text_file_or_empty(request.buildDir / "build.log");
    }
    else if (execResult.producedOutput && !execResult.capturedFirstLine.empty())
    {
      result.output = execResult.capturedFirstLine + "\n";
    }

    return result;
  }

  void render_graph_debug_event(
      const BuildGraphExecutorEvent &event,
      bool quiet,
      bool verbose)
  {
    if (quiet)
      return;

    if (!verbose)
      return;

    if (!graph_debug_logs_enabled())
      return;

    if (event.message.empty())
      return;

    std::cout << "  " << event.message << "\n";
    std::cout.flush();
  }

} // namespace vix::cli::build
