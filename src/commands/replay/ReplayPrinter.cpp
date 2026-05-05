/**
 *
 *  @file ReplayPrinter.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/replay/ReplayPrinter.hpp>
#include <vix/cli/commands/replay/ReplayClock.hpp>
#include <vix/cli/Style.hpp>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

using namespace vix::cli::style;

namespace vix::commands::replay
{

  namespace
  {

    /**
     * @brief Return a color for a replay status.
     *
     * @param status Replay status.
     * @return ANSI color sequence.
     */
    const char *status_color(ReplayStatus status)
    {
      switch (status)
      {
      case ReplayStatus::Success:
        return GREEN;
      case ReplayStatus::Failed:
      case ReplayStatus::Crashed:
      case ReplayStatus::TimedOut:
        return RED;
      case ReplayStatus::Interrupted:
        return YELLOW;
      case ReplayStatus::Unknown:
      default:
        return GRAY;
      }
    }

    /**
     * @brief Return a symbol for a replay status.
     *
     * @param status Replay status.
     * @return Status symbol.
     */
    const char *status_symbol(ReplayStatus status)
    {
      switch (status)
      {
      case ReplayStatus::Success:
        return "✔";
      case ReplayStatus::Failed:
      case ReplayStatus::Crashed:
      case ReplayStatus::TimedOut:
        return "✖";
      case ReplayStatus::Interrupted:
        return "!";
      case ReplayStatus::Unknown:
      default:
        return "•";
      }
    }

    /**
     * @brief Print a section label.
     *
     * @param out Output stream.
     * @param label Section label.
     */
    void section(std::ostream &out, const std::string &label)
    {
      out << "\n"
          << PAD << GRAY << label << RESET << "\n";
    }

    /**
     * @brief Print a faint separator.
     *
     * @param out Output stream.
     */
    void sep(std::ostream &out)
    {
      out << PAD << GRAY << "─────────────────────────────────────" << RESET << "\n";
    }

    /**
     * @brief Print a key-value row.
     *
     * @param out Output stream.
     * @param key Key label.
     * @param value Value label.
     * @param highlight Whether the value should be highlighted.
     */
    void kv(std::ostream &out, const std::string &key, const std::string &value, bool highlight = false)
    {
      constexpr int pad = 12;

      std::string k = key;
      if (static_cast<int>(k.size()) < pad)
        k.append(static_cast<std::size_t>(pad - static_cast<int>(k.size())), ' ');

      out << PAD << GRAY << k << RESET;

      if (highlight)
        out << CYAN << BOLD << value << RESET << "\n";
      else
        out << value << "\n";
    }

    /**
     * @brief Return the first non-empty string.
     *
     * @param first First value.
     * @param second Fallback value.
     * @return first if non-empty, otherwise second.
     */
    std::string non_empty_or(const std::string &first, const std::string &second)
    {
      return first.empty() ? second : first;
    }

    /**
     * @brief Split a text into lines.
     *
     * @param text Input text.
     * @return Lines.
     */
    std::vector<std::string> split_lines(const std::string &text)
    {
      std::vector<std::string> lines;
      std::istringstream iss(text);
      std::string line;

      while (std::getline(iss, line))
        lines.push_back(line);

      if (lines.empty() && !text.empty())
        lines.push_back(text);

      return lines;
    }

  } // namespace

  void print_replay_header(std::ostream &out, const ReplayRecord &record)
  {
    const char *color = status_color(record.status);

    out << "\n"
        << PAD << color << BOLD << status_symbol(record.status) << RESET
        << "  " << BOLD << CYAN << record.id << RESET
        << "  " << GRAY << to_string(record.status) << RESET
        << "\n";
  }

  void print_replay_summary(std::ostream &out, const ReplayRecord &record)
  {
    section(out, "summary");

    kv(out, "mode", to_string(record.mode), true);
    kv(out, "target", to_string(record.target_kind), true);

    if (!record.command.empty())
      kv(out, "command", record.command, true);

    if (!record.cwd.empty())
      kv(out, "cwd", record.cwd.string());

    if (!record.target_path.empty())
      kv(out, "path", record.target_path.string());

    if (!record.timing.started_at.empty())
      kv(out, "started", record.timing.started_at);

    if (record.timing.duration_ms > 0)
      kv(out, "duration", format_replay_duration(record.timing.duration_ms));

    if (record.process.exit_code != 0 || record.status != ReplayStatus::Success)
      kv(out, "exit", std::to_string(record.process.exit_code), record.process.exit_code != 0);

    if (record.error_kind != ReplayErrorKind::None)
      kv(out, "error", to_string(record.error_kind), true);

    if (!record.error_message.empty())
      kv(out, "message", record.error_message);
  }

  void print_replay_logs(std::ostream &out, const ReplayRecord &record)
  {
    section(out, "logs");

    if (!record.logs.stdout_log.empty())
      kv(out, "stdout", record.logs.stdout_log.string());

    if (!record.logs.stderr_log.empty())
      kv(out, "stderr", record.logs.stderr_log.string());

    if (!record.logs.combined_log.empty())
      kv(out, "combined", record.logs.combined_log.string());
  }

  void print_replay_command_hint(std::ostream &out, const ReplayRecord &record)
  {
    if (record.id.empty())
      return;

    section(out, "replay");
    out << PAD << CYAN << BOLD << "vix replay " << record.id << RESET << "\n";
    out << PAD << GRAY << "vix replay last" << RESET << "\n";
  }

  void print_replay_record(std::ostream &out, const ReplayRecord &record)
  {
    print_replay_header(out, record);
    sep(out);
    print_replay_summary(out, record);
    print_replay_logs(out, record);

    if (!record.hint.empty())
    {
      section(out, "hint");
      out << PAD << GRAY << record.hint << RESET << "\n";
    }

    print_replay_command_hint(out, record);
    out << "\n";
  }

  void print_replay_list_entry(std::ostream &out, const ReplayListEntry &entry)
  {
    const char *color = status_color(entry.status);

    out << PAD
        << color << status_symbol(entry.status) << RESET
        << "  "
        << CYAN << BOLD << entry.id << RESET
        << "  "
        << GRAY << to_string(entry.status) << RESET;

    if (!entry.command.empty())
      out << "  " << entry.command;

    if (entry.duration_ms > 0)
      out << "  " << GRAY << format_replay_duration(entry.duration_ms) << RESET;

    out << "\n";
  }

  void print_replay_list(std::ostream &out, const std::vector<ReplayListEntry> &entries)
  {
    if (entries.empty())
    {
      out << PAD << GRAY << "No replay runs found." << RESET << "\n";
      return;
    }

    out << "\n";
    out << PAD << BOLD << CYAN << "Replay runs" << RESET << "\n";
    sep(out);

    for (const auto &entry : entries)
      print_replay_list_entry(out, entry);

    out << "\n";
  }

  void print_replay_saved(std::ostream &out, const ReplayRecord &record)
  {
    out << PAD << GREEN << "✔" << RESET
        << " replay saved"
        << "  " << GRAY << record.id << RESET
        << "\n";
  }

  void print_replay_error(std::ostream &out, const std::string &message)
  {
    out << PAD << RED << "✖" << RESET << " " << message << "\n";
  }

  void print_replay_warning(std::ostream &out, const std::string &message)
  {
    out << PAD << YELLOW << "!" << RESET << " " << message << "\n";
  }

  void print_replay_log_excerpt(
      std::ostream &out,
      const std::string &title,
      const std::string &text,
      std::size_t maxLines)
  {
    const std::vector<std::string> lines = split_lines(text);

    if (lines.empty())
      return;

    section(out, title);

    const std::size_t count = std::min(maxLines, lines.size());

    for (std::size_t i = 0; i < count; ++i)
      out << PAD << GRAY << "│ " << RESET << lines[i] << "\n";

    if (lines.size() > count)
    {
      const std::size_t hidden = lines.size() - count;
      out << PAD << GRAY << "│ ... " << hidden << " more line(s)" << RESET << "\n";
    }
  }

} // namespace vix::commands::replay
