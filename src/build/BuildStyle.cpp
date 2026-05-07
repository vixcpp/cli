/**
 *
 *  @file BuildStyle.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Reusable build output style
 *
 */

#include <vix/cli/build/BuildStyle.hpp>

#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>
#include <system_error>

#include <vix/cli/Style.hpp>

namespace vix::cli::build
{
  namespace style = vix::cli::style;

  namespace
  {
    static std::string trim_copy(const std::string &value)
    {
      std::size_t begin = 0;

      while (begin < value.size() &&
             std::isspace(static_cast<unsigned char>(value[begin])))
      {
        ++begin;
      }

      std::size_t end = value.size();

      while (end > begin &&
             std::isspace(static_cast<unsigned char>(value[end - 1])))
      {
        --end;
      }

      return value.substr(begin, end - begin);
    }

    static std::string colorize(
        const char *color,
        const std::string &value)
    {
      return std::string(color) + value + style::RESET;
    }

    static bool is_absolute_or_relative_path_like(const std::string &value)
    {
      if (value.empty())
        return false;

      if (value.find('/') != std::string::npos)
        return true;

#ifdef _WIN32
      if (value.find('\\') != std::string::npos)
        return true;

      if (value.size() > 2 &&
          std::isalpha(static_cast<unsigned char>(value[0])) &&
          value[1] == ':')
      {
        return true;
      }
#endif

      return value.find('.') != std::string::npos;
    }

    static std::string make_pointer_line(std::size_t column)
    {
      if (column == 0)
        return "";

      std::string out;
      out.append(column > 1 ? column - 1 : 0, ' ');
      out.push_back('^');
      return out;
    }

    static std::string label_color(const std::string &label)
    {
      if (label == "error:")
        return style::RED;

      if (label == "hint:")
        return style::YELLOW;

      if (label == "location:")
        return style::CYAN;

      if (label == "code:")
        return style::CYAN;

      return style::GRAY;
    }

    static void print_label(
        std::ostream &out,
        const std::string &label)
    {
      out << "  "
          << colorize(label_color(label).c_str(), label)
          << "\n";
    }

    static void print_optional_line(
        std::ostream &out,
        const std::string &label,
        const std::string &value)
    {
      if (value.empty())
        return;

      print_label(out, label);
      out << "    " << value << "\n\n";
    }
  } // namespace

  bool BuildLocation::valid() const
  {
    return !file.empty();
  }

  bool BuildCodeFrame::valid() const
  {
    return location.valid() && !lines.empty();
  }

  bool BuildDiagnostic::has_location() const
  {
    return location.valid();
  }

  bool BuildDiagnostic::has_code_frame() const
  {
    return codeFrame.valid();
  }

  bool BuildProgress::valid() const
  {
    return !action.empty() ||
           !target.empty() ||
           total > 0;
  }

  std::string build_message_prefix(BuildMessageKind kind)
  {
    switch (kind)
    {
    case BuildMessageKind::Info:
      return colorize(style::CYAN, "•");
    case BuildMessageKind::Step:
      return colorize(style::CYAN, "›");
    case BuildMessageKind::Success:
      return colorize(style::GREEN, "✔");
    case BuildMessageKind::Warning:
      return colorize(style::YELLOW, "!");
    case BuildMessageKind::Error:
      return colorize(style::RED, "✖");
    default:
      return "•";
    }
  }

  void print_build_message(
      std::ostream &out,
      BuildMessageKind kind,
      const std::string &message)
  {
    out << "  "
        << build_message_prefix(kind)
        << " "
        << message
        << "\n";
  }

  void print_build_info(
      std::ostream &out,
      const std::string &message)
  {
    print_build_message(out, BuildMessageKind::Info, message);
  }

  void print_build_step(
      std::ostream &out,
      const std::string &message)
  {
    print_build_message(out, BuildMessageKind::Step, message);
  }

  void print_build_success(
      std::ostream &out,
      const std::string &message)
  {
    print_build_message(out, BuildMessageKind::Success, message);
  }

  void print_build_warning(
      std::ostream &out,
      const std::string &message)
  {
    print_build_message(out, BuildMessageKind::Warning, message);
  }

  void print_build_error(
      std::ostream &out,
      const std::string &message)
  {
    print_build_message(out, BuildMessageKind::Error, message);
  }

