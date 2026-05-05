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
#include <vix/cli/util/Ui.hpp>
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
namespace ui = vix::cli::util;

namespace vix::commands::new_cmd::tui
{

  // ------------------------------------------------------------------
  // Global cancellation flag
  // ------------------------------------------------------------------

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

  SignalGuard::~SignalGuard()
  {
    std::signal(SIGINT, old);
  }

  // ------------------------------------------------------------------
  // RawMode (Unix only)
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
  // Environment helpers
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

  bool can_interact()
  {
    return is_tty_stdin() && !is_noninteractive_env();
  }

  // ------------------------------------------------------------------
  // Terminal primitives
  // ------------------------------------------------------------------

  void clear_line() { std::cout << "\033[2K\r"; }

  void move_cursor_up(int lines)
  {
    if (lines > 0)
      std::cout << "\033[" << lines << "A";
  }

  void print_cancel_line()
  {
    std::cout << PAD << RED << "✖" << RESET << " Cancelled\n";
  }

  // ------------------------------------------------------------------
  // Key reading
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

      const bool ctrlDown = (k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

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
      if (ctrlDown && (ch == L'c' || ch == L'C'))
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

      pollfd pfd{};
      pfd.fd = STDIN_FILENO;
      pfd.events = POLLIN;

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
  // Single-select menu
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

    clear_line();
    std::cout << PAD << BOLD << CYAN << title << RESET << "\n";

    for (int i = 0; i < (int)items.size(); ++i)
    {
      clear_line();
      const bool active = (i == selected);
      if (active)
        std::cout << PAD << CYAN << BOLD << "❯ " << RESET << CYAN << BOLD << items[(std::size_t)i] << RESET << "\n";
      else
        std::cout << PAD << "  " << items[(std::size_t)i] << "\n";
    }
    std::cout.flush();
  }

  // ------------------------------------------------------------------
  // Multi-select features menu
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
      std::string tip;
      bool advanced{false};
      std::size_t index{0};
    };

    const std::vector<Item> items = {
        {"ORM (database layer)", "Maps to VIX_USE_ORM=ON.", false, 0},
        {"Sanitizers (debug only)", "Maps to VIX_ENABLE_SANITIZERS=ON.", false, 1},
        {"Static C++ runtime", "Maps to VIX_LINK_STATIC=ON.", false, 2},
        {"Full static binary (musl recommended)", "Maps to VIX_LINK_FULL_STATIC=ON.", true, 3},
    };

    int cursorIndex = 0;
    int selectedIndex = 0;
    res.selected[static_cast<std::size_t>(selectedIndex)] = true;

    auto render_item = [&](const Item &item, bool active) -> std::string
    {
      const bool sel = (selectedIndex == static_cast<int>(item.index));
      std::string line = PAD;
      line += "  ";
      if (active)
        line += std::string(CYAN) + BOLD + "❯ " + RESET;
      else
        line += "  ";
      if (sel)
        line += std::string(CYAN) + "● " + RESET;
      else
        line += std::string(GRAY) + "○ " + RESET;
      if (active)
        line += std::string(CYAN) + BOLD + item.label + RESET;
      else
        line += item.label;
      return line;
    };

    auto build_lines = [&](int activeIdx) -> std::vector<std::string>
    {
      std::vector<std::string> out;
      out.push_back(std::string(PAD) + BOLD + CYAN + "Core" + RESET);
      for (int i = 0; i < 3; ++i)
        out.push_back(render_item(items[static_cast<std::size_t>(i)], i == activeIdx));
      out.push_back("");
      out.push_back(std::string(PAD) + BOLD + CYAN + "Advanced" + RESET);
      out.push_back(render_item(items[3], activeIdx == 3));
      out.push_back("");
      out.push_back(std::string(PAD) + ui::dim(items[static_cast<std::size_t>(activeIdx)].tip));
      return out;
    };

    bool firstDraw = true;
    render_lines(build_lines(cursorIndex), firstDraw);
    firstDraw = false;

    while (true)
    {
      if (g_cancelled.load())
      {
        std::cout << "\n";
        ui::warn_line(std::cout, "Selection cancelled.");
        res.cancelled = true;
        return res;
      }

      const Key k = read_key();
      if (k == Key::CtrlC)
      {
        std::cout << "\n";
        ui::warn_line(std::cout, "Selection cancelled.");
        res.cancelled = true;
        return res;
      }

      if (k == Key::Up || k == Key::Down)
      {
        cursorIndex = (k == Key::Up) ? (cursorIndex - 1 + 4) % 4 : (cursorIndex + 1) % 4;
        selectedIndex = cursorIndex;
        std::fill(res.selected.begin(), res.selected.end(), false);
        res.selected[static_cast<std::size_t>(selectedIndex)] = true;
        render_lines(build_lines(cursorIndex), firstDraw);
        continue;
      }

      if (k == Key::Space || k == Key::Enter)
      {
        selectedIndex = cursorIndex;
        std::fill(res.selected.begin(), res.selected.end(), false);
        res.selected[static_cast<std::size_t>(selectedIndex)] = true;
        if (k == Key::Enter)
        {
          res.cancelled = false;
          return res;
        }
        render_lines(build_lines(cursorIndex), firstDraw);
        continue;
      }

      if (k == Key::Escape || k == Key::Quit)
      {
        std::cout << "\n";
        ui::warn_line(std::cout, "Selection cancelled.");
        res.cancelled = true;
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
        {"Application", "Library (header-only)"},
        0,
        {TemplateKind::App, TemplateKind::Lib});
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
} // namespace vix::commands::new_cmd::tui
