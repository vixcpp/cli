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
#include <optional>
#include <regex>
#include <sstream>
#include <system_error>
#include <vector>
#include <iomanip>

#include <vix/cli/Style.hpp>

namespace vix::cli::build
{
  namespace style = vix::cli::style;

  static std::string colorize(
      const char *color,
      const std::string &value)
  {
    return std::string(color) + value + style::RESET;
  }

  namespace
  {
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

      if (label == "message:")
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

    static void print_compact_optional_line(
        std::ostream &out,
        const std::string &label,
        const std::string &value)
    {
      if (value.empty())
        return;

      out << "  "
          << colorize(label_color(label).c_str(), label)
          << " "
          << value
          << "\n";
    }

    static std::string trim_copy(std::string value)
    {
      while (!value.empty() &&
             std::isspace(static_cast<unsigned char>(value.front())) != 0)
      {
        value.erase(value.begin());
      }

      while (!value.empty() &&
             std::isspace(static_cast<unsigned char>(value.back())) != 0)
      {
        value.pop_back();
      }

      return value;
    }

    static std::string extract_warning_flag(const std::string &message)
    {
      static const std::regex flagRe(R"(\[(-W[^\]]+)\])");

      std::smatch match;

      if (!std::regex_search(message, match, flagRe))
        return {};

      return match[1].str();
    }

    static std::string strip_warning_flag(std::string message)
    {
      static const std::regex flagRe(R"(\s*\[-W[^\]]+\]\s*$)");
      return trim_copy(std::regex_replace(message, flagRe, ""));
    }

    static std::string shorten_cpp_signature(std::string value)
    {
      const std::size_t paren = value.find('(');

      if (paren != std::string::npos)
        value = value.substr(0, paren);

      const std::string anonymous = "{anonymous}::";
      const std::size_t anonymousPos = value.find(anonymous);

      if (anonymousPos != std::string::npos)
        value = value.substr(anonymousPos + anonymous.size());

      const std::size_t ns = value.rfind("::");

      if (ns != std::string::npos)
        value = value.substr(ns + 2);

      if (!value.empty() && value.front() == '\'' && value.back() == '\'')
        value = value.substr(1, value.size() - 2);

      if (!value.empty() && value.front() == '‘' && value.back() == '’')
        value = value.substr(3, value.size() > 6 ? value.size() - 6 : value.size());

      return trim_copy(value);
    }

    static std::string extract_quoted_symbol(const std::string &message)
    {
      {
        const std::size_t start = message.find("‘");
        const std::size_t end = message.find("’", start + 1);

        if (start != std::string::npos &&
            end != std::string::npos &&
            end > start)
        {
          return message.substr(start + std::string("‘").size(),
                                end - (start + std::string("‘").size()));
        }
      }

      {
        const std::size_t start = message.find('\'');
        const std::size_t end = message.find('\'', start + 1);

        if (start != std::string::npos &&
            end != std::string::npos &&
            end > start)
        {
          return message.substr(start + 1, end - start - 1);
        }
      }

      return {};
    }
  } // namespace

  void print_build_header_full(
      std::ostream &out,
      const std::string &target,
      const std::string &preset,
      const std::optional<std::string> &launcher,
      const std::optional<std::string> &fastLinkerFlag,
      int jobs)
  {
    out << style::CYAN
        << style::BOLD
        << "Compiling"
        << style::RESET;

    if (!target.empty())
    {
      out << " "
          << style::CYAN
          << style::BOLD
          << target
          << style::RESET;
    }

    if (!preset.empty())
      out << " " << colorize(style::GRAY, "(" + preset + ")");

    out << "\n";

    std::vector<std::string> meta;

    if (launcher && !launcher->empty())
    {
      meta.push_back(
          "launcher: " + colorize(style::MAGENTA, *launcher));
    }

    if (fastLinkerFlag && !fastLinkerFlag->empty())
    {
      const std::string name =
          fastLinkerFlag->find("mold") != std::string::npos
              ? "mold"
              : "lld";

      meta.push_back(
          "linker: " + colorize(style::MAGENTA, name));
    }

    if (jobs > 0)
    {
      meta.push_back(
          "jobs: " + colorize(style::MAGENTA, std::to_string(jobs)));
    }

    if (!meta.empty())
    {
      out << style::GRAY << "  * " << style::RESET;

      for (std::size_t i = 0; i < meta.size(); ++i)
      {
        if (i > 0)
          out << style::GRAY << " | " << style::RESET;

        out << meta[i];
      }

      out << "\n";
    }
  }

