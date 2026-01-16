/**
 *
 *  @file CodeFrame.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CODE_FRAME_HPP
#define VIX_CODE_FRAME_HPP

#include <string>
#include <vix/cli/errors/CompilerError.hpp>
#include <vix/cli/errors/ErrorContext.hpp>

namespace vix::cli::errors
{
  struct CodeFrameOptions
  {
    int contextLines = 1;
    int tabWidth = 4;       // tabs -> espaces
    int maxLineWidth = 120; // tronquage
  };

  // Prints a compact code frame + caret under the error column.
  // Safe: if the file can't be read, prints nothing.
  void printCodeFrame(
      const CompilerError &err,
      const ErrorContext &ctx,
      const CodeFrameOptions &opt = {});
}

#endif
