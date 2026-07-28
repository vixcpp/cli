/**
 *
 *  @file Process_win.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/process/Process.hpp>

#include <vix/engine/Process.hpp>

#ifdef _WIN32

namespace vix::cli::process
{
  int normalize_exit_code(int raw) noexcept
  {
    return vix::engine::process::normalize_exit_code(raw);
  }
} // namespace vix::cli::process

#endif // _WIN32