  void print_build_success_timed(
      std::ostream &out,
      const std::string &message,
      long long milliseconds)
  {
    out << "  "
        << colorize(style::GREEN, "ok")
        << " "
        << message;

    if (milliseconds > 0)
    {
      const double seconds = static_cast<double>(milliseconds) / 1000.0;

      std::ostringstream time;
      time.setf(std::ios::fixed);
      time.precision(seconds >= 10.0 ? 1 : 2);
      time << seconds << "s";

      out << " " << colorize(style::GRAY, "| " + time.str());
    }

    out << "\n";
  }

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
    const std::string title =
        diagnostic.title.empty()
            ? std::string("Build failed")
            : diagnostic.title;

    out << "  "
        << style::RED
        << style::BOLD
        << "✖ "
        << title
        << style::RESET
        << "\n";

    print_compact_optional_line(out, "message:", diagnostic.message);

    if (diagnostic.has_location())
    {
      out << "  "
          << colorize(label_color("location:").c_str(), "location:")
          << " "
          << format_build_location(diagnostic.location)
          << "\n";
    }

    print_compact_optional_line(out, "error:", diagnostic.error);

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

    print_compact_optional_line(out, "hint:", diagnostic.hint);
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

  void print_task_header_full(
      std::ostream &out,
      const std::string &action,
      const std::string &target,
      const std::string &preset,
      const std::vector<std::pair<std::string, std::string>> &meta)
  {
    out << style::CYAN
        << style::BOLD
        << action
        << style::RESET;

    if (!target.empty())
    {
      out << " "
          << style::CYAN
          << style::BOLD
          << target
          << style::RESET;
    }

    if (!preset.empty())
      out << " " << colorize(style::GRAY, "(" + preset + ")");

    out << "\n";

    if (meta.empty())
      return;

    out << style::GRAY << "  * " << style::RESET;

    for (std::size_t i = 0; i < meta.size(); ++i)
    {
      if (i > 0)
        out << style::GRAY << " | " << style::RESET;

      out << meta[i].first << ": "
          << colorize(style::MAGENTA, meta[i].second);
    }

    out << "\n";
  }

  void print_task_success_timed(
      std::ostream &out,
      const std::string &message,
      long long milliseconds)
  {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << (static_cast<double>(milliseconds) / 1000.0);

    out << "  "
        << colorize(style::GREEN, "✔")
        << " "
        << message
        << " in "
        << oss.str()
        << "s\n";
  }

  void print_task_failure_timed(
      std::ostream &out,
      const std::string &message,
      long long milliseconds)
  {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << (static_cast<double>(milliseconds) / 1000.0);

    out << "  "
        << colorize(style::RED, "✖")
        << " "
        << message
        << " after "
        << oss.str()
        << "s\n";
  }

  bool BuildWarning::has_location() const
  {
    return !file.empty();
  }

  std::optional<BuildWarning> parse_build_warning(
      const std::string &text)
  {
    static const std::regex pattern(
        R"(([^:\n\r]+):([0-9]+):([0-9]+):\s+warning:\s+(.+))");

    std::smatch match;

    if (!std::regex_search(text, match, pattern))
      return std::nullopt;

    BuildWarning warning;
    warning.file = fs::path(match[1].str());

    try
    {
      warning.line = static_cast<std::size_t>(
          std::stoull(match[2].str()));

      warning.column = static_cast<std::size_t>(
          std::stoull(match[3].str()));
    }
    catch (...)
    {
      return std::nullopt;
    }

    warning.message = match[4].str();
    warning.raw = text;

    return humanize_build_warning(std::move(warning));
  }

  void print_build_warnings_summary(
      std::ostream &out,
      const std::vector<BuildWarning> &warnings,
      std::size_t total)
  {
    if (total == 0)
      return;

    out << "  "
        << colorize(style::YELLOW, "warning")
        << " "
        << colorize(style::BOLD, std::to_string(total))
        << " compiler warning"
        << (total > 1 ? "s" : "")
        << "\n";

    const std::size_t shown = warnings.size();

    for (const BuildWarning &warning : warnings)
    {
      out << "    "
          << colorize(style::YELLOW, "•")
          << " ";

      if (warning.has_location())
      {
        out << colorize(style::CYAN, warning.file.filename().string());

        if (warning.line > 0)
        {
          out << style::GRAY
              << ":"
              << warning.line;

          if (warning.column > 0)
            out << ":" << warning.column;

          out << style::RESET;
        }

        out << "\n";
      }

      out << "      "
          << (warning.message.empty() ? warning.raw : warning.message)
          << "\n";

      if (!warning.hint.empty())
      {
        out << "      "
            << colorize(style::GRAY, "hint:")
            << " "
            << warning.hint
            << "\n";
      }
    }

    if (total > shown)
    {
      out << "    "
          << colorize(style::GRAY, "• ")
          << colorize(
                 style::GRAY,
                 std::to_string(total - shown) +
                     " more warning" +
                     ((total - shown) > 1 ? "s" : "") +
                     " hidden")
          << "\n";
    }

    out << "    "
        << colorize(style::GRAY, "hint:")
        << " run with "
        << colorize(style::CYAN, "--verbose")
        << " for full compiler output"
        << "\n\n";
  }

