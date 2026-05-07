/**
 *
 *  @file DevRebuilder.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Dev mode project rebuilder
 *
 */

#ifndef VIX_CLI_COMMANDS_RUN_DEV_DEV_REBUILDER_HPP
#define VIX_CLI_COMMANDS_RUN_DEV_DEV_REBUILDER_HPP

#include <filesystem>
#include <string>

#include <vix/cli/commands/run/RunDetail.hpp>

namespace vix::commands::RunCommand::dev
{
  namespace fs = std::filesystem;
  namespace detail = vix::commands::RunCommand::detail;

  struct DevRebuilderOptions
  {
    fs::path projectDir;
    fs::path buildDir;
    std::string targetName;

    detail::Options runOptions;

    bool quiet{false};
  };

  struct DevRebuilderResult
  {
    bool ok{false};
    bool configured{false};
    bool built{false};

    int exitCode{0};
    std::string message;
    std::string output;
  };

  class DevRebuilder
  {
  public:
    explicit DevRebuilder(DevRebuilderOptions options);

    const DevRebuilderOptions &options() const;

    DevRebuilderResult ensure_configured() const;
    DevRebuilderResult rebuild() const;
    DevRebuilderResult reconfigure_and_rebuild() const;

  private:
    DevRebuilderOptions options_;

    bool has_cmake_cache() const;

    std::string configure_command() const;
    std::string build_command() const;

    DevRebuilderResult run_configure_command() const;
    DevRebuilderResult run_build_command() const;
  };

} // namespace vix::commands::RunCommand::dev

#endif // VIX_CLI_COMMANDS_RUN_DEV_DEV_REBUILDER_HPP
