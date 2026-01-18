/**
 *
 *  @file Process_prosix.cpp
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

#ifndef _WIN32
#include <sys/wait.h>

namespace vix::cli::process
{
  int normalize_exit_code(int raw) noexcept
  {
    if (raw == -1)
      return 127;

    if (WIFEXITED(raw))
      return WEXITSTATUS(raw);

    if (WIFSIGNALED(raw))
      return 128 + WTERMSIG(raw);

    return raw;
  }
} // namespace vix::cli::process

#endif // !_WIN32
