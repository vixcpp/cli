/**
 * @file ModulesTypes.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Shared types for the `vix modules` command.
 */
#ifndef VIX_CLI_MODULES_TYPES_HPP
#define VIX_CLI_MODULES_TYPES_HPP

#include <string>

namespace vix::commands::modules_cmd
{

  /// Parsed CLI arguments for `vix modules`.
  struct Options
  {
    std::string subcmd;  ///< "init" | "add" | "check" | "help"
    std::string dir;     ///< --dir value (empty = cwd)
    std::string project; ///< --project override (empty = auto-detect)
    std::string module;  ///< module name for `add`

    bool patchRoot = true; ///< patch root CMakeLists.txt on init
    bool patchLink = true; ///< auto-link module into main target on add
    bool showHelp = false;
  };

} // namespace vix::commands::modules_cmd

#endif
