/**
 *
 *  @file ReplLineEditor.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RELP_LINE_EDITOR_HPP
#define VIX_RELP_LINE_EDITOR_HPP

#include <string>
#include <vector>
#include <functional>

namespace vix::cli::repl
{
  enum class ReadStatus
  {
    Ok,
    Eof,
    Interrupted,
    Clear
  };

  struct CompletionResult
  {
    bool changed = false;
    std::string newLine;
    std::vector<std::string> suggestions; // printed when multiple matches
  };

  using CompletionFn = std::function<CompletionResult(const std::string &currentLine)>;

  // Return true if line changed
  using HistoryNavFn = std::function<bool(std::string &line)>;

  ReadStatus read_line_edit(
      const std::string &prompt,
      std::string &outLine,
      const CompletionFn &completer,
      const HistoryNavFn &onHistoryUp,
      const HistoryNavFn &onHistoryDown);
}

#endif
