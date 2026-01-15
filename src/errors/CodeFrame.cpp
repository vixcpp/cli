/**
 *
 *  @file CodeFrame.cpp
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
#include <vix/cli/errors/CodeFrame.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <vix/cli/Style.hpp>
using namespace vix::cli::style;

namespace vix::cli::errors
{
  static bool readAllLines(const std::string &file, std::vector<std::string> &out)
  {
    std::ifstream in(file);
    if (!in.is_open())
      return false;

    std::string line;
    while (std::getline(in, line))
      out.push_back(line);

    return true;
  }

  static std::string expandTabs(const std::string &s, int tabWidth)
  {
    if (tabWidth <= 0)
      tabWidth = 4;

    std::string out;
    out.reserve(s.size());

    int col = 0;
    for (char ch : s)
    {
      if (ch == '\t')
      {
        int spaces = tabWidth - (col % tabWidth);
        out.append(static_cast<std::size_t>(spaces), ' ');
        col += spaces;
      }
      else
      {
        out.push_back(ch);
        col += 1;
      }
    }
    return out;
  }

  static std::string makeCaretLine(int caretCol1Based, int tabWidth, const std::string &originalLine)
  {
    std::string expanded = expandTabs(originalLine, tabWidth);

    int col = std::max(1, caretCol1Based);
    int maxPos = static_cast<int>(expanded.size()) + 1;
    col = std::min(col, maxPos);

    std::string caret;
    caret.reserve(static_cast<std::size_t>(col));
    caret.append(static_cast<std::size_t>(col - 1), ' ');
    caret.push_back('^');
    return caret;
  }

  static void printTruncatedLine(
      const std::string &line,
      const std::string &caret,
      int maxWidth)
  {
    if (maxWidth <= 0 || static_cast<int>(line.size()) <= maxWidth)
    {
      std::cerr << GRAY << "  " << line << RESET << "\n";
      std::cerr << GRAY << "  " << caret << RESET << "\n";
      return;
    }

    int caretPos = 0;
    for (char c : caret)
    {
      if (c == '^')
        break;
      caretPos++;
    }

    // Desired window [start, start+maxWidth)
    int half = maxWidth / 2;
    int start = std::max(0, caretPos - half);
    int end = std::min(static_cast<int>(line.size()), start + maxWidth);

    // Adjust start if we hit the end
    start = std::max(0, end - maxWidth);

    std::string slice = line.substr(static_cast<std::size_t>(start),
                                    static_cast<std::size_t>(end - start));

    // Build caret slice aligned with slice
    int caretInSlice = caretPos - start;
    caretInSlice = std::max(0, std::min(caretInSlice, maxWidth - 1));

    std::string caretSlice;
    caretSlice.reserve(static_cast<std::size_t>(maxWidth));
    caretSlice.append(static_cast<std::size_t>(caretInSlice), ' ');
    caretSlice.push_back('^');

    const bool leftCut = (start > 0);
    const bool rightCut = (end < static_cast<int>(line.size()));

    if (leftCut)
      slice = "…" + slice.substr(1);
    if (rightCut && !slice.empty())
      slice = slice.substr(0, slice.size() - 1) + "…";

    std::cerr << GRAY << "  " << slice << RESET << "\n";
    std::cerr << GRAY << "  " << caretSlice << RESET << "\n";
  }

  void printCodeFrame(
      const CompilerError &err,
      const ErrorContext &ctx,
      const CodeFrameOptions &opt)
  {
    (void)ctx;
    if (err.file.empty() || err.line <= 0)
      return;

    std::vector<std::string> lines;
    if (!readAllLines(err.file, lines))
      return;

    const int n = static_cast<int>(lines.size());
    if (err.line > n)
      return;

    int from = std::max(1, err.line - opt.contextLines);
    int to = std::min(n, err.line + opt.contextLines);

    std::cerr << "\n"
              << GRAY << "code:" << RESET << "\n";

    for (int ln = from; ln <= to; ++ln)
    {
      const bool isMain = (ln == err.line);
      const std::string &rawLine = lines[static_cast<std::size_t>(ln - 1)];
      const std::string expanded = expandTabs(rawLine, opt.tabWidth);

      if (!isMain)
      {
        std::cerr << GRAY << "  " << ln << " | " << expanded << RESET << "\n";
        continue;
      }

      std::string caret = makeCaretLine(err.column, opt.tabWidth, rawLine);

      std::cerr << GRAY << "  " << ln << " | " << RESET;
      // print line + caret truncated if needed
      printTruncatedLine(expanded, caret, opt.maxLineWidth);
    }
  }
} // namespace vix::cli::errors
