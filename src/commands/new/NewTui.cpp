/**
 * @file NewTui.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#if defined(_WIN32)
#include <vix/cli/platform/windows.hpp>
#endif

#include <vix/cli/commands/new/NewTui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::new_cmd::tui
{

  std::atomic<bool> g_cancelled{false};
  static void on_sigint(int) { g_cancelled.store(true); }

  // ------------------------------------------------------------------
  // SignalGuard
  // ------------------------------------------------------------------

  SignalGuard::SignalGuard()
  {
    g_cancelled.store(false);
    old = std::signal(SIGINT, on_sigint);
  }
  SignalGuard::~SignalGuard() { std::signal(SIGINT, old); }

  // ------------------------------------------------------------------
  // RawMode (Unix)
  // ------------------------------------------------------------------

#if !defined(_WIN32)
  RawMode::RawMode()
  {
    if (!::isatty(STDIN_FILENO))
      return;
    if (::tcgetattr(STDIN_FILENO, &old) != 0)
      return;
    termios t = old;
    t.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    t.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
      active = true;
  }
  RawMode::~RawMode()
  {
    if (active)
      ::tcsetattr(STDIN_FILENO, TCSANOW, &old);
  }
#endif

  // ------------------------------------------------------------------
  // CursorGuard
  // ------------------------------------------------------------------

  CursorGuard::CursorGuard(bool enable) : active(enable)
  {
    if (!active)
      return;
    std::cout << "\033[?25l";
    std::cout.flush();
  }
  CursorGuard::~CursorGuard()
  {
    if (!active)
      return;
    std::cout << "\033[?25h";
    std::cout.flush();
  }

  // ------------------------------------------------------------------
  // Env
  // ------------------------------------------------------------------

  bool is_tty_stdin()
  {
#if defined(_WIN32)
    return ::_isatty(0) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
  }

  bool is_noninteractive_env()
  {
    if (const char *v = vix::utils::vix_getenv("VIX_NONINTERACTIVE"))
    {
      const std::string s = v;
      if (!s.empty() && s != "0" && s != "false" && s != "FALSE")
        return true;
    }
    return vix::utils::vix_getenv("CI") != nullptr;
  }

  bool can_interact() { return is_tty_stdin() && !is_noninteractive_env(); }

  // ------------------------------------------------------------------
  // Terminal primitives
  // ------------------------------------------------------------------

  void clear_line() { std::cout << "\033[2K\r"; }
  void move_cursor_up(int n)
  {
    if (n > 0)
      std::cout << "\033[" << n << "A";
  }

  void print_cancel_line()
  {
    std::cout << PAD << RED << "✖" << RESET << " Cancelled\n";
  }

  // ------------------------------------------------------------------
  // Key reading (unchanged from original)
  // ------------------------------------------------------------------

  Key read_key()
  {
#if defined(_WIN32)
    HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE || hIn == nullptr)
      return Key::None;
    while (true)
    {
      if (g_cancelled.load())
        return Key::CtrlC;
      DWORD pending = 0;
      if (!::GetNumberOfConsoleInputEvents(hIn, &pending))
        return Key::None;
      if (pending == 0)
      {
        ::Sleep(50);
        continue;
      }
      INPUT_RECORD rec{};
      DWORD readCount = 0;
      if (!::ReadConsoleInputW(hIn, &rec, 1, &readCount) || readCount != 1)
        continue;
      if (rec.EventType != KEY_EVENT)
        continue;
      const KEY_EVENT_RECORD &k = rec.Event.KeyEvent;
      if (!k.bKeyDown)
        continue;
      const bool ctrl = (k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
      switch (k.wVirtualKeyCode)
      {
      case VK_UP:
        return Key::Up;
      case VK_DOWN:
        return Key::Down;
      case VK_RETURN:
        return Key::Enter;
      case VK_ESCAPE:
        return Key::Escape;
      default:
        break;
      }
      const wchar_t ch = k.uChar.UnicodeChar;
      if (ctrl && (ch == L'c' || ch == L'C'))
        return Key::CtrlC;
      if (ch == 3)
        return Key::CtrlC;
      if (ch == L' ')
        return Key::Space;
      if (ch == L'a' || ch == L'A')
        return Key::ToggleAll;
      if (ch == L'q' || ch == L'Q')
        return Key::Quit;
      if (ch == L'k' || ch == L'K')
        return Key::Up;
      if (ch == L'j' || ch == L'J')
        return Key::Down;
      if (ch == L'l' || ch == L'L')
        return Key::Enter;
      if (ch == L'h' || ch == L'H')
        return Key::Escape;
      return Key::None;
    }
#else
    while (true)
    {
      if (g_cancelled.load())
        return Key::CtrlC;
      pollfd pfd{STDIN_FILENO, POLLIN, 0};
      const int r = ::poll(&pfd, 1, 50);
      if (r < 0)
      {
        if (g_cancelled.load())
          return Key::CtrlC;
        continue;
      }
      if (r == 0)
        continue;
      unsigned char c = 0;
      if (::read(STDIN_FILENO, &c, 1) != 1)
        continue;
      if (c == 3)
        return Key::CtrlC;
      if (c == ' ')
        return Key::Space;
      if (c == 'a' || c == 'A')
        return Key::ToggleAll;
      if (c == 'k' || c == 'K')
        return Key::Up;
      if (c == 'j' || c == 'J')
        return Key::Down;
      if (c == 'l' || c == 'L')
        return Key::Enter;
      if (c == 'h' || c == 'H')
        return Key::Escape;
      if (c == '\n' || c == '\r')
        return Key::Enter;
      if (c == 'q' || c == 'Q')
        return Key::Quit;
      if (c == 27)
      {
        unsigned char s1 = 0, s2 = 0;
        pollfd p2{STDIN_FILENO, POLLIN, 0};
        if (::poll(&p2, 1, 10) <= 0)
          return Key::Escape;
        if (::read(STDIN_FILENO, &s1, 1) != 1)
          return Key::Escape;
        if (::poll(&p2, 1, 10) <= 0)
          return Key::Escape;
        if (::read(STDIN_FILENO, &s2, 1) != 1)
          return Key::Escape;
        if (s1 == '[')
        {
          if (s2 == 'A')
            return Key::Up;
          if (s2 == 'B')
            return Key::Down;
        }
        return Key::Escape;
      }
      return Key::None;
    }
#endif
  }

  // ------------------------------------------------------------------
  // Single-select menu — improved style
  //
  // Before:   "❯ Application"   (active)   |  "  Library"  (idle)
  // After:    "❯ Application"   (active, cyan bold)
  //           "  Library"       (idle, dim gray)
  //
  // Title line uses a faint bullet instead of raw bold text.
  // ------------------------------------------------------------------

  void render_menu_single(
      const std::string &title,
      const std::vector<std::string> &items,
      int selected,
      bool firstDraw)
  {
    const int total = 1 + (int)items.size();
    if (!firstDraw)
      move_cursor_up(total);

    // Title — dim bullet + label
    clear_line();
    std::cout << PAD << GRAY << title << RESET << "\n";

    for (int i = 0; i < (int)items.size(); ++i)
    {
      clear_line();
      const bool active = (i == selected);
      if (active)
        std::cout << PAD << CYAN << BOLD << "❯ " << items[(std::size_t)i] << RESET << "\n";
      else
        std::cout << PAD << GRAY << "  " << items[(std::size_t)i] << RESET << "\n";
    }
    std::cout.flush();
  }

  // ------------------------------------------------------------------
  // render_lines helper
  // ------------------------------------------------------------------

  void render_lines(const std::vector<std::string> &lines, bool firstDraw)
  {
    const int total = (int)lines.size();
    if (!firstDraw)
      move_cursor_up(total);
    for (int i = 0; i < total; ++i)
    {
      clear_line();
      std::cout << lines[(std::size_t)i] << "\n";
    }
    std::cout.flush();
  }

  // ------------------------------------------------------------------
  // Features multi-select — improved style
  //
  // Layout per row:
  //   "  ❯ ● ORM         maps to VIX_USE_ORM=ON"      ← active + selected
  //   "    ○ Sanitizers"                                ← idle, unselected
  //   "    ● Static C++ runtime"                        ← idle, selected
  //
  // • Hint is shown inline on the same row (dim gray) — no separate line
  // • Section headers ("Core" / "Advanced") stay but are even more faint
  // • Selected item uses ● cyan; unselected ○ gray
  // • Active cursor: "❯" cyan bold prefix
  // ------------------------------------------------------------------

  MultiSelectResult menu_multiselect_features()
  {
    MultiSelectResult res{};
    res.selected = std::vector<bool>(4, false);
    if (!can_interact())
      return res;

    CursorGuard cursor(true);
    SignalGuard sig;
#if !defined(_WIN32)
    RawMode raw;
    if (!raw.active)
      return res;
#endif

    struct Item
    {
      std::string label;
      std::string hint;
      bool advanced{false};
      std::size_t index{0};
    };

    const std::vector<Item> items = {
        {"ORM", "VIX_USE_ORM=ON", false, 0},
        {"Sanitizers", "VIX_ENABLE_SANITIZERS=ON", false, 1},
        {"Static C++ runtime", "VIX_LINK_STATIC=ON", false, 2},
        {"Full static", "VIX_LINK_FULL_STATIC=ON", true, 3},
    };

    int cursor_idx = 0;

    // Build one display line per item
    // Format: [cursor] [dot] [label]  [dim hint]
    auto make_line = [&](std::size_t idx) -> std::string
    {
      const auto &item = items[idx];
      const bool active = ((int)idx == cursor_idx);
      const bool sel = res.selected[idx];

      std::string line;
      line += PAD;
      line += active
                  ? std::string(CYAN) + BOLD + "❯ " + RESET
                  : "  ";
      line += sel
                  ? std::string(CYAN) + "● " + RESET
                  : std::string(GRAY) + "○ " + RESET;

      // label
      if (active)
        line += std::string(CYAN) + BOLD + item.label + RESET;
      else if (sel)
        line += std::string(BOLD) + item.label + RESET;
      else
        line += std::string(GRAY) + item.label + RESET;

      // inline hint — always dim, same row
      if (!item.hint.empty())
      {
        // pad label to column 22 for alignment
        const int label_len = (int)item.label.size() + 2; // 2 for "● "
        const int cursor_len = 2;
        const int total_text = cursor_len + label_len;
        const int target_col = 26;
        const int spaces = std::max(2, target_col - total_text);
        line += std::string((std::size_t)spaces, ' ');
        line += GRAY + item.hint + RESET;
      }

      return line;
    };

    auto build_lines = [&]() -> std::vector<std::string>
    {
      std::vector<std::string> out;

      out.push_back(std::string(PAD) + GRAY + "Core" + RESET);
      for (std::size_t i = 0; i < 3; ++i)
        out.push_back(make_line(i));

      out.push_back("");

      out.push_back(std::string(PAD) + GRAY + "Advanced" + RESET);
      out.push_back(make_line(3));

      return out;
    };

    bool firstDraw = true;
    render_lines(build_lines(), firstDraw);
    firstDraw = false;

    while (true)
    {
      if (g_cancelled.load())
      {
        std::cout << "\n";
        std::cout << PAD << YELLOW << "!" << RESET << " Selection cancelled.\n";
        res.cancelled = true;
        return res;
      }

      const Key k = read_key();

      if (k == Key::CtrlC || k == Key::Escape || k == Key::Quit)
      {
        std::cout << "\n";
        std::cout << PAD << YELLOW << "!" << RESET << " Selection cancelled.\n";
        res.cancelled = true;
        return res;
      }

      if (k == Key::Up || k == Key::Down)
      {
        cursor_idx = (k == Key::Up)
                         ? (cursor_idx - 1 + 4) % 4
                         : (cursor_idx + 1) % 4;
        render_lines(build_lines(), firstDraw);
        continue;
      }

      if (k == Key::Space)
      {
        // toggle current item
        res.selected[(std::size_t)cursor_idx] = !res.selected[(std::size_t)cursor_idx];
        render_lines(build_lines(), firstDraw);
        continue;
      }

      if (k == Key::ToggleAll)
      {
        const bool any = std::any_of(res.selected.begin(), res.selected.end(), [](bool b)
                                     { return b; });
        std::fill(res.selected.begin(), res.selected.end(), !any);
        render_lines(build_lines(), firstDraw);
        continue;
      }

      if (k == Key::Enter)
      {
        std::cout << "\n";
        res.cancelled = false;
        return res;
      }
    }
  }

  // ------------------------------------------------------------------
  // Convenience wrappers
  // ------------------------------------------------------------------

  SelectResult<TemplateKind> choose_template_interactive()
  {
    return menu_select<TemplateKind>(
        "Template",
        {
            "Application",
            "Backend production",
            "Game",
            "Library (header-only)",
        },
        0,
        {
            TemplateKind::App,
            TemplateKind::Backend,
            TemplateKind::Game,
            TemplateKind::Lib,
        });
  }

  SelectResult<OverwriteChoice> confirm_overwrite_interactive(const std::filesystem::path &dir)
  {
    std::cout << PAD << GRAY << dir.string() << RESET << "\n";
    return menu_select<OverwriteChoice>(
        "Directory exists",
        {"Overwrite (delete and recreate)", "Cancel"},
        1,
        {OverwriteChoice::Overwrite, OverwriteChoice::Cancel});
  }

  SelectResult<InPlaceDirChoice> confirm_inplace_dir_interactive(const std::filesystem::path &dir)
  {
    std::cout << PAD << GRAY << dir.string() << RESET << "\n";
    return menu_select<InPlaceDirChoice>(
        "Current directory is not empty",
        {"Continue (overwrite template files in current directory)", "Cancel"},
        1,
        {InPlaceDirChoice::Proceed, InPlaceDirChoice::Cancel});
  }

  FeaturesSelection choose_features_interactive(bool &cancelled)
  {
    cancelled = false;
    MultiSelectResult r = menu_multiselect_features();
    if (r.cancelled)
    {
      cancelled = true;
      return FeaturesSelection{};
    }

    FeaturesSelection f{};
    f.orm = r.selected.size() > 0 && r.selected[0];
    f.sanitizers = r.selected.size() > 1 && r.selected[1];
    f.static_rt = r.selected.size() > 2 && r.selected[2];
    f.full_static = r.selected.size() > 3 && r.selected[3];
    return f;
  }

  // Explicit instantiations
  template SelectResult<TemplateKind> menu_select(const std::string &, const std::vector<std::string> &, int, const std::vector<TemplateKind> &);
  template SelectResult<OverwriteChoice> menu_select(const std::string &, const std::vector<std::string> &, int, const std::vector<OverwriteChoice> &);
  template SelectResult<InPlaceDirChoice> menu_select(const std::string &, const std::vector<std::string> &, int, const std::vector<InPlaceDirChoice> &);

} // namespace vix::commands::new_cmd::tui
