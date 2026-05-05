/**
 *
 *  @file ReplayPrinter.hpp
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
#ifndef VIX_CLI_COMMANDS_REPLAY_PRINTER_HPP
#define VIX_CLI_COMMANDS_REPLAY_PRINTER_HPP

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include <vix/cli/commands/replay/ReplayRecord.hpp>
#include <vix/cli/commands/replay/ReplayStore.hpp>

namespace vix::commands::replay
{

  /**
   * @brief Print a compact replay header.
   *
   * @param out Output stream.
   * @param record Replay record.
   */
  void print_replay_header(std::ostream &out, const ReplayRecord &record);

  /**
   * @brief Print replay metadata as a compact key-value block.
   *
   * @param out Output stream.
   * @param record Replay record.
   */
  void print_replay_summary(std::ostream &out, const ReplayRecord &record);

  /**
   * @brief Print captured log file paths.
   *
   * @param out Output stream.
   * @param record Replay record.
   */
  void print_replay_logs(std::ostream &out, const ReplayRecord &record);

  /**
   * @brief Print the command that can be used to replay this record.
   *
   * @param out Output stream.
   * @param record Replay record.
   */
  void print_replay_command_hint(std::ostream &out, const ReplayRecord &record);

  /**
   * @brief Print a full replay record view.
   *
   * This is used by commands such as:
   * vix replay show <id>
   *
   * @param out Output stream.
   * @param record Replay record.
   */
  void print_replay_record(std::ostream &out, const ReplayRecord &record);

  /**
   * @brief Print one replay list entry.
   *
   * @param out Output stream.
   * @param entry Replay list entry.
   */
  void print_replay_list_entry(std::ostream &out, const ReplayListEntry &entry);

  /**
   * @brief Print a list of replay runs.
   *
   * @param out Output stream.
   * @param entries Replay list entries.
   */
  void print_replay_list(std::ostream &out, const std::vector<ReplayListEntry> &entries);

  /**
   * @brief Print a success message after saving a replay record.
   *
   * @param out Output stream.
   * @param record Replay record.
   */
  void print_replay_saved(std::ostream &out, const ReplayRecord &record);

  /**
   * @brief Print a replay error message.
   *
   * @param out Output stream.
   * @param message Error message.
   */
  void print_replay_error(std::ostream &out, const std::string &message);

  /**
   * @brief Print a replay warning message.
   *
   * @param out Output stream.
   * @param message Warning message.
   */
  void print_replay_warning(std::ostream &out, const std::string &message);

  /**
   * @brief Print a short excerpt from a log text.
   *
   * @param out Output stream.
   * @param title Section title.
   * @param text Log text.
   * @param maxLines Maximum number of lines to print.
   */
  void print_replay_log_excerpt(
      std::ostream &out,
      const std::string &title,
      const std::string &text,
      std::size_t maxLines);

} // namespace vix::commands::replay

#endif // VIX_CLI_COMMANDS_REPLAY_PRINTER_HPP
