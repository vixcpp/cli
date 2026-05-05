#pragma once

/**
 * @file NewTui.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Interactive terminal UI primitives used by `vix new`.
 * Covers: raw mode, key reading, single-select and multi-select menus.
 */

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <vix/cli/commands/new/NewTypes.hpp>

#if defined(_WIN32)
#include <vix/cli/platform/windows.hpp>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace vix::commands::new_cmd::tui
{

  // ------------------------------------------------------------------
  // Cancellation flag (set by SIGINT handler)
  // ------------------------------------------------------------------

  extern std::atomic<bool> g_cancelled;

  // ------------------------------------------------------------------
  // Key enum
  // ------------------------------------------------------------------

  enum class Key
  {
    None,
    Up,
    Down,
    Enter,
    Escape,
    CtrlC,
    Space,
    ToggleAll,
    Quit
  };

  // ------------------------------------------------------------------
  // RAII guards
  // ------------------------------------------------------------------

  /// Installs a SIGINT handler that sets g_cancelled; restores on destruction.
  struct SignalGuard
  {
    using Handler = void (*)(int);
    Handler old{nullptr};

    SignalGuard();
    ~SignalGuard();
  };

#if !defined(_WIN32)
  /// Puts the terminal in raw/non-canonical mode; restores on destruction.
  struct RawMode
  {
    termios old{};
    bool active{false};

    RawMode();
    ~RawMode();
  };
#endif

  /// Hides the terminal cursor while alive; shows it on destruction.
  struct CursorGuard
  {
    bool active{false};

    explicit CursorGuard(bool enable);
    ~CursorGuard();
  };

  // ------------------------------------------------------------------
  // Environment helpers
  // ------------------------------------------------------------------

  bool is_tty_stdin();
  bool is_noninteractive_env();

  /// Returns true when interactive prompts can be shown.
  bool can_interact();

  // ------------------------------------------------------------------
  // Low-level terminal helpers
  // ------------------------------------------------------------------

  void clear_line();
  void move_cursor_up(int lines);
  void print_cancel_line();

  Key read_key();

  // ------------------------------------------------------------------
  // Generic single-select menu
  // ------------------------------------------------------------------

  template <typename T>
  struct SelectResult
  {
    bool cancelled{false};
    T value{};
  };

  /// Render a single-select list; redraws in-place after the first draw.
  void render_menu_single(
      const std::string &title,
      const std::vector<std::string> &items,
      int selected,
      bool firstDraw);

  /// Run a blocking single-select menu. Returns immediately (with default)
  /// when !can_interact().
  template <typename EnumT>
  SelectResult<EnumT> menu_select(
      const std::string &title,
      const std::vector<std::string> &items,
      int defaultIndex,
      const std::vector<EnumT> &values)
  {
    SelectResult<EnumT> result{};

    if (items.empty() || values.empty())
    {
      result.cancelled = true;
      return result;
    }

    const int count = static_cast<int>(items.size());
    int selected = defaultIndex;

    if (selected < 0 || selected >= count)
      selected = 0;

    if (!can_interact())
    {
      const std::size_t index = static_cast<std::size_t>(selected);
      if (index < values.size())
        result.value = values[index];
      return result;
    }

    CursorGuard cursor(true);
    SignalGuard sig;

#if !defined(_WIN32)
    RawMode raw;
    if (!raw.active)
    {
      const std::size_t index = static_cast<std::size_t>(selected);
      if (index < values.size())
        result.value = values[index];
      return result;
    }
#endif

    bool firstDraw = true;
    render_menu_single(title, items, selected, firstDraw);
    firstDraw = false;

    while (true)
    {
      if (g_cancelled.load())
      {
        std::cout << "\n";
        result.cancelled = true;
        return result;
      }

      const Key key = read_key();

      if (key == Key::CtrlC || key == Key::Escape || key == Key::Quit)
      {
        std::cout << "\n";
        result.cancelled = true;
        return result;
      }

      if (key == Key::Up)
      {
        selected = (selected - 1 + count) % count;
        render_menu_single(title, items, selected, firstDraw);
        continue;
      }

      if (key == Key::Down)
      {
        selected = (selected + 1) % count;
        render_menu_single(title, items, selected, firstDraw);
        continue;
      }

      if (key == Key::Enter || key == Key::Space)
      {
        const std::size_t index = static_cast<std::size_t>(selected);
        if (index < values.size())
          result.value = values[index];

        std::cout << "\n";
        result.cancelled = false;
        return result;
      }
    }
  }

  // ------------------------------------------------------------------
  // Multi-select menu (features)
  // ------------------------------------------------------------------

  struct MultiSelectResult
  {
    bool cancelled{false};
    std::vector<bool> selected{};
  };

  void render_lines(const std::vector<std::string> &lines, bool firstDraw);

  /// Run the features multi-select menu.
  MultiSelectResult menu_multiselect_features();

  // ------------------------------------------------------------------
  // Convenience wrappers used by NewCommand
  // ------------------------------------------------------------------

  SelectResult<TemplateKind> choose_template_interactive();
  SelectResult<OverwriteChoice> confirm_overwrite_interactive(const std::filesystem::path &dir);
  SelectResult<InPlaceDirChoice> confirm_inplace_dir_interactive(const std::filesystem::path &dir);

  /// Runs the features menu and maps the result to a FeaturesSelection.
  FeaturesSelection choose_features_interactive(bool &cancelled);

} // namespace vix::commands::new_cmd::tui
