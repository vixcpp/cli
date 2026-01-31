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
#include <vix/cli/commands/NewCommand.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/Utils.hpp>

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

namespace
{
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

  constexpr const char *kBasicTestCpp_App = R"(#include <iostream>

int main()
{
  std::cout << "basic test OK\n";
  return 0;
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
    s.reserve(800);

    s += "#include <" + name + "/" + name + ".hpp>\n";
    s += "#include <iostream>\n\n";
    s += "int main()\n";
    s += "{\n";
    s += "  auto nodes = " + name + "::make_chain(5);\n";
    s += "  std::cout << \"nodes=\" << nodes.size() << \"\\n\";\n";
    s += "  return nodes.size() == 5 ? 0 : 1;\n";
    s += "}\n";

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
    if (const char *v = std::getenv("VIX_NONINTERACTIVE"))
    {
      const std::string s = v;
      if (!s.empty() && s != "0" && s != "false" && s != "FALSE")
        return true;
    }
    if (std::getenv("CI") != nullptr)
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
    // ... (ton code Windows inchangé)
#else
    while (true)
    {
      if (g_cancelled.load())
        return Key::CtrlC;

      pollfd pfd{};
      pfd.fd = STDIN_FILENO;
      pfd.events = POLLIN;

      // timeout 50ms to stay responsive to Ctrl+C
      const int r = ::poll(&pfd, 1, 50);
      if (r < 0)
      {
        // EINTR: interrupted by signal -> treat as cancel if SIGINT
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

      if (c == '\n' || c == '\r')
        return Key::Enter;

      if (c == 27)
      {
        unsigned char s1 = 0;
        unsigned char s2 = 0;

        // If escape alone -> return Escape quickly (do NOT block)
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

  // ---------- Multi select (Features) ----------
  struct MultiSelectResult
  {
    bool cancelled{false};
    std::vector<bool> selected{};
  };

  static bool all_checked(const std::vector<bool> &v)
  {
    if (v.empty())
      return false;
    for (bool b : v)
      if (!b)
        return false;
    return true;
  }

  static void toggle_all(std::vector<bool> &v)
  {
    const bool turnOn = !all_checked(v);
    for (std::size_t i = 0; i < v.size(); ++i)
      v[i] = turnOn;
  }

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

  static std::string gray_tip(const std::string &s)
  {
    return std::string(GRAY) + s + RESET;
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

    auto build_lines = [&](int activeIdx) -> std::vector<std::string>
    {
      std::vector<std::string> out;
      out.reserve(1 + 3 + 1 + 1 + 1 + 1);

      out.push_back(std::string(PAD) + BOLD + CYAN + "Features" + RESET);

      for (int i = 0; i < 3; ++i)
      {
        const bool active = (i == activeIdx);
        const bool on = (bool)res.selected[(std::size_t)items[(std::size_t)i].index];

        std::string line = PAD;
        line += "  ";
        line += active ? std::string(CYAN) + BOLD + "❯ " + RESET : "  ";
        line += on ? "☑ " : "☐ ";
        line += active ? std::string(CYAN) + BOLD + items[(std::size_t)i].label + RESET : items[(std::size_t)i].label;

        out.push_back(line);
      }

      out.push_back(std::string(PAD));

      out.push_back(std::string(PAD) + BOLD + CYAN + "Advanced" + RESET);

      {
        const int advPos = 3;
        const bool active = (advPos == activeIdx);
        const bool on = (bool)res.selected[items[3].index];

        std::string line = PAD;
        line += "  ";
        line += active ? std::string(CYAN) + BOLD + "❯ " + RESET : "  ";
        line += on ? "☑ " : "☐ ";
        line += active ? std::string(CYAN) + BOLD + items[3].label + RESET : items[3].label;

        out.push_back(line);
      }

      out.push_back(std::string(PAD) + gray_tip(items[(std::size_t)activeIdx].tip));

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
        cursorIndex -= 1;
        if (cursorIndex < 0)
          cursorIndex = 3;
        render_lines(build_lines(cursorIndex), firstDraw);
        continue;
      }

      if (k == Key::Down)
      {
        cursorIndex = (cursorIndex + 1) % 4;
        render_lines(build_lines(cursorIndex), firstDraw);
        continue;
      }

      if (k == Key::Space)
      {
        const std::size_t idx = items[(std::size_t)cursorIndex].index;
        res.selected[idx] = !(bool)res.selected[idx];
        render_lines(build_lines(cursorIndex), firstDraw);
        continue;
      }

      if (k == Key::ToggleAll)
      {
        toggle_all(res.selected);
        render_lines(build_lines(cursorIndex), firstDraw);
        continue;
      }

      if (k == Key::Enter)
      {
        std::cout << "\n";
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

  // Project generation (strict: no unselected feature appears)
  static std::string make_readme_app(const std::string &projectName)
  {
    std::string readme;
    readme.reserve(3000);

    readme += "# " + projectName + "\n\n";
    readme += "Minimal Vix.cpp application.\n\n";
    readme += "```bash\n";
    readme += "vix build\n";
    readme += "vix run\n";
    readme += "```\n";

    return readme;
  }

  static std::string make_readme_lib(const std::string &name)
  {
    std::string readme;
    readme.reserve(4000);

    readme += "# " + name + "\n\n";
    readme += "Header-only C++ library scaffold.\n\n";
    readme += "```bash\n";
    readme += "vix tests\n";
    readme += "```\n";

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
    s.reserve(900);

    s += "{\n";
    s += "  \"name\": \"" + name + "\",\n";
    s += "  \"namespace\": \"<your-namespace>\",\n";
    s += "  \"version\": \"0.1.0\",\n";
    s += "  \"license\": \"MIT\",\n";
    s += "  \"description\": \"A C++ library.\",\n";
    s += "  \"repo\": \"<git-url>\",\n";
    s += "  \"type\": \"library\",\n";
    s += "  \"build\": { \"system\": \"cmake\", \"preset\": \"dev-ninja\" }\n";
    s += "}\n";

    return s;
  }

  static std::string make_cmakelists_app(const std::string &projectName, const FeaturesSelection &f)
  {
    std::string s;
    s.reserve(12000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + projectName + " LANGUAGES CXX)\n\n";
    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

    // Options only for selected features (strict)
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

    s += "find_package(vix QUIET CONFIG)\n";
    s += "if (NOT vix_FOUND)\n";
    s += "  find_package(Vix CONFIG REQUIRED)\n";
    s += "endif()\n\n";

    s += "add_executable(" + projectName + " src/main.cpp)\n";
    s += "target_link_libraries(" + projectName + " PRIVATE vix::vix)\n\n";

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
      s += "vix_apply_static_link_flags(" + projectName + ")\n\n";
    }

    if (f.sanitizers)
    {
      s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
      s += "  target_compile_options(" + projectName + " PRIVATE -g3 -fno-omit-frame-pointer -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined)\n";
      s += "  target_link_options(" + projectName + " PRIVATE -g -fsanitize=address,undefined)\n";
      s += "  target_compile_definitions(" + projectName + " PRIVATE VIX_SANITIZERS=1 VIX_ASAN=1 VIX_UBSAN=1)\n";
      s += "endif()\n\n";
    }

    s += "include(CTest)\n";
    s += "enable_testing()\n\n";
    s += "add_executable(" + projectName + "_basic_test tests/test_basic.cpp)\n";
    s += "target_link_libraries(" + projectName + "_basic_test PRIVATE vix::vix)\n";
    if (f.static_rt || f.full_static)
      s += "vix_apply_static_link_flags(" + projectName + "_basic_test)\n";
    s += "add_test(NAME " + projectName + ".basic COMMAND " + projectName + "_basic_test)\n\n";

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
    s.reserve(9000);

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + name + " LANGUAGES CXX)\n\n";
    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

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

    s += "include(CTest)\n";
    s += "enable_testing()\n\n";

    s += "add_executable(" + name + "_basic_test tests/test_basic.cpp)\n";
    s += "target_link_libraries(" + name + "_basic_test PRIVATE " + name + "::" + name + ")\n";
    s += "add_test(NAME " + name + ".basic COMMAND " + name + "_basic_test)\n";

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
    const fs::path configDir = projectDir / "config";

    if (!ensure_dir(srcDir, err))
      return false;
    if (!ensure_dir(testsDir, err))
      return false;
    if (!ensure_dir(configDir, err))
      return false;

    if (!write_text_file(srcDir / "main.cpp", kMainCpp, err))
      return false;
    if (!write_text_file(testsDir / "test_basic.cpp", kBasicTestCpp_App, err))
      return false;

    if (!write_text_file(configDir / "config.json", kAppConfigJson, err))
      return false;

    if (!write_text_file(projectDir / "CMakeLists.txt", make_cmakelists_app(projName, features), err))
      return false;
    if (!write_text_file(projectDir / "README.md", make_readme_app(projName), err))
      return false;
    if (!write_text_file(projectDir / "CMakePresets.json", make_cmake_presets_json_app(features), err))
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

    if (!ensure_dir(includeDir, err))
      return false;
    if (!ensure_dir(testsDir, err))
      return false;

    if (!write_text_file(includeDir / (projName + ".hpp"), make_lib_header(projName), err))
      return false;
    if (!write_text_file(testsDir / "test_basic.cpp", make_basic_test_cpp_lib(projName), err))
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
    const std::string manifest = projName + ".vix";

    std::cout << "\n";
    info("Next steps:");
    step("cd " + projName + "/");
    step("vix build");
    step("vix run");
    std::cout << "\n";
    hint("Tip: vix dev");
    hint("Tip: vix dev " + manifest);
  }

  static void print_next_steps_lib(const fs::path &, const std::string &projName)
  {
    std::cout << "\n";
    info("Next steps:");
    step("cd " + projName + "/");
    step("vix tests");
    std::cout << "\n";
    hint("Tip: tag + publish when ready");
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
      const std::string projName = projectDir.filename().string();

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
          error("Failed to create project files.");
          hint(genErr);
          return 1;
        }

        success("Project created.");
        info("Location: " + projectDir.string());
        print_next_steps_app(projectDir, projName);
        return 0;
      }

      if (!generate_lib_project(projectDir, projName, genErr))
      {
        error("Failed to create project files.");
        hint(genErr);
        return 1;
      }

      success("Project created.");
      info("Location: " + projectDir.string());
      print_next_steps_lib(projectDir, projName);
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

    out << "Usage:\n";
    out << "  vix new <name|path> [options]\n\n";

    out << "Options:\n";
    out << "  -d, --dir <base_dir>    Base directory where the project folder will be created\n";
    out << "  --app                   Generate an application template\n";
    out << "  --lib                   Generate a library template (header-only)\n";
    out << "  --force                 Overwrite if directory exists (no prompt)\n\n";

    out << "Environment:\n";
    out << "  VIX_NONINTERACTIVE=1    Disable interactive prompts\n";
    out << "  CI=1                    Disable interactive prompts\n\n";

    out << "Examples:\n";
    out << "  vix new api\n";
    out << "  vix new tree --lib\n";
    out << "  vix new blog -d ./projects\n";
    out << "  vix new api --force\n";

    return 0;
  }

} // namespace vix::commands::NewCommand
