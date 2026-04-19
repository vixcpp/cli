/**
 *
 *  @file NewCommand.cpp
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
#if defined(_WIN32)
#include <vix/cli/platform/windows.hpp>
#endif

#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/Utils.hpp>
#include <vix/utils/Env.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/util/Ui.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
#include <cctype>
#include <system_error>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
#include <poll.h>
#endif

namespace fs = std::filesystem;
using namespace vix::cli::style;
namespace ui = vix::cli::util;

namespace
{
  static bool is_dot_path(const std::string &s)
  {
    return s == "." || s == "./" || s == ".\\";
  }

  static std::string current_dir_name()
  {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::current_path(), ec);
    if (ec)
      p = fs::current_path();

    std::string name = p.filename().string();
    if (name.empty())
      name = "app";
    return name;
  }

  enum class TemplateKind
  {
    App,
    Lib
  };

  struct FeaturesSelection
  {
    bool orm{false};         // VIX_USE_ORM=ON
    bool sanitizers{false};  // VIX_ENABLE_SANITIZERS=ON
    bool static_rt{false};   // VIX_LINK_STATIC=ON
    bool full_static{false}; // VIX_LINK_FULL_STATIC=ON
  };

  enum class OverwriteChoice
  {
    Overwrite,
    Cancel
  };

  // ---------- Templates ----------
  constexpr const char *kMainCpp = R"(#include <vix.hpp>
using namespace vix;

int main()
{
  App app;

  // GET /
  app.get("/", [](Request&, Response& res) {
    res.send("Hello world");
  });

  app.run(8080);
}
)";

  constexpr const char *kBasicTestCpp_App = R"(#include <vix/tests/tests.hpp>

int main()
{
  using namespace vix::tests;

  auto &registry = TestRegistry::instance();
  registry.clear();

  registry.add(TestCase("app basic test", [] {
    Assert::equal(2 + 2, 4);
  }));

  return TestRunner::run_all_and_exit();
}
)";

  constexpr const char *kAppConfigJson = R"JSON({
  "server": {
    "port": 8080,
    "request_timeout": 2000,
    "io_threads": 0,
    "session_timeout_sec": 20
  },
  "logging": {
    "async": true,
    "queue_max": 20000,
    "drop_on_overflow": true
  },
  "waf": {
    "mode": "basic",
    "max_target_len": 4096,
    "max_body_bytes": 1048576
  },
  "database": {
    "default": {
      "host": "localhost",
      "user": "root",
      "password": "",
      "name": "",
      "port": 3306
    }
  }
}
)JSON";

  static std::string make_lib_header(const std::string &name)
  {
    std::string s;
    s.reserve(1500);

    s += "#pragma once\n";
    s += "#include <cstddef>\n";
    s += "#include <vector>\n\n";
    s += "namespace " + name + "\n";
    s += "{\n";
    s += "  struct Node\n";
    s += "  {\n";
    s += "    std::size_t id{};\n";
    s += "    std::vector<std::size_t> children{};\n";
    s += "  };\n\n";
    s += "  inline std::vector<Node> make_chain(std::size_t n)\n";
    s += "  {\n";
    s += "    std::vector<Node> nodes;\n";
    s += "    nodes.reserve(n);\n\n";
    s += "    for (std::size_t i = 0; i < n; ++i)\n";
    s += "      nodes.push_back(Node{i, {}});\n\n";
    s += "    for (std::size_t i = 0; i + 1 < n; ++i)\n";
    s += "      nodes[i].children.push_back(i + 1);\n\n";
    s += "    return nodes;\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  static std::string make_basic_test_cpp_lib(const std::string &name)
  {
    std::string s;
    s.reserve(1200);

    s += "#include <vix/tests/tests.hpp>\n";
    s += "#include <" + name + "/" + name + ".hpp>\n\n";

    s += "int main()\n";
    s += "{\n";
    s += "  using namespace vix::tests;\n\n";
    s += "  auto &registry = TestRegistry::instance();\n";
    s += "  registry.clear();\n\n";

    s += "  registry.add(TestCase(\"" + name + " basic test\", [] {\n";
    s += "    auto nodes = " + name + "::make_chain(5);\n";
    s += "    Assert::equal(nodes.size(), static_cast<std::size_t>(5));\n";
    s += "  }));\n\n";

    s += "  return TestRunner::run_all_and_exit();\n";
    s += "}\n";

    return s;
  }

  static std::string make_basic_example_cpp_lib(const std::string &name)
  {
    std::string s;
    s.reserve(700);

    s += "#include <" + name + "/" + name + ".hpp>\n";
    s += "#include <iostream>\n\n";
    s += "int main()\n";
    s += "{\n";
    s += "  auto nodes = " + name + "::make_chain(3);\n";
    s += "  std::cout << \"nodes=\" << nodes.size() << \"\\n\";\n";
    s += "  return 0;\n";
    s += "}\n";

    return s;
  }

  static std::string make_examples_cmakelists_lib(const std::string &name)
  {
    std::string s;
    s.reserve(256);

    s += name + "_add_example(" + name + "_example_basic basic.cpp)\n";

    return s;
  }

  // ---------- TTY / env ----------
  static bool is_tty_stdin()
  {
#if defined(_WIN32)
    return ::_isatty(0) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
  }

  static bool is_noninteractive_env()
  {
    if (const char *v = vix::utils::vix_getenv("VIX_NONINTERACTIVE"))
    {
      const std::string s = v;
      if (!s.empty() && s != "0" && s != "false" && s != "FALSE")
        return true;
    }
    if (vix::utils::vix_getenv("CI") != nullptr)
      return true;

    return false;
  }

  static bool can_interact()
  {
    return is_tty_stdin() && !is_noninteractive_env();
  }

  // ---------- argv helpers ----------
  static bool consume_flag(std::vector<std::string> &a, const std::string &flag)
  {
    auto it = std::find(a.begin(), a.end(), flag);
    if (it == a.end())
      return false;
    a.erase(it);
    return true;
  }

  static bool has_any(const std::vector<std::string> &a, const std::vector<std::string> &candidates)
  {
    for (const auto &x : candidates)
    {
      if (std::find(a.begin(), a.end(), x) != a.end())
        return true;
    }
    return false;
  }

  static std::optional<std::string> take_option_value(std::vector<std::string> &a, const std::vector<std::string> &names)
  {
    for (const auto &n : names)
    {
      for (std::size_t i = 0; i < a.size(); ++i)
      {
        if (a[i] == n)
        {
          if (i + 1 >= a.size())
            return std::nullopt;
          std::string v = a[i + 1];
          a.erase(a.begin() + (long)i, a.begin() + (long)i + 2);
          return v;
        }

        const std::string prefix = n + "=";
        if (a[i].rfind(prefix, 0) == 0)
        {
          std::string v = a[i].substr(prefix.size());
          a.erase(a.begin() + (long)i);
          return v;
        }
      }
    }
    return std::nullopt;
  }

  // ---------- fs helpers ----------
  static bool dir_exists(const fs::path &p)
  {
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_directory(p, ec);
  }

  static bool dir_is_empty(const fs::path &p)
  {
    std::error_code ec;

    if (!dir_exists(p))
      return true;

    fs::directory_iterator it(p, ec);
    if (ec)
      return false;

    return it == fs::directory_iterator{};
  }

  static bool ensure_dir(const fs::path &p, std::string &err)
  {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec)
    {
      err = ec.message();
      return false;
    }
    return true;
  }

  static bool write_text_file(const fs::path &p, const std::string &content, std::string &err)
  {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec)
    {
      err = ec.message();
      return false;
    }

    std::ofstream out(p, std::ios::binary);
    if (!out)
    {
      err = "cannot open file for writing";
      return false;
    }
    out.write(content.data(), (std::streamsize)content.size());
    if (!out.good())
    {
      err = "write failed";
      return false;
    }
    return true;
  }

  // Interactive UI engine
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

  static std::atomic<bool> g_cancelled{false};

  static void on_sigint(int)
  {
    g_cancelled.store(true);
  }

  struct SignalGuard
  {
    using Handler = void (*)(int);
    Handler old{nullptr};

    SignalGuard()
    {
      g_cancelled.store(false);
      old = std::signal(SIGINT, on_sigint);
    }

    ~SignalGuard()
    {
      std::signal(SIGINT, old);
    }
  };

#if !defined(_WIN32)
  struct RawMode
  {
    termios old{};
    bool active{false};

    RawMode()
    {
      if (!::isatty(STDIN_FILENO))
        return;

      if (::tcgetattr(STDIN_FILENO, &old) != 0)
        return;

      termios t = old;

      const tcflag_t lmask = static_cast<tcflag_t>(ICANON | ECHO);
      const tcflag_t imask = static_cast<tcflag_t>(IXON | ICRNL);

      t.c_lflag &= static_cast<tcflag_t>(~lmask);
      t.c_iflag &= static_cast<tcflag_t>(~imask);

      t.c_cc[VMIN] = 1;
      t.c_cc[VTIME] = 0;

      if (::tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
        active = true;
    }

    ~RawMode()
    {
      if (active)
        ::tcsetattr(STDIN_FILENO, TCSANOW, &old);
    }
  };
#endif

  struct CursorGuard
  {
    bool active{false};

    explicit CursorGuard(bool enable) : active(enable)
    {
      if (!active)
        return;
      std::cout << "\033[?25l";
      std::cout.flush();
    }

    ~CursorGuard()
    {
      if (!active)
        return;
      std::cout << "\033[?25h";
      std::cout.flush();
    }
  };

  static void clear_line()
  {
    std::cout << "\033[2K\r";
  }

  static void move_cursor_up(int lines)
  {
    if (lines > 0)
      std::cout << "\033[" << lines << "A";
  }

  static Key read_key()
  {
#if defined(_WIN32)
    // -------- Windows (Console Input) --------
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
        ::Sleep(50); // keep responsive
        continue;
      }

      INPUT_RECORD rec{};
      DWORD readCount = 0;
      if (!::ReadConsoleInputW(hIn, &rec, 1, &readCount) || readCount != 1)
        continue;

      if (rec.EventType != KEY_EVENT)
        continue;

      const KEY_EVENT_RECORD &k = rec.Event.KeyEvent;

      // only handle key DOWN
      if (!k.bKeyDown)
        continue;

      // Ctrl+C detection (either control state or ascii 3)
      const bool ctrlDown = (k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

      // Virtual keys first (arrows, enter, escape)
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

      // Character-based mapping
      const wchar_t ch = k.uChar.UnicodeChar;

      if (ctrlDown && (ch == L'c' || ch == L'C'))
        return Key::CtrlC;

      if (ch == 3) // ETX (rare here, but safe)
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

      // unknown key -> ignore
      return Key::None;
    }
    return Key::None;
#else
    // -------- Unix (poll/read) --------
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
        continue; // timeout, re-check cancelled

      unsigned char c = 0;
      const ssize_t n = ::read(STDIN_FILENO, &c, 1);
      if (n != 1)
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

      if (c == 27)
      {
        unsigned char s1 = 0;
        unsigned char s2 = 0;

        pollfd p2{};
        p2.fd = STDIN_FILENO;
        p2.events = POLLIN;

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

      if (c == 'q' || c == 'Q')
        return Key::Quit;

      return Key::None;
    }
#endif
  }

  static void print_cancel_line()
  {
    std::cout << PAD << RED << "✖" << RESET << " Cancelled\n";
  }

  // ---------- Single select ----------
  template <typename T>
  struct SelectResult
  {
    bool cancelled{false};
    T value{};
  };

  static void render_menu_single(
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
      {
        std::cout << PAD << CYAN << BOLD << "❯ " << RESET
                  << CYAN << BOLD << items[(std::size_t)i] << RESET << "\n";
      }
      else
      {
        std::cout << PAD << "  " << items[(std::size_t)i] << "\n";
      }
    }

    std::cout.flush();
  }

  template <typename EnumT>
  static SelectResult<EnumT> menu_select(
      const std::string &title,
      const std::vector<std::string> &items,
      int defaultIndex,
      const std::vector<EnumT> &values)
  {
    SelectResult<EnumT> res{};

    if (items.empty() || values.size() != items.size())
    {
      res.cancelled = false;
      res.value = values.empty() ? EnumT{} : values[0];
      return res;
    }

    const int clampIdx = std::max(0, std::min(defaultIndex, (int)values.size() - 1));
    res.value = values[(std::size_t)clampIdx];

    if (!can_interact())
      return res;

    CursorGuard cursor(true);
    SignalGuard sig;

#if !defined(_WIN32)
    RawMode raw;
    if (!raw.active)
      return res;
#endif

    int selected = clampIdx;
    bool firstDraw = true;

    render_menu_single(title, items, selected, firstDraw);
    firstDraw = false;

    while (true)
    {
      if (g_cancelled.load())
      {
        std::cout << "\n";
        print_cancel_line();
        res.cancelled = true;
        return res;
      }

      const Key k = read_key();

      if (k == Key::CtrlC)
      {
        std::cout << "\n";
        print_cancel_line();
        res.cancelled = true;
        return res;
      }

      if (k == Key::Up)
      {
        selected -= 1;
        if (selected < 0)
          selected = (int)items.size() - 1;
        render_menu_single(title, items, selected, firstDraw);
        continue;
      }

      if (k == Key::Down)
      {
        selected = (selected + 1) % (int)items.size();
        render_menu_single(title, items, selected, firstDraw);
        continue;
      }

      if (k == Key::Enter)
      {
        std::cout << "\n";
        res.value = values[(std::size_t)selected];
        res.cancelled = false;
        return res;
      }

      if (k == Key::Escape || k == Key::Quit)
      {
        std::cout << "\n";
        print_cancel_line();
        res.cancelled = true;
        return res;
      }
    }
  }

  enum class InPlaceDirChoice
  {
    Proceed,
    Cancel
  };

  static SelectResult<InPlaceDirChoice> confirm_inplace_dir_interactive(const fs::path &dir)
  {
    std::cout << PAD << GRAY << dir.string() << RESET << "\n";

    const std::vector<std::string> items = {
        "Continue (overwrite template files in current directory)",
        "Cancel"};

    const std::vector<InPlaceDirChoice> values = {
        InPlaceDirChoice::Proceed,
        InPlaceDirChoice::Cancel};

    return menu_select<InPlaceDirChoice>("Current directory is not empty", items, 1, values);
  }

  // ---------- Multi select (Features) ----------
  struct MultiSelectResult
  {
    bool cancelled{false};
    std::vector<bool> selected{};
  };

  static void render_lines(const std::vector<std::string> &lines, bool firstDraw)
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

  static MultiSelectResult menu_multiselect_features()
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
      const bool selected = (selectedIndex == static_cast<int>(item.index));

      std::string line = PAD;
      line += "  ";

      if (active)
        line += std::string(CYAN) + BOLD + "❯ " + RESET;
      else
        line += "  ";

      if (selected)
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

      if (k == Key::Up)
      {
        cursorIndex = (cursorIndex - 1 + 4) % 4;
        selectedIndex = cursorIndex;

        std::fill(res.selected.begin(), res.selected.end(), false);
        res.selected[static_cast<std::size_t>(selectedIndex)] = true;

        render_lines(build_lines(cursorIndex), firstDraw);
        continue;
      }

      if (k == Key::Down)
      {
        cursorIndex = (cursorIndex + 1) % 4;
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

  static std::string make_readme_app(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(8000);

    readme += "# " + projectName + "\n\n";
    readme += "Minimal Vix.cpp application.\n\n";

    // ---------------------------
    // Quick start (IMPORTANT)
    // ---------------------------
    readme += "## Quick start\n\n";
    readme += "```bash\n";
    readme += "cd " + projectName + "\n";
    readme += "cp .env.example .env\n";
    readme += "vix build\n";
    readme += "vix run\n";
    readme += "```\n\n";

    readme += "Then open:\n\n";
    readme += "```\n";
    readme += "http://localhost:8080\n";
    readme += "```\n\n";

    // ---------------------------
    // Dependencies
    // ---------------------------
    readme += "## Dependencies\n\n";
    readme += "This project uses a `vix.json` manifest.\n\n";

    readme += "Workflow:\n\n";
    readme += "- `vix add <pkg>` → add dependency\n";
    readme += "- `vix install` → install dependencies\n";
    readme += "- `vix.lock` → ensures reproducible builds\n\n";

    readme += "Example:\n\n";
    readme += "```bash\n";
    readme += "vix add gk/json@^1.0.0\n";
    readme += "vix install\n";
    readme += "```\n\n";

    // ---------------------------
    // Tasks
    // ---------------------------
    readme += "## Tasks\n\n";
    readme += "Run project tasks:\n\n";

    readme += "```bash\n";
    readme += "vix task <name>\n";
    readme += "```\n\n";

    readme += "Common tasks:\n\n";
    readme += "```bash\n";
    readme += "vix task dev\n";
    readme += "vix task test\n";
    readme += "vix task ci\n";
    readme += "```\n\n";

    readme += "Edit `vix.json` to customize tasks and pipelines.\n\n";

    // ---------------------------
    // Configuration
    // ---------------------------
    readme += "## Configuration\n\n";

    readme += "Vix uses `.env` files for configuration.\n\n";

    readme += "Start by copying the example:\n\n";
    readme += "```bash\n";
    readme += "cp .env.example .env\n";
    readme += "```\n\n";

    readme += "Example:\n\n";
    readme += "```env\n";
    readme += "SERVER_PORT=8080\n";
    readme += "DATABASE_ENGINE=mysql\n";
    readme += "DATABASE_DEFAULT_HOST=127.0.0.1\n";
    readme += "DATABASE_DEFAULT_PORT=3306\n";
    readme += "DATABASE_DEFAULT_USER=root\n";
    readme += "DATABASE_DEFAULT_PASSWORD=\n";
    readme += "DATABASE_DEFAULT_NAME=appdb\n";
    readme += "LOGGING_ASYNC=true\n";
    readme += "WAF_MODE=basic\n";
    readme += "```\n\n";

    // ---------------------------
    // Code usage
    // ---------------------------
    readme += "## Using configuration in code\n\n";

    readme += "```cpp\n";
    readme += "#include <vix.hpp>\n";
    readme += "using namespace vix;\n\n";
    readme += "int main()\n";
    readme += "{\n";
    readme += "  config::Config cfg{\".env\"};\n\n";
    readme += "  App app;\n";
    readme += "  app.get(\"/\", [](Request&, Response& res) {\n";
    readme += "    res.send(\"Hello world\");\n";
    readme += "  });\n\n";
    readme += "  app.run(cfg.getServerPort());\n";
    readme += "}\n";
    readme += "```\n\n";

    // ---------------------------
    // Env mapping
    // ---------------------------
    readme += "## Environment mapping\n\n";

    readme += "Vix maps config keys to environment variables:\n\n";

    readme += "- `server.port` → `SERVER_PORT`\n";
    readme += "- `database.default.host` → `DATABASE_DEFAULT_HOST`\n";
    readme += "- `database.default.name` → `DATABASE_DEFAULT_NAME`\n\n";

    readme += "This keeps the C++ API clean and environment-driven.\n\n";

    // ---------------------------
    // Layered env
    // ---------------------------
    readme += "## Environment layers\n\n";

    readme += "You can use multiple env files:\n\n";
    readme += "- `.env`\n";
    readme += "- `.env.local`\n";
    readme += "- `.env.production`\n\n";

    readme += "Use `.env` for development and environment-specific files for deployment.\n";

    return readme;
  }

  static std::string make_readme_lib(const std::string &name)
  {
    std::string readme;
    readme.reserve(6500);

    readme += "# " + name + "\n\n";
    readme += "Header-only C++ library scaffold.\n\n";

    readme += "## Principles\n\n";
    readme += "This scaffold is generated to stay deterministic, composable, and registry-safe.\n\n";

    readme += "Key rules:\n\n";
    readme += "- tests are OFF by default\n";
    readme += "- examples are OFF by default\n";
    readme += "- the library exposes a stable alias target\n";
    readme += "- example targets are prefixed to avoid collisions\n";
    readme += "- the package is safe for add_subdirectory(...) and Vix registry integration\n\n";

    readme += "## Targets\n\n";
    readme += "Canonical target:\n\n";
    readme += "```cmake\n";
    readme += name + "::" + name + "\n";
    readme += "```\n\n";

    readme += "## Manifest\n\n";
    readme += "This project includes a `vix.json` manifest.\n\n";
    readme += "For libraries, `vix.json` is used to describe package metadata and declared dependencies.\n\n";
    readme += "Important:\n\n";
    readme += "- `vix.json` stores declared dependency requirements\n";
    readme += "- `vix.lock` stores exact resolved versions for reproducible installs\n";
    readme += "- `vix add` updates both `vix.json` and `vix.lock`\n";
    readme += "- `vix install` installs dependencies from `vix.lock`\n\n";

    readme += "Example:\n\n";
    readme += "```bash\n";
    readme += "vix add gk/json@^1.0.0\n";
    readme += "vix install\n";
    readme += "```\n\n";

    readme += "## Build\n\n";

    readme += "Build project:\n\n";
    readme += "```bash\n";
    readme += "vix build\n";
    readme += "```\n\n";

    readme += "Build with tests enabled:\n\n";
    readme += "```bash\n";
    readme += "vix build -- -D" + name + "_BUILD_TESTS=ON\n";
    readme += "```\n\n";

    readme += "Build with examples enabled:\n\n";
    readme += "```bash\n";
    readme += "vix build -- -D" + name + "_BUILD_EXAMPLES=ON\n";
    readme += "```\n\n";

    readme += "Build tests + examples:\n\n";
    readme += "```bash\n";
    readme += "vix build -- -D" + name + "_BUILD_TESTS=ON -D" + name + "_BUILD_EXAMPLES=ON\n";
    readme += "```\n\n";

    readme += "## Tests\n\n";
    readme += "```bash\n";
    readme += "vix tests\n";
    readme += "```\n\n";

    readme += "## Notes\n\n";
    readme += "- Uses embedded Vix CMake presets (dev, dev-ninja, release)\n";
    readme += "- Automatically configures and builds (no manual cmake needed)\n";
    readme += "- Pass extra CMake flags after `--`\n";
    readme += "- Edit `vix.json` metadata before publishing the package\n";

    return readme;
  }

  static std::string make_cmake_presets_json_app(const FeaturesSelection &f)
  {
    std::string json;
    json.reserve(9000);

    json += "{\n";
    json += "  \"version\": 6,\n\n";
    json += "  \"configurePresets\": [\n";
    json += "    {\n";
    json += "      \"name\": \"dev-ninja\",\n";
    json += "      \"displayName\": \"Dev (Ninja, Debug)\",\n";
    json += "      \"generator\": \"Ninja\",\n";
    json += "      \"binaryDir\": \"build-ninja\",\n";
    json += "      \"cacheVariables\": {\n";
    json += "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
    json += "        \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\"";

    if (f.orm)
      json += ",\n        \"VIX_USE_ORM\": \"ON\"";
    if (f.static_rt)
      json += ",\n        \"VIX_LINK_STATIC\": \"ON\"";
    if (f.full_static)
      json += ",\n        \"VIX_LINK_FULL_STATIC\": \"ON\"";

    json += "\n      }\n";
    json += "    }";

    if (f.sanitizers)
    {
      json += ",\n";
      json += "    {\n";
      json += "      \"name\": \"dev-ninja-san\",\n";
      json += "      \"displayName\": \"Dev (Ninja, ASan+UBSan, Debug)\",\n";
      json += "      \"generator\": \"Ninja\",\n";
      json += "      \"binaryDir\": \"build-ninja-san\",\n";
      json += "      \"cacheVariables\": {\n";
      json += "        \"CMAKE_BUILD_TYPE\": \"Debug\",\n";
      json += "        \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\",\n";
      json += "        \"VIX_ENABLE_SANITIZERS\": \"ON\"";
      if (f.orm)
        json += ",\n        \"VIX_USE_ORM\": \"ON\"";
      if (f.static_rt)
        json += ",\n        \"VIX_LINK_STATIC\": \"ON\"";
      if (f.full_static)
        json += ",\n        \"VIX_LINK_FULL_STATIC\": \"ON\"";
      json += "\n      }\n";
      json += "    }";
    }

    json += ",\n";
    json += "    {\n";
    json += "      \"name\": \"release\",\n";
    json += "      \"displayName\": \"Release (Ninja, Release)\",\n";
    json += "      \"generator\": \"Ninja\",\n";
    json += "      \"binaryDir\": \"build-release\",\n";
    json += "      \"cacheVariables\": {\n";
    json += "        \"CMAKE_BUILD_TYPE\": \"Release\",\n";
    json += "        \"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\"";
    if (f.orm)
      json += ",\n        \"VIX_USE_ORM\": \"ON\"";
    if (f.static_rt)
      json += ",\n        \"VIX_LINK_STATIC\": \"ON\"";
    if (f.full_static)
      json += ",\n        \"VIX_LINK_FULL_STATIC\": \"ON\"";
    json += "\n      }\n";
    json += "    },\n";
    json += "    {\n";
    json += "      \"name\": \"dev-msvc\",\n";
    json += "      \"displayName\": \"Dev (MSVC, Release)\",\n";
    json += "      \"generator\": \"Visual Studio 17 2022\",\n";
    json += "      \"architecture\": { \"value\": \"x64\" },\n";
    json += "      \"binaryDir\": \"build-msvc\",\n";
    json += "      \"cacheVariables\": {\n";
    json += "        \"CMAKE_CONFIGURATION_TYPES\": \"Release\"";
    if (f.orm)
      json += ",\n        \"VIX_USE_ORM\": \"ON\"";
    json += "\n      }\n";
    json += "    }\n";
    json += "  ],\n\n";

    json += "  \"buildPresets\": [\n";
    json += "    { \"name\": \"build-ninja\", \"displayName\": \"Build (ALL, Ninja Debug)\", \"configurePreset\": \"dev-ninja\" }";
    if (f.sanitizers)
      json += ",\n    { \"name\": \"build-ninja-san\", \"displayName\": \"Build (ALL, Ninja Debug, ASan+UBSan)\", \"configurePreset\": \"dev-ninja-san\" }";
    json += ",\n    { \"name\": \"build-release\", \"displayName\": \"Build (ALL, Ninja Release)\", \"configurePreset\": \"release\" },\n";
    json += "    { \"name\": \"run-dev-ninja\", \"displayName\": \"Run (target=run, Ninja Debug)\", \"configurePreset\": \"dev-ninja\", \"targets\": [\"run\"] },\n";
    json += "    { \"name\": \"run-release\", \"displayName\": \"Run (target=run, Ninja Release)\", \"configurePreset\": \"release\", \"targets\": [\"run\"] },\n";
    json += "    { \"name\": \"build-msvc\", \"displayName\": \"Build (ALL, MSVC)\", \"configurePreset\": \"dev-msvc\", \"configuration\": \"Release\" },\n";
    json += "    { \"name\": \"run-msvc\", \"displayName\": \"Run (target=run, MSVC)\", \"configurePreset\": \"dev-msvc\", \"configuration\": \"Release\", \"targets\": [\"run\"] }\n";
    json += "  ]\n";
    json += "}\n";

    return json;
  }

  static std::string make_cmake_presets_json_lib()
  {
    return R"JSON({
  "version": 6,
  "configurePresets": [
    {
      "name": "dev-ninja",
      "displayName": "Dev (Ninja, Debug)",
      "generator": "Ninja",
      "binaryDir": "build-ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "release",
      "displayName": "Release (Ninja, Release)",
      "generator": "Ninja",
      "binaryDir": "build-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "dev-msvc",
      "displayName": "Dev (MSVC, Release)",
      "generator": "Visual Studio 17 2022",
      "architecture": { "value": "x64" },
      "binaryDir": "build-msvc",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "Release"
      }
    }
  ],
  "buildPresets": [
    { "name": "build-ninja", "displayName": "Build (ALL, Ninja Debug)", "configurePreset": "dev-ninja" },
    { "name": "build-release", "displayName": "Build (ALL, Ninja Release)", "configurePreset": "release" },
    { "name": "build-msvc", "displayName": "Build (ALL, MSVC)", "configurePreset": "dev-msvc", "configuration": "Release" }
  ]
}
)JSON";
  }

  static std::string make_project_manifest_app(const std::string &name, const FeaturesSelection &f)
  {
    // Minimal + strict: only include selected feature flags as extra lines
    std::string s;
    s.reserve(800);

    s += "version = 1\n\n";
    s += "[app]\n";
    s += "kind = \"project\"\n";
    s += "dir = \".\"\n";
    s += "name = \"" + name + "\"\n";
    s += "entry = \"src/main.cpp\"\n\n";
    s += "[build]\n";
    s += "preset = \"dev-ninja\"\n";
    s += "run_preset = \"run-dev-ninja\"\n";

    if (f.orm || f.sanitizers || f.static_rt || f.full_static)
    {
      // Store feature state explicitly for reproducibility, but only when needed.
      s += "\n[features]\n";
      if (f.orm)
        s += "orm = true\n";
      if (f.sanitizers)
        s += "sanitizers = true\n";
      if (f.static_rt)
        s += "static_rt = true\n";
      if (f.full_static)
        s += "full_static = true\n";
    }

    return s;
  }

  static std::string make_project_manifest_lib(const std::string &name)
  {
    return "version = 1\n\n"
           "[app]\n"
           "kind = \"project\"\n"
           "dir = \".\"\n"
           "name = \"" +
           name + "\"\n"
                  "entry = \"tests/test_basic.cpp\"\n\n"
                  "[build]\n"
                  "preset = \"dev-ninja\"\n";
  }

  static std::string make_vix_json_lib(const std::string &name)
  {
    std::string s;
    s.reserve(1600);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"namespace\": \"your-namespace\",\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"type\": \"header-only\",\n";
    s += "  \"include\": \"include\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"license\": \"MIT\",\n";
    s += "  \"description\": \"A tiny header-only C++ library.\",\n";
    s += "  \"keywords\": [\n";
    s += "    \"cpp\",\n";
    s += "    \"header-only\",\n";
    s += "    \"vix\"\n";
    s += "  ],\n";
    s += "  \"repository\": \"https://github.com/your-username/" + name + "\",\n";
    s += "  \"authors\": [\n";
    s += "    {\n";
    s += "      \"name\": \"Your Name\",\n";
    s += "      \"github\": \"your-username\"\n";
    s += "    }\n";
    s += "  ]\n";
    s += "}\n";

    return s;
  }

  static std::string make_vix_json_app(const std::string &name)
  {
    std::string s;
    s.reserve(4200);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"deps\": [],\n";
    s += "  \"vars\": {\n";
    s += "    \"preset\": \"dev-ninja\",\n";
    s += "    \"release_preset\": \"release\",\n";
    s += "    \"log_level\": \"info\"\n";
    s += "  },\n";
    s += "  \"tasks\": {\n";
    s += "    \"fmt\": \"vix fmt\",\n";
    s += "    \"check\": {\n";
    s += "      \"description\": \"Validate project health\",\n";
    s += "      \"command\": \"vix check --preset ${preset} --tests\",\n";
    s += "      \"env\": {\n";
    s += "        \"VIX_LOG_LEVEL\": \"${log_level}\"\n";
    s += "      }\n";
    s += "    },\n";
    s += "    \"test\": {\n";
    s += "      \"description\": \"Run project tests\",\n";
    s += "      \"command\": \"vix tests --preset ${preset} --fail-fast\"\n";
    s += "    },\n";
    s += "    \"dev\": {\n";
    s += "      \"description\": \"Start dev mode\",\n";
    s += "      \"command\": \"vix dev\"\n";
    s += "    },\n";
    s += "    \"ci\": {\n";
    s += "      \"description\": \"Local CI pipeline\",\n";
    s += "      \"deps\": [\n";
    s += "        \"fmt\"\n";
    s += "      ],\n";
    s += "      \"commands\": [\n";
    s += "        \"vix check --preset ${preset} --tests\",\n";
    s += "        \"vix tests --preset ${preset} --fail-fast\"\n";
    s += "      ]\n";
    s += "    },\n";
    s += "    \"release\": {\n";
    s += "      \"description\": \"Release pipeline\",\n";
    s += "      \"deps\": [\n";
    s += "        \"fmt\",\n";
    s += "        \"test\"\n";
    s += "      ],\n";
    s += "      \"vars\": {\n";
    s += "        \"preset\": \"${release_preset}\"\n";
    s += "      },\n";
    s += "      \"env\": {\n";
    s += "        \"VIX_LOG_LEVEL\": \"warn\"\n";
    s += "      },\n";
    s += "      \"cwd\": \"${project_dir}\",\n";
    s += "      \"commands\": [\n";
    s += "        \"vix build --preset ${preset}\",\n";
    s += "        \"vix check --preset ${preset} --tests\"\n";
    s += "      ],\n";
    s += "      \"linux\": {\n";
    s += "        \"commands\": [\n";
    s += "          \"vix build --preset ${preset}\",\n";
    s += "          \"vix check --preset ${preset} --tests --run\"\n";
    s += "        ]\n";
    s += "      },\n";
    s += "      \"windows\": {\n";
    s += "        \"command\": \"vix build --preset dev-msvc\"\n";
    s += "      },\n";
    s += "      \"macos\": {\n";
    s += "        \"commands\": [\n";
    s += "          \"vix build --preset ${preset}\",\n";
    s += "          \"vix tests --preset ${preset}\"\n";
    s += "        ]\n";
    s += "      }\n";
    s += "    },\n";
    s += "    \"package\": {\n";
    s += "      \"description\": \"Build package artifacts\",\n";
    s += "      \"deps\": [\n";
    s += "        \"release\"\n";
    s += "      ],\n";
    s += "      \"commands\": [\n";
    s += "        \"echo Packaging project from ${project_dir}\",\n";
    s += "        \"vix build --preset ${release_preset}\"\n";
    s += "      ]\n";
    s += "    }\n";
    s += "  }\n";
    s += "}\n";

    return s;
  }

  static std::string make_cmakelists_app(const std::string &projectName, const FeaturesSelection &f)
  {
    std::string s;
    s.reserve(16000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + projectName + " LANGUAGES CXX)\n\n";

    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

    // Options only for selected features
    if (f.orm)
      s += "option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm in install)\" ON)\n";
    if (f.sanitizers)
      s += "option(VIX_ENABLE_SANITIZERS \"Enable ASan/UBSan (dev only)\" ON)\n";
    if (f.static_rt)
      s += "option(VIX_LINK_STATIC \"Static libstdc++/libgcc\" ON)\n";
    if (f.full_static)
      s += "option(VIX_LINK_FULL_STATIC \"Full static link (-static). Prefer musl.\" ON)\n";
    if (f.orm || f.sanitizers || f.static_rt || f.full_static)
      s += "\n";

    s += "# ------------------------------------------------------\n";
    s += "# Core Vix runtime\n";
    s += "# ------------------------------------------------------\n";
    s += "find_package(vix QUIET CONFIG)\n";
    s += "if (NOT vix_FOUND)\n";
    s += "  find_package(Vix CONFIG REQUIRED)\n";
    s += "endif()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Local registry packages installed with: vix install\n";
    s += "# ------------------------------------------------------\n";
    s += "# If you add packages from the Vix registry, they are wired\n";
    s += "# through .vix/vix_deps.cmake. This file creates the imported\n";
    s += "# targets for local project dependencies.\n";
    s += "#\n";
    s += "# Example:\n";
    s += "#   vix add @cnerium/app\n";
    s += "#   vix install\n";
    s += "#\n";
    s += "# Then uncomment the links you need below.\n";
    s += "if (EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/.vix/vix_deps.cmake\")\n";
    s += "  include(\"${CMAKE_CURRENT_SOURCE_DIR}/.vix/vix_deps.cmake\")\n";
    s += "endif()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Helpers\n";
    s += "# ------------------------------------------------------\n";
    s += "function(vix_link_optional_targets tgt)\n";
    s += "  foreach(dep IN LISTS ARGN)\n";
    s += "    if (TARGET ${dep})\n";
    s += "      target_link_libraries(${tgt} PRIVATE ${dep})\n";
    s += "    endif()\n";
    s += "  endforeach()\n";
    s += "endfunction()\n\n";

    if (f.static_rt || f.full_static)
    {
      s += "function(vix_apply_static_link_flags tgt)\n";
      s += "  if (MSVC)\n";
      s += "    return()\n";
      s += "  endif()\n";
      if (f.full_static)
      {
        s += "  if (VIX_LINK_FULL_STATIC)\n";
        s += "    target_link_options(${tgt} PRIVATE -static)\n";
        s += "    target_compile_definitions(${tgt} PRIVATE VIX_LINK_FULL_STATIC=1)\n";
        s += "  endif()\n";
      }
      else
      {
        s += "  if (VIX_LINK_STATIC)\n";
        s += "    target_link_options(${tgt} PRIVATE -static-libstdc++ -static-libgcc)\n";
        s += "    target_compile_definitions(${tgt} PRIVATE VIX_LINK_STATIC=1)\n";
        s += "  endif()\n";
      }
      s += "endfunction()\n\n";
    }

    s += "# ------------------------------------------------------\n";
    s += "# Main executable\n";
    s += "# ------------------------------------------------------\n";
    s += "add_executable(" + projectName + " src/main.cpp)\n";
    s += "target_link_libraries(" + projectName + " PRIVATE vix::vix)\n\n";

    s += "# Add local registry libraries here.\n";
    s += "# Keep them in this block so the build stays stable even if\n";
    s += "# some packages are not installed yet.\n";
    s += "#\n";
    s += "# Example:\n";
    s += "# vix_link_optional_targets(" + projectName + "\n";
    s += "#   cnerium::app\n";
    s += "#   cnerium::http\n";
    s += "#   cnerium::json\n";
    s += "# )\n\n";

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + projectName + " PRIVATE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + projectName + " PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

    if (f.orm)
    {
      s += "if (VIX_USE_ORM)\n";
      s += "  if (TARGET vix::orm)\n";
      s += "    target_link_libraries(" + projectName + " PRIVATE vix::orm)\n";
      s += "    target_compile_definitions(" + projectName + " PRIVATE VIX_USE_ORM=1)\n";
      s += "  else()\n";
      s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but vix::orm target is not available in this Vix install\")\n";
      s += "  endif()\n";
      s += "endif()\n\n";
    }

    if (f.static_rt || f.full_static)
      s += "vix_apply_static_link_flags(" + projectName + ")\n\n";

    if (f.sanitizers)
    {
      s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
      s += "  target_compile_options(" + projectName + " PRIVATE -g3 -fno-omit-frame-pointer -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined)\n";
      s += "  target_link_options(" + projectName + " PRIVATE -g -fsanitize=address,undefined)\n";
      s += "  target_compile_definitions(" + projectName + " PRIVATE VIX_SANITIZERS=1 VIX_ASAN=1 VIX_UBSAN=1)\n";
      s += "endif()\n\n";
    }

    s += "# ------------------------------------------------------\n";
    s += "# Tests\n";
    s += "# ------------------------------------------------------\n";
    s += "include(CTest)\n";
    s += "enable_testing()\n\n";

    s += "add_executable(" + projectName + "_basic_test tests/test_basic.cpp)\n";
    s += "target_link_libraries(" + projectName + "_basic_test PRIVATE vix::vix)\n\n";

    s += "# Add the same local registry libraries to tests when needed.\n";
    s += "# Example:\n";
    s += "# vix_link_optional_targets(" + projectName + "_basic_test\n";
    s += "#   cnerium::app\n";
    s += "#   cnerium::http\n";
    s += "#   cnerium::json\n";
    s += "# )\n\n";

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + projectName + "_basic_test PRIVATE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + projectName + "_basic_test PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

    if (f.static_rt || f.full_static)
      s += "vix_apply_static_link_flags(" + projectName + "_basic_test)\n\n";

    s += "add_test(NAME " + projectName + ".basic COMMAND " + projectName + "_basic_test)\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Convenience target\n";
    s += "# ------------------------------------------------------\n";
    s += "add_custom_target(run\n";
    s += "  COMMAND $<TARGET_FILE:" + projectName + ">\n";
    s += "  DEPENDS " + projectName + "\n";
    s += "  USES_TERMINAL\n";
    s += ")\n";

    return s;
  }

  static std::string make_cmakelists_lib(const std::string &name)
  {
    std::string s;
    s.reserve(16000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + name + " LANGUAGES CXX)\n\n";

    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Options\n";
    s += "# ------------------------------------------------------\n";
    s += "option(" + name + "_BUILD_TESTS \"Build tests\" OFF)\n";
    s += "option(" + name + "_BUILD_EXAMPLES \"Build examples\" OFF)\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Library\n";
    s += "# ------------------------------------------------------\n";
    s += "add_library(" + name + " INTERFACE)\n";
    s += "add_library(" + name + "::" + name + " ALIAS " + name + ")\n\n";

    s += "target_include_directories(" + name + " INTERFACE\n";
    s += "  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>\n";
    s += "  $<INSTALL_INTERFACE:include>\n";
    s += ")\n\n";

    s += "if (MSVC)\n";
    s += "  target_compile_options(" + name + " INTERFACE /W4 /permissive-)\n";
    s += "else()\n";
    s += "  target_compile_options(" + name + " INTERFACE -Wall -Wextra -Wpedantic)\n";
    s += "endif()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Helpers\n";
    s += "# ------------------------------------------------------\n";
    s += "function(" + name + "_add_example target file)\n";
    s += "  add_executable(${target} ${file})\n";
    s += "  target_link_libraries(${target} PRIVATE " + name + "::" + name + ")\n\n";
    s += "  if (MSVC)\n";
    s += "    target_compile_options(${target} PRIVATE /W4 /permissive-)\n";
    s += "  else()\n";
    s += "    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "  endif()\n";
    s += "endfunction()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Examples\n";
    s += "# ------------------------------------------------------\n";
    s += "if (" + name + "_BUILD_EXAMPLES)\n";
    s += "  if (EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt\")\n";
    s += "    add_subdirectory(examples)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    s += "# ------------------------------------------------------\n";
    s += "# Tests\n";
    s += "# ------------------------------------------------------\n";
    s += "if (" + name + "_BUILD_TESTS)\n";
    s += "  include(CTest)\n";
    s += "  enable_testing()\n\n";

    s += "  add_executable(" + name + "_test_basic tests/test_basic.cpp)\n";
    s += "  target_link_libraries(" + name + "_test_basic PRIVATE " + name + "::" + name + ")\n\n";

    s += "  if (MSVC)\n";
    s += "    target_compile_options(" + name + "_test_basic PRIVATE /W4 /permissive-)\n";
    s += "  else()\n";
    s += "    target_compile_options(" + name + "_test_basic PRIVATE -Wall -Wextra -Wpedantic)\n";
    s += "  endif()\n\n";

    s += "  add_test(NAME " + name + ".basic COMMAND " + name + "_test_basic)\n";
    s += "endif()\n";

    return s;
  }

  // ==========================================================
  // Flow steps
  // ==========================================================

  static SelectResult<TemplateKind> choose_template_interactive()
  {
    const std::vector<std::string> items = {
        "Application",
        "Library (header-only)"};

    const std::vector<TemplateKind> values = {
        TemplateKind::App,
        TemplateKind::Lib};

    return menu_select<TemplateKind>("Template", items, 0, values);
  }

  static SelectResult<OverwriteChoice> confirm_overwrite_interactive(const fs::path &dir)
  {
    std::cout << PAD << GRAY << dir.string() << RESET << "\n";

    const std::vector<std::string> items = {
        "Overwrite (delete and recreate)",
        "Cancel"};

    const std::vector<OverwriteChoice> values = {
        OverwriteChoice::Overwrite,
        OverwriteChoice::Cancel};

    return menu_select<OverwriteChoice>("Directory exists", items, 1, values);
  }

  static FeaturesSelection choose_features_interactive(bool &cancelled)
  {
    cancelled = false;

    MultiSelectResult r = menu_multiselect_features();
    if (r.cancelled)
    {
      cancelled = true;
      return FeaturesSelection{};
    }

    FeaturesSelection f{};
    f.orm = (r.selected.size() > 0) ? (bool)r.selected[0] : false;
    f.sanitizers = (r.selected.size() > 1) ? (bool)r.selected[1] : false;
    f.static_rt = (r.selected.size() > 2) ? (bool)r.selected[2] : false;
    f.full_static = (r.selected.size() > 3) ? (bool)r.selected[3] : false;

    return f;
  }

  // Generation routines
  static bool generate_app_project(
      const fs::path &projectDir,
      const std::string &projName,
      const FeaturesSelection &features,
      std::string &err)
  {
    const fs::path srcDir = projectDir / "src";
    const fs::path testsDir = projectDir / "tests";
    static const char *kEnvExample = R"(# ----------------------------------
    # Server
    # ----------------------------------
    SERVER_PORT=8080

    # ----------------------------------
    # Database
    # ----------------------------------
    DATABASE_ENGINE=mysql
    DATABASE_DEFAULT_HOST=127.0.0.1
    DATABASE_DEFAULT_PORT=3306
    DATABASE_DEFAULT_USER=root
    DATABASE_DEFAULT_PASSWORD=
    DATABASE_DEFAULT_NAME=appdb

    # ----------------------------------
    # Logging
    # ----------------------------------
    LOGGING_ASYNC=true

    # ----------------------------------
    # Security / WAF
    # ----------------------------------
    WAF_MODE=basic
    )";

    if (!ensure_dir(srcDir, err))
      return false;
    if (!ensure_dir(testsDir, err))
      return false;

    if (!write_text_file(srcDir / "main.cpp", kMainCpp, err))
      return false;
    if (!write_text_file(testsDir / "test_basic.cpp", kBasicTestCpp_App, err))
      return false;

    if (!write_text_file(projectDir / ".env.example", kEnvExample, err))
      return false;

    if (!write_text_file(projectDir / ".env", kEnvExample, err))
      return false;

    if (!write_text_file(projectDir / "CMakeLists.txt", make_cmakelists_app(projName, features), err))
      return false;
    if (!write_text_file(projectDir / "README.md", make_readme_app(projName), err))
      return false;
    if (!write_text_file(projectDir / "CMakePresets.json", make_cmake_presets_json_app(features), err))
      return false;
    if (!write_text_file(projectDir / "vix.json", make_vix_json_app(projName), err))
      return false;
    if (!write_text_file(projectDir / (projName + ".vix"), make_project_manifest_app(projName, features), err))
      return false;

    return true;
  }
  static bool generate_lib_project(
      const fs::path &projectDir,
      const std::string &projName,
      std::string &err)
  {
    const fs::path includeDir = projectDir / "include" / projName;
    const fs::path testsDir = projectDir / "tests";
    const fs::path examplesDir = projectDir / "examples";

    if (!ensure_dir(includeDir, err))
      return false;
    if (!ensure_dir(testsDir, err))
      return false;
    if (!ensure_dir(examplesDir, err))
      return false;

    if (!write_text_file(includeDir / (projName + ".hpp"), make_lib_header(projName), err))
      return false;

    if (!write_text_file(testsDir / "test_basic.cpp", make_basic_test_cpp_lib(projName), err))
      return false;

    if (!write_text_file(examplesDir / "basic.cpp", make_basic_example_cpp_lib(projName), err))
      return false;

    if (!write_text_file(examplesDir / "CMakeLists.txt", make_examples_cmakelists_lib(projName), err))
      return false;

    if (!write_text_file(projectDir / "CMakeLists.txt", make_cmakelists_lib(projName), err))
      return false;
    if (!write_text_file(projectDir / "README.md", make_readme_lib(projName), err))
      return false;
    if (!write_text_file(projectDir / "CMakePresets.json", make_cmake_presets_json_lib(), err))
      return false;
    if (!write_text_file(projectDir / "vix.json", make_vix_json_lib(projName), err))
      return false;
    if (!write_text_file(projectDir / (projName + ".vix"), make_project_manifest_lib(projName), err))
      return false;

    return true;
  }

  static void print_next_steps_app(const fs::path &, const std::string &projName)
  {
    namespace ui = vix::cli::util;

    const std::string manifest = projName + ".vix";

    std::cout << "\n";
    ui::info_line(std::cout, "Next steps");
    std::cout << "    " << ui::dim("cd " + projName + "/") << "\n";
    std::cout << "    " << ui::dim("vix build") << "\n";
    std::cout << "    " << ui::dim("vix run") << "\n";
    std::cout << "\n";
    std::cout << "    " << ui::dim("vix task dev") << "\n";
    std::cout << "    " << ui::dim("vix dev " + manifest) << "\n";
  }

  static void print_next_steps_lib(const fs::path &, const std::string &projName)
  {
    namespace ui = vix::cli::util;

    std::cout << "\n";
    ui::info_line(std::cout, "Next steps");
    std::cout << "    " << ui::dim("cd " + projName + "/") << "\n";
    std::cout << "    " << ui::dim("vix build") << "\n";
    std::cout << "    " << ui::dim("vix tests") << "\n";
    std::cout << "\n";
    std::cout << "    " << ui::dim("TIP: tag + publish when ready") << "\n";
  }
} // namespace

namespace vix::commands::NewCommand
{
  int run(const std::vector<std::string> &argsIn)
  {
    // Return codes:
    // 0 success
    // 2 user cancelled
    // 1+ errors
    if (argsIn.empty())
    {
      error("Missing project name.");
      hint("Usage: vix new <name|path> [-d|--dir <base_dir>] [--app|--lib] [--force]");
      return 1;
    }

    std::vector<std::string> args = argsIn;

    const bool force = consume_flag(args, "--force");
    const std::optional<std::string> baseOpt = take_option_value(args, {"-d", "--dir"});

    const bool wantsLib = has_any(args, {"--lib", "--library", "--type=lib", "--type=library"});
    const bool wantsApp = has_any(args, {"--app", "--application", "--type=app", "--type=application"});

    consume_flag(args, "--lib");
    consume_flag(args, "--library");
    consume_flag(args, "--type=lib");
    consume_flag(args, "--type=library");

    consume_flag(args, "--app");
    consume_flag(args, "--application");
    consume_flag(args, "--type=app");
    consume_flag(args, "--type=application");

    if (wantsLib && wantsApp)
    {
      error("Conflicting options: choose either --app or --lib.");
      return 1;
    }

    const std::string nameOrPath = args[0];
    const bool inPlace = is_dot_path(nameOrPath);

    try
    {
      // Allow Ctrl+C to cancel even outside interactive menus.
      SignalGuard sig;

      if (g_cancelled.load())
        return 2;

      fs::path np = fs::path(nameOrPath);

      // Step 0: resolve destination path FIRST
      fs::path dest;

      if (baseOpt.has_value())
      {
        fs::path base = fs::path(*baseOpt);
        std::error_code ec;

        if (!fs::exists(base, ec) || !fs::is_directory(base, ec))
        {
          error("Base directory '" + base.string() + "' is not a valid folder.");
          hint("Make sure it exists and is a directory, or omit --dir to use the current directory.");
          return 1;
        }

        if (np.is_absolute())
          dest = np;
        else
        {
          dest = fs::weakly_canonical(base / np, ec);
          if (ec)
            dest = base / np;
        }
      }
      else
      {
        std::error_code ec;
        if (np.is_absolute())
          dest = np;
        else
        {
          dest = fs::weakly_canonical(fs::current_path() / np, ec);
          if (ec)
            dest = fs::current_path() / np;
        }
      }

      fs::path projectDir = dest;
      std::string projName = projectDir.filename().string();

      if (inPlace)
      {
        projectDir = fs::current_path();
        projName = current_dir_name();
      }

      if (g_cancelled.load())
        return 2;

      // Step 1: existing dir behavior (overwrite BEFORE template)
      if (fs::exists(projectDir))
      {
        if (!dir_exists(projectDir))
        {
          error("Path exists but is not a directory: '" + projectDir.string() + "'");
          return 1;
        }

        if (!dir_is_empty(projectDir))
        {
          // Special case: vix new . must never delete the current directory
          if (inPlace)
          {
            if (force)
            {
              // Proceed: overwrite template files, but do not delete the folder
            }
            else if (can_interact())
            {
              const auto choice = confirm_inplace_dir_interactive(projectDir);
              if (choice.cancelled || choice.value == InPlaceDirChoice::Cancel)
                return 2;
            }
            else
            {
              error("Cannot create project in current directory: directory is not empty.");
              hint("Use --force to overwrite template files.");
              return 1;
            }
          }
          else
          {
            if (force)
            {
              std::error_code ec;
              fs::remove_all(projectDir, ec);
              if (ec)
              {
                error("Failed to remove existing directory.");
                hint(ec.message());
                return 1;
              }
            }
            else if (can_interact())
            {
              const auto choice = confirm_overwrite_interactive(projectDir);
              if (choice.cancelled || choice.value == OverwriteChoice::Cancel)
                return 2;

              std::error_code ec;
              fs::remove_all(projectDir, ec);
              if (ec)
              {
                error("Failed to remove existing directory.");
                hint(ec.message());
                return 1;
              }
            }
            else
            {
              error("Cannot create project in '" + projectDir.string() + "': directory is not empty.");
              hint("Use --force to overwrite.");
              return 1;
            }
          }

          if (g_cancelled.load())
            return 2;
        }
      }

      // Step 2: choose template (AFTER overwrite decision)
      TemplateKind kind = TemplateKind::App;

      if (wantsLib)
        kind = TemplateKind::Lib;
      else if (wantsApp)
        kind = TemplateKind::App;
      else
      {
        if (can_interact())
        {
          const auto sel = choose_template_interactive();
          if (sel.cancelled)
            return 2;
          kind = sel.value;
        }
        else
        {
          kind = TemplateKind::App;
        }
      }

      if (g_cancelled.load())
        return 2;

      // Step 3: features (only after overwrite decision + template)
      FeaturesSelection features{};
      if (kind == TemplateKind::App && can_interact())
      {
        bool cancelled = false;
        features = choose_features_interactive(cancelled);
        if (cancelled)
          return 2;
      }

      if (features.full_static)
        features.static_rt = true;

      if (g_cancelled.load())
        return 2;

      // Step 4: create project root (now safe)
      {
        std::string err;
        if (!ensure_dir(projectDir, err))
        {
          error("Failed to create project directory.");
          hint(err);
          return 1;
        }
      }

      if (g_cancelled.load())
        return 2;

      // Step 5: generate
      std::string genErr;

      if (kind == TemplateKind::App)
      {
        if (!generate_app_project(projectDir, projName, features, genErr))
        {
          vix::cli::util::err_line(std::cerr, "Failed to create project files.");
          vix::cli::util::warn_line(std::cerr, genErr);
          return 1;
        }

        vix::cli::util::ok_line(std::cout, "Project created.");
        vix::cli::util::kv(std::cout, "Location", projectDir.string());
        print_next_steps_app(projectDir, projName);
        return 0;
      }

      if (!generate_lib_project(projectDir, projName, genErr))
      {
        vix::cli::util::err_line(std::cerr, "Failed to create project files.");
        vix::cli::util::warn_line(std::cerr, genErr);
        return 1;
      }

      vix::cli::util::ok_line(std::cout, "Project created.");
      vix::cli::util::kv(std::cout, "Location", projectDir.string());
      print_next_steps_lib(projectDir, projName);
      return 0;
      return 0;
    }
    catch (const std::exception &ex)
    {
      error("Failed to create project.");
      hint(ex.what());
      return 1;
    }
  }

  int help()
  {
    std::ostream &out = std::cout;

    out
        << "vix new\n"
        << "Create a new Vix project.\n\n"

        << "Usage\n"
        << "  vix new <name|path> [options]\n\n"

        << "Examples\n"
        << "  vix new api\n"
        << "  vix new .\n"
        << "  vix new tree --lib\n"
        << "  vix new blog -d ./projects\n"
        << "  vix new api --force\n\n"

        << "What happens\n"
        << "  • Generates a ready-to-run Vix project\n"
        << "  • Sets up CMake, source structure, and config files\n"
        << "  • Creates a vix.json manifest\n"
        << "  • For apps, includes default tasks and an empty deps list\n"
        << "  • For libraries, includes package metadata and an empty deps list\n"
        << "  • Applies the selected template (app or library)\n\n"

        << "Options\n"
        << "  --app       Generate an application (default)\n"
        << "  --lib       Generate a header-only library\n"
        << "  -d, --dir   Base directory for project creation\n"
        << "  --force     Overwrite existing directory\n\n"

        << "Environment\n"
        << "  VIX_NONINTERACTIVE=1   Disable prompts\n"
        << "  CI=1                   Disable prompts\n\n"

        << "Notes\n"
        << "  • Use '.' to initialize in the current directory\n"
        << "  • Use 'vix add <pkg>' to add dependencies\n"
        << "  • Use 'vix install' to install from vix.lock\n"
        << "  • Use 'vix task <name>' to run generated tasks\n"
        << "  • Designed for fast start with zero setup\n";

    return 0;
  }

} // namespace vix::commands::NewCommand