  BuildWarning humanize_build_warning(BuildWarning warning)
  {
    const std::string message = warning.message;
    warning.flag = extract_warning_flag(message);

    const std::string cleanMessage = strip_warning_flag(message);
    const std::string quoted = extract_quoted_symbol(cleanMessage);

    if (cleanMessage.find("defined but not used") != std::string::npos)
    {
      warning.kind = BuildWarningKind::UnusedFunction;
      warning.symbol = shorten_cpp_signature(quoted);
      warning.message =
          warning.symbol.empty()
              ? "unused function"
              : "unused function: " + warning.symbol;
      warning.hint = "remove it, use it, or mark it [[maybe_unused]]";
      return warning;
    }

    if (cleanMessage.find("unused variable") != std::string::npos)
    {
      warning.kind = BuildWarningKind::UnusedVariable;
      warning.symbol = shorten_cpp_signature(quoted);
      warning.message =
          warning.symbol.empty()
              ? "unused variable"
              : "unused variable: " + warning.symbol;
      warning.hint = "remove it or mark it [[maybe_unused]]";
      return warning;
    }

    if (cleanMessage.find("unused parameter") != std::string::npos)
    {
      warning.kind = BuildWarningKind::UnusedParameter;
      warning.symbol = shorten_cpp_signature(quoted);
      warning.message =
          warning.symbol.empty()
              ? "unused parameter"
              : "unused parameter: " + warning.symbol;
      warning.hint = "remove the parameter name or mark it [[maybe_unused]]";
      return warning;
    }

    if (cleanMessage.find("shadows a previous local") != std::string::npos ||
        cleanMessage.find("declaration of") != std::string::npos &&
            cleanMessage.find("shadows") != std::string::npos)
    {
      warning.kind = BuildWarningKind::ShadowedVariable;
      warning.symbol = shorten_cpp_signature(quoted);
      warning.message =
          warning.symbol.empty()
              ? "variable shadows another variable"
              : "variable shadows another variable: " + warning.symbol;
      warning.hint = "rename the inner variable to avoid confusion";
      return warning;
    }

    if (cleanMessage.find("deprecated") != std::string::npos)
    {
      warning.kind = BuildWarningKind::DeprecatedDeclaration;
      warning.symbol = shorten_cpp_signature(quoted);
      warning.message =
          warning.symbol.empty()
              ? "deprecated API used"
              : "deprecated API used: " + warning.symbol;
      warning.hint = "replace it with the recommended API";
      return warning;
    }

    if (warning.flag == "-Wsign-compare")
    {
      warning.kind = BuildWarningKind::SignCompare;
      warning.message = "comparison mixes signed and unsigned values";
      warning.hint = "use matching integer types before comparing";
      return warning;
    }

    if (warning.flag == "-Wconversion")
    {
      warning.kind = BuildWarningKind::Conversion;
      warning.message = "implicit conversion may change the value";
      warning.hint = "use an explicit cast or a safer target type";
      return warning;
    }

    if (warning.flag == "-Wreorder")
    {
      warning.kind = BuildWarningKind::Reorder;
      warning.message = "constructor initializes members in a different order";
      warning.hint = "match the initializer list order with the class field order";
      return warning;
    }

    if (warning.flag == "-Wmissing-field-initializers")
    {
      warning.kind = BuildWarningKind::MissingFieldInitializer;
      warning.message = "some fields are not initialized";
      warning.hint = "initialize all fields explicitly";
      return warning;
    }

    warning.kind = BuildWarningKind::Unknown;
    warning.message = cleanMessage;
    warning.hint = warning.flag.empty()
                       ? "review this compiler warning"
                       : "review this compiler warning: " + warning.flag;

    return warning;
  }

} // namespace vix::cli::build
