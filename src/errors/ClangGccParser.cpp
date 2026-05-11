/**
 *
 *  @file ClangGccParser.cpp
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
#include <vix/cli/errors/ClangGccParser.hpp>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <regex>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace vix::cli::errors
{
  namespace
  {
    std::string trim_copy(std::string text)
    {
      while (!text.empty() &&
             (text.back() == '\n' ||
              text.back() == '\r' ||
              text.back() == ' ' ||
              text.back() == '\t'))
      {
        text.pop_back();
      }

      std::size_t start = 0;

      while (start < text.size() &&
             (text[start] == '\n' ||
              text[start] == '\r' ||
              text[start] == ' ' ||
              text[start] == '\t'))
      {
        ++start;
      }

      text.erase(0, start);
      return text;
    }

    std::string strip_ansi(std::string text)
    {
      static const std::regex ansiRe(
          R"(\x1B\[[0-9;?]*[ -/]*[@-~])");

      return std::regex_replace(text, ansiRe, "");
    }

    bool is_build_noise_line(const std::string &line)
    {
      return line.rfind("gmake", 0) == 0 ||
             line.rfind("make", 0) == 0 ||
             line.rfind("ninja", 0) == 0 ||
             line.rfind("[", 0) == 0;
    }

    bool is_error_severity(const std::string &severity)
    {
      return severity == "error" ||
             severity == "fatal error";
    }

    bool is_note_severity(const std::string &severity)
    {
      return severity == "note";
    }

    bool is_virtual_file(const std::string &file)
    {
      if (file.empty())
        return true;

      return file.front() == '<' && file.back() == '>';
    }

    bool is_real_file_location(const std::string &file)
    {
      return !file.empty() && !is_virtual_file(file);
    }

    bool is_caret_or_snippet_line(const std::string &line)
    {
      if (line.empty())
        return false;

      const auto first = line.find_first_not_of(' ');

      if (first == std::string::npos)
        return false;

      if (line[first] == '^' || line[first] == '~')
        return true;

      static const std::regex gccSnippetRe(
          R"(^[0-9]+\s*\|\s*)");

      return std::regex_search(line, gccSnippetRe);
    }

    bool contains_text(
        const std::string &value,
        const std::string &needle)
    {
      return value.find(needle) != std::string::npos;
    }

    bool looks_like_template_or_instantiation_note(
        const std::string &message)
    {
      return contains_text(message, "required from") ||
             contains_text(message, "in instantiation of") ||
             contains_text(message, "candidate") ||
             contains_text(message, "template argument deduction") ||
             contains_text(message, "substitution failed") ||
             contains_text(message, "constraints not satisfied");
    }

    bool looks_like_macro_note(
        const std::string &message)
    {
      return contains_text(message, "in expansion of macro") ||
             contains_text(message, "expanded from macro") ||
             contains_text(message, "in definition of macro");
    }

    std::string strip_macro_quotes(std::string value)
    {
      value = trim_copy(value);

      while (!value.empty())
      {
        const unsigned char c =
            static_cast<unsigned char>(value.front());

        if (value.front() == '\'' ||
            value.front() == '"' ||
            value.front() == '`')
        {
          value.erase(value.begin());
          continue;
        }

        /*
         * UTF-8 left single quote:  E2 80 98
         * UTF-8 right single quote: E2 80 99
         *
         * Remove the first UTF-8 quote if present. This keeps GCC output
         * like ‘MACRO_NAME’ usable without depending on a Unicode regex.
         */
        if (c == 0xE2 && value.size() >= 3)
        {
          const unsigned char c1 =
              static_cast<unsigned char>(value[1]);
          const unsigned char c2 =
              static_cast<unsigned char>(value[2]);

          if (c1 == 0x80 && (c2 == 0x98 || c2 == 0x99))
          {
            value.erase(0, 3);
            continue;
          }
        }

        break;
      }

      while (!value.empty())
      {
        const unsigned char c =
            static_cast<unsigned char>(value.back());

        if (value.back() == '\'' ||
            value.back() == '"' ||
            value.back() == '`' ||
            value.back() == ',' ||
            value.back() == ':' ||
            value.back() == ';')
        {
          value.pop_back();
          continue;
        }

        if (c == 0x99 && value.size() >= 3)
        {
          const std::size_t n = value.size();

          const unsigned char c0 =
              static_cast<unsigned char>(value[n - 3]);
          const unsigned char c1 =
              static_cast<unsigned char>(value[n - 2]);
          const unsigned char c2 =
              static_cast<unsigned char>(value[n - 1]);

          if (c0 == 0xE2 && c1 == 0x80 && c2 == 0x99)
          {
            value.erase(n - 3);
            continue;
          }
        }

        break;
      }

      return trim_copy(value);
    }

    std::string extract_macro_name_from_note(
        const std::string &message)
    {
      static const std::vector<std::string> markers = {
          "in expansion of macro",
          "expanded from macro",
          "in definition of macro"};

      for (const std::string &marker : markers)
      {
        const std::size_t pos = message.find(marker);

        if (pos == std::string::npos)
          continue;

        std::string rest = message.substr(pos + marker.size());
        rest = trim_copy(rest);

        if (rest.empty())
          return {};

        const std::size_t space = rest.find_first_of(" \t\r\n");

        if (space != std::string::npos)
          rest = rest.substr(0, space);

        return strip_macro_quotes(rest);
      }

      return {};
    }

    const std::regex &re_diag_with_col()
    {
      static const std::regex r(
          R"(^(.+?):([0-9]+):([0-9]+):\s*(fatal error|error|warning|note):\s*(.+)$)");
      return r;
    }

    const std::regex &re_diag_no_col()
    {
      static const std::regex r(
          R"(^(.+?):([0-9]+):\s*(fatal error|error|warning|note):\s*(.+)$)");
      return r;
    }

    const std::regex &re_virtual_error()
    {
      static const std::regex r(
          R"(^(<[^>]+>):\s*(fatal error|error):\s*(.+)$)");
      return r;
    }

    const std::regex &re_include_from()
    {
      static const std::regex r(
          R"(^(?:In file included from|from)\s+(.+?):([0-9]+)(?::[0-9]+)?[,:]?\s*$)");
      return r;
    }

    struct PendingError
    {
      bool active{false};
      std::string virtualFile;
      std::string severity;
      std::string message;
      std::string raw;

      void reset()
      {
        active = false;
        virtualFile.clear();
        severity.clear();
        message.clear();
        raw.clear();
      }
    };

    struct IncludeFrame
    {
      std::string file;
      int line{0};
    };

    std::string format_include_context(
        const std::vector<IncludeFrame> &stack)
    {
      if (stack.empty())
        return {};

      std::string out;

      for (const IncludeFrame &frame : stack)
      {
        if (frame.file.empty() || frame.line <= 0)
          continue;

        out += "included from " +
               frame.file +
               ":" +
               std::to_string(frame.line) +
               "\n";
      }

      return out;
    }

    CompilerError make_error(
        const std::string &file,
        int line,
        int column,
        const std::string &severity,
        const std::string &message,
        const std::string &raw,
        const std::vector<IncludeFrame> &includeStack)
    {
      CompilerError err;
      err.file = trim_copy(file);
      err.line = line;
      err.column = column > 0 ? column : 1;
      err.message = severity + ": " + trim_copy(message);
      err.raw = raw;

      const std::string includeContext =
          format_include_context(includeStack);

      if (!includeContext.empty())
        err.raw = includeContext + err.raw;

      return err;
    }
  } // namespace

  std::vector<CompilerError> ClangGccParser::parse(
      const std::string &buildLog)
  {
    std::vector<CompilerError> out;

    PendingError pending;
    std::vector<IncludeFrame> includeStack;

    auto discard_pending =
        [&]()
    {
      /*
       * Do not emit diagnostics with <command-line>, <built-in>,
       * or similar pseudo files. They cannot produce a useful codeframe.
       * The full raw build output remains available through the fallback.
       */
      pending.reset();
    };

    auto resolve_pending_with_note =
        [&](const std::string &file,
            int lineNumber,
            int columnNumber,
            const std::string &noteMessage,
            const std::string &rawLine) -> bool
    {
      if (!pending.active)
        return false;

      if (!is_real_file_location(file))
      {
        pending.raw += "\n" + rawLine;
        return true;
      }

      std::string message =
          pending.severity + ": " + pending.message;

      const std::string macroName =
          extract_macro_name_from_note(noteMessage);

      if (!macroName.empty())
      {
        message += " while expanding macro '" + macroName + "'";
      }
      else if (!noteMessage.empty())
      {
        message += " (" + noteMessage + ")";
      }

      CompilerError err;
      err.file = trim_copy(file);
      err.line = lineNumber;
      err.column = columnNumber > 0 ? columnNumber : 1;
      err.message = message;
      err.raw = pending.raw + "\n" + rawLine;

      const std::string includeContext =
          format_include_context(includeStack);

      if (!includeContext.empty())
        err.raw = includeContext + err.raw;

      out.push_back(std::move(err));

      pending.reset();
      includeStack.clear();
      return true;
    };

    std::istringstream input(buildLog);
    std::string line;

    while (std::getline(input, line))
    {
      line = strip_ansi(trim_copy(line));

      if (line.empty())
        continue;

      if (is_build_noise_line(line))
        continue;

      if (is_caret_or_snippet_line(line))
        continue;

      std::smatch match;

      if (std::regex_search(line, match, re_include_from()))
      {
        IncludeFrame frame;
        frame.file = trim_copy(match[1].str());
        frame.line = std::stoi(match[2].str());

        if (!frame.file.empty() && frame.line > 0)
          includeStack.push_back(std::move(frame));

        continue;
      }

      if (std::regex_search(line, match, re_virtual_error()))
      {
        discard_pending();

        pending.active = true;
        pending.virtualFile = trim_copy(match[1].str());
        pending.severity = trim_copy(match[2].str());
        pending.message = trim_copy(match[3].str());
        pending.raw = line;

        continue;
      }

      if (std::regex_search(line, match, re_diag_with_col()))
      {
        const std::string file = trim_copy(match[1].str());
        const int lineNumber = std::stoi(match[2].str());
        const int columnNumber = std::stoi(match[3].str());
        const std::string severity = trim_copy(match[4].str());
        const std::string message = trim_copy(match[5].str());

        if (pending.active && is_note_severity(severity))
        {
          if (resolve_pending_with_note(
                  file,
                  lineNumber,
                  columnNumber,
                  message,
                  line))
          {
            continue;
          }
        }

        if (is_note_severity(severity))
        {
          if (!out.empty() &&
              (looks_like_template_or_instantiation_note(message) ||
               looks_like_macro_note(message)))
          {
            out.back().raw += "\n" + line;
          }

          continue;
        }

        if (!is_error_severity(severity))
        {
          includeStack.clear();
          continue;
        }

        discard_pending();

        if (!is_real_file_location(file))
          continue;

        out.push_back(
            make_error(
                file,
                lineNumber,
                columnNumber,
                severity,
                message,
                line,
                includeStack));

        includeStack.clear();
        continue;
      }

      if (std::regex_search(line, match, re_diag_no_col()))
      {
        const std::string file = trim_copy(match[1].str());
        const int lineNumber = std::stoi(match[2].str());
        const std::string severity = trim_copy(match[3].str());
        const std::string message = trim_copy(match[4].str());

        if (pending.active && is_note_severity(severity))
        {
          if (resolve_pending_with_note(
                  file,
                  lineNumber,
                  1,
                  message,
                  line))
          {
            continue;
          }
        }

        if (is_note_severity(severity))
        {
          if (!out.empty() &&
              (looks_like_template_or_instantiation_note(message) ||
               looks_like_macro_note(message)))
          {
            out.back().raw += "\n" + line;
          }

          continue;
        }

        if (!is_error_severity(severity))
        {
          includeStack.clear();
          continue;
        }

        discard_pending();

        if (!is_real_file_location(file))
          continue;

        out.push_back(
            make_error(
                file,
                lineNumber,
                1,
                severity,
                message,
                line,
                includeStack));

        includeStack.clear();
        continue;
      }

      if (pending.active)
      {
        /*
         * The current line did not resolve the pseudo-location error.
         * Drop it instead of emitting an invalid location.
         */
        discard_pending();
      }
    }

    discard_pending();

    return out;
  }
} // namespace vix::cli::errors