  void print_build_header(
      std::ostream &out,
      const std::string &target,
      const std::string &preset)
  {
    out << "\n";
    out << colorize(style::BOLD, "Building");

    if (!target.empty())
      out << " " << colorize(style::CYAN, target);

    if (!preset.empty())
      out << " " << colorize(style::GRAY, "[" + preset + "]");

    out << "\n";
  }

  void print_build_progress(
      std::ostream &out,
      const BuildProgress &progress)
  {
    if (!progress.valid())
      return;

    out << "  " << colorize(style::CYAN, "›") << " ";

    if (progress.total > 0)
    {
      out << "["
          << progress.current
          << "/"
          << progress.total
          << "] ";
    }

    if (!progress.action.empty())
      out << progress.action;

    if (!progress.target.empty())
      out << " " << colorize(style::GRAY, progress.target);

    out << "\n";
  }

  void print_build_done(
      std::ostream &out,
      const std::string &profile,
      const std::string &duration)
  {
    out << "  "
        << colorize(style::GREEN, "✔")
        << " Finished";

    if (!profile.empty())
      out << " " << profile;

    if (!duration.empty())
      out << " in " << duration;

    out << "\n";
  }

  void print_build_diagnostic(
      std::ostream &out,
      const BuildDiagnostic &diagnostic)
  {
    out << "\n";
    out << "  "
        << colorize(style::RED, "✖")
        << " "
        << colorize(style::BOLD, diagnostic.title.empty()
                                     ? std::string("Build failed")
                                     : diagnostic.title)
        << "\n\n";

    if (!diagnostic.message.empty())
      out << "  " << diagnostic.message << "\n\n";

    if (diagnostic.has_location())
    {
      print_label(out, "location:");
      out << "    "
          << format_build_location(diagnostic.location)
          << "\n\n";
    }

    print_optional_line(out, "error:", diagnostic.error);

    if (diagnostic.has_code_frame())
    {
      print_label(out, "code:");

      const std::size_t firstLine =
          diagnostic.codeFrame.location.line > diagnostic.codeFrame.lines.size()
              ? diagnostic.codeFrame.location.line - diagnostic.codeFrame.lines.size() + 1
              : 1;

      for (std::size_t i = 0; i < diagnostic.codeFrame.lines.size(); ++i)
      {
        const std::size_t lineNumber = firstLine + i;

        out << "    "
            << lineNumber
            << " | "
            << diagnostic.codeFrame.lines[i]
            << "\n";

        if (lineNumber == diagnostic.codeFrame.location.line)
        {
          const std::string pointer =
              make_pointer_line(diagnostic.codeFrame.location.column);

          if (!pointer.empty())
          {
            out << "      | "
                << colorize(style::RED, pointer)
                << "\n";
          }
        }
      }

      out << "\n";
    }

    print_optional_line(out, "hint:", diagnostic.hint);
  }

  std::string format_build_location(
      const BuildLocation &location)
  {
    if (!location.valid())
      return "";

    std::ostringstream out;
    out << location.file.string();

    if (location.line > 0)
    {
      out << ":" << location.line;

      if (location.column > 0)
        out << ":" << location.column;
    }

    return out.str();
  }

  std::string relative_build_path(
      const fs::path &path,
      const fs::path &base)
  {
    if (path.empty())
      return "";

    if (base.empty())
      return path.lexically_normal().string();

    std::error_code ec;
    const fs::path relative = fs::relative(path, base, ec);

    if (!ec && !relative.empty())
      return relative.lexically_normal().string();

    return path.lexically_normal().string();
  }

  std::optional<BuildLocation> parse_build_location(
      const std::string &text)
  {
    static const std::regex pattern(
        R"(([^:\s][^:\n\r]*):([0-9]+):([0-9]+))");

    std::smatch match;

    if (!std::regex_search(text, match, pattern))
      return std::nullopt;

    const std::string path = match[1].str();

    if (!is_absolute_or_relative_path_like(path))
      return std::nullopt;

    BuildLocation location;
    location.file = fs::path(path);

    try
    {
      location.line = static_cast<std::size_t>(
          std::stoull(match[2].str()));

      location.column = static_cast<std::size_t>(
          std::stoull(match[3].str()));
    }
    catch (...)
    {
      return std::nullopt;
    }

    return location;
  }

} // namespace vix::cli::build
