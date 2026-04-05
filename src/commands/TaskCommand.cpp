/**
 *
 *  @file TaskCommand.cpp
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
#include <vix/cli/commands/TaskCommand.hpp>
#include <vix/cli/commands/DevCommand.hpp>
#include <vix/cli/commands/FmtCommand.hpp>
#include <vix/cli/commands/RunCommand.hpp>
#include <vix/cli/commands/CheckCommand.hpp>
#include <vix/cli/commands/TestsCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <stdlib.h>
#else
#include <poll.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace vix::cli::style;
namespace fs = std::filesystem;
namespace ui = vix::cli::util;

namespace
{
  enum class TaskKind
  {
    Dev,
    Run,
    Fmt,
    Check,
    Build,
    Test
  };

  struct TaskSpec
  {
    const char *name;
    const char *description;
    TaskKind kind;
  };

  constexpr TaskSpec kBuiltins[] = {
      {"dev", "Run in dev mode with hot reload", TaskKind::Dev},
      {"run", "Run the application", TaskKind::Run},
      {"fmt", "Format code with clang-format", TaskKind::Fmt},
      {"check", "Validate build, tests, runtime and sanitizers", TaskKind::Check},
      {"build", "Build the current project", TaskKind::Build},
      {"test", "Run tests with CTest", TaskKind::Test},
  };

  enum class Key
  {
    None,
    Up,
    Down,
    Enter,
    Escape,
    CtrlC,
    Quit
  };

  struct TaskDefinition
  {
    std::string name;
    std::string description;
    std::vector<std::string> deps;
    std::vector<std::string> commands;
    std::map<std::string, std::string> env;
    std::map<std::string, std::string> vars;
    std::string cwd;
  };

  struct TaskRegistry
  {
    std::map<std::string, std::string> globalVars;
    std::map<std::string, TaskDefinition> customTasks;
    std::optional<fs::path> manifestPath;
  };

  struct MenuEntry
  {
    std::string name;
    std::string description;
    bool isCustom{false};
    TaskKind builtin{};
  };

  struct ExecuteContext
  {
    const TaskRegistry &registry;
    std::set<std::string> done;
    std::set<std::string> visiting;
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

  static std::string current_platform()
  {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
  }

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

  struct ScopedCwd
  {
    fs::path old;
    bool changed{false};

    explicit ScopedCwd(const std::string &path)
    {
      if (path.empty())
        return;

      std::error_code ec;
      old = fs::current_path(ec);
      if (ec)
        return;

      fs::current_path(fs::path(path), ec);
      changed = !ec;
    }

    ~ScopedCwd()
    {
      if (!changed)
        return;

      std::error_code ec;
      fs::current_path(old, ec);
    }
  };

  struct ScopedEnv
  {
    std::map<std::string, std::optional<std::string>> previous;
    bool active{false};

    explicit ScopedEnv(const std::map<std::string, std::string> &vars)
    {
      if (vars.empty())
        return;

      active = true;

      for (const auto &kv : vars)
      {
        const char *old = vix::utils::vix_getenv(kv.first.c_str());
        if (old)
          previous[kv.first] = std::string(old);
        else
          previous[kv.first] = std::nullopt;

#if defined(_WIN32)
        _putenv_s(kv.first.c_str(), kv.second.c_str());
#else
        ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
#endif
      }
    }

    ~ScopedEnv()
    {
      if (!active)
        return;

      for (const auto &kv : previous)
      {
        if (kv.second.has_value())
        {
#if defined(_WIN32)
          _putenv_s(kv.first.c_str(), kv.second->c_str());
#else
          ::setenv(kv.first.c_str(), kv.second->c_str(), 1);
#endif
        }
        else
        {
#if defined(_WIN32)
          _putenv_s(kv.first.c_str(), "");
#else
          ::unsetenv(kv.first.c_str());
#endif
        }
      }
    }
  };

  static int normalize_exit_code_local(int raw)
  {
#if defined(_WIN32)
    return raw;
#else
    if (raw == -1)
      return 1;
    if (WIFEXITED(raw))
      return WEXITSTATUS(raw);
    if (WIFSIGNALED(raw))
      return 128 + WTERMSIG(raw);
    return raw;
#endif
  }

  static std::string shell_quote(const std::string &s)
  {
#if defined(_WIN32)
    std::string out = "\"";
    for (char c : s)
    {
      if (c == '"')
        out += "\\\"";
      else
        out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : s)
    {
      if (c == '\'')
        out += "'\\''";
      else
        out += c;
    }
    out += "'";
    return out;
#endif
  }

  static std::string append_args_to_command(
      const std::string &command,
      const std::vector<std::string> &args)
  {
    if (args.empty())
      return command;

    std::string out = command;
    for (const auto &arg : args)
    {
      out += " ";
      out += shell_quote(arg);
    }
    return out;
  }

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
    while (true)
    {
      if (g_cancelled.load())
        return Key::CtrlC;

      const int ch = ::_getch();

      if (ch == 3)
        return Key::CtrlC;
      if (ch == 13)
        return Key::Enter;
      if (ch == 27)
        return Key::Escape;
      if (ch == 'q' || ch == 'Q')
        return Key::Quit;
      if (ch == 'k' || ch == 'K')
        return Key::Up;
      if (ch == 'j' || ch == 'J')
        return Key::Down;

      if (ch == 0 || ch == 224)
      {
        const int ext = ::_getch();
        if (ext == 72)
          return Key::Up;
        if (ext == 80)
          return Key::Down;
        continue;
      }

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
      const ssize_t n = ::read(STDIN_FILENO, &c, 1);
      if (n != 1)
        continue;

      if (c == 3)
        return Key::CtrlC;
      if (c == '\n' || c == '\r')
        return Key::Enter;
      if (c == 'q' || c == 'Q')
        return Key::Quit;
      if (c == 'k' || c == 'K')
        return Key::Up;
      if (c == 'j' || c == 'J')
        return Key::Down;

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

      return Key::None;
    }
#endif
  }

  static void print_cancel_line()
  {
    std::cout << PAD << RED << "✖" << RESET << " Cancelled\n";
  }

  template <typename T>
  struct SelectResult
  {
    bool cancelled{false};
    T value{};
  };

  static bool is_help_flag(const std::string &arg)
  {
    return arg == "-h" || arg == "--help";
  }

  static bool is_list_flag(const std::string &arg)
  {
    return arg == "--list" || arg == "-l";
  }

  static std::vector<std::string> tail_args(const std::vector<std::string> &args)
  {
    if (args.size() <= 1)
      return {};

    return std::vector<std::string>(args.begin() + 1, args.end());
  }

  static std::optional<fs::path> find_vix_json()
  {
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    if (ec)
      return std::nullopt;

    for (;;)
    {
      const fs::path candidate = cur / "vix.json";
      if (fs::exists(candidate, ec) && !ec)
        return candidate;

      const fs::path parent = cur.parent_path();
      if (parent.empty() || parent == cur)
        break;

      cur = parent;
    }

    return std::nullopt;
  }

  static std::map<std::string, std::string> read_string_map(
      const nlohmann::json &obj,
      const char *key)
  {
    std::map<std::string, std::string> out;

    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_object())
      return out;

    for (auto it = obj[key].begin(); it != obj[key].end(); ++it)
    {
      if (it.value().is_string())
        out[it.key()] = it.value().get<std::string>();
    }

    return out;
  }

  static std::vector<std::string> read_string_array(
      const nlohmann::json &obj,
      const char *key)
  {
    std::vector<std::string> out;

    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_array())
      return out;

    for (const auto &item : obj[key])
    {
      if (item.is_string())
        out.push_back(item.get<std::string>());
    }

    return out;
  }

  static bool parse_commands_node(
      const nlohmann::json &node,
      std::vector<std::string> &out)
  {
    out.clear();

    if (node.is_string())
    {
      out.push_back(node.get<std::string>());
      return true;
    }

    if (node.is_array())
    {
      for (const auto &item : node)
      {
        if (!item.is_string())
          return false;
        out.push_back(item.get<std::string>());
      }
      return true;
    }

    return false;
  }

  static TaskDefinition parse_task_object_fields(
      const std::string &name,
      const nlohmann::json &obj)
  {
    TaskDefinition def;
    def.name = name;

    if (!obj.is_object())
      return def;

    if (obj.contains("description") && obj["description"].is_string())
      def.description = obj["description"].get<std::string>();

    def.deps = read_string_array(obj, "deps");
    def.env = read_string_map(obj, "env");
    def.vars = read_string_map(obj, "vars");

    if (obj.contains("cwd") && obj["cwd"].is_string())
      def.cwd = obj["cwd"].get<std::string>();

    if (obj.contains("command"))
    {
      std::vector<std::string> tmp;
      if (parse_commands_node(obj["command"], tmp))
        def.commands = tmp;
    }

    if (obj.contains("commands"))
    {
      std::vector<std::string> tmp;
      if (parse_commands_node(obj["commands"], tmp))
        def.commands = tmp;
    }

    return def;
  }

  static void merge_task_overlay(TaskDefinition &base, const TaskDefinition &overlay)
  {
    if (!overlay.description.empty())
      base.description = overlay.description;

    if (!overlay.deps.empty())
      base.deps.insert(base.deps.end(), overlay.deps.begin(), overlay.deps.end());

    if (!overlay.commands.empty())
      base.commands = overlay.commands;

    if (!overlay.cwd.empty())
      base.cwd = overlay.cwd;

    for (const auto &kv : overlay.env)
      base.env[kv.first] = kv.second;

    for (const auto &kv : overlay.vars)
      base.vars[kv.first] = kv.second;
  }

  static TaskDefinition parse_task_definition(
      const std::string &name,
      const nlohmann::json &node)
  {
    TaskDefinition def;
    def.name = name;

    if (node.is_string() || node.is_array())
    {
      parse_commands_node(node, def.commands);
      return def;
    }

    if (!node.is_object())
      return def;

    def = parse_task_object_fields(name, node);

    const std::string platform = current_platform();

    if (node.contains(platform))
    {
      const TaskDefinition overlay = parse_task_definition(name, node[platform]);
      merge_task_overlay(def, overlay);
    }

    if (node.contains("platform") &&
        node["platform"].is_object() &&
        node["platform"].contains(platform))
    {
      const TaskDefinition overlay = parse_task_definition(name, node["platform"][platform]);
      merge_task_overlay(def, overlay);
    }

    return def;
  }

  static TaskRegistry load_task_registry()
  {
    TaskRegistry reg;
    reg.manifestPath = find_vix_json();

    if (!reg.manifestPath.has_value())
      return reg;

    std::ifstream in(*reg.manifestPath);
    if (!in)
      return reg;

    nlohmann::json j;
    try
    {
      in >> j;
    }
    catch (...)
    {
      return reg;
    }

    if (!j.is_object())
      return reg;

    reg.globalVars = read_string_map(j, "vars");

    if (!j.contains("tasks") || !j["tasks"].is_object())
      return reg;

    for (auto it = j["tasks"].begin(); it != j["tasks"].end(); ++it)
    {
      reg.customTasks[it.key()] = parse_task_definition(it.key(), it.value());
    }

    return reg;
  }

  static bool parse_builtin_task_name(const std::string &name, TaskKind &out)
  {
    for (const auto &task : kBuiltins)
    {
      if (name == task.name)
      {
        out = task.kind;
        return true;
      }
    }

    if (name == "tests")
    {
      out = TaskKind::Test;
      return true;
    }

    return false;
  }

  static int run_build_builtin(const std::vector<std::string> &args)
  {
    std::string cmd = "vix build";
    for (const auto &arg : args)
    {
      cmd += " ";
      cmd += shell_quote(arg);
    }

    return normalize_exit_code_local(std::system(cmd.c_str()));
  }

  static int dispatch_builtin_task(TaskKind task, const std::vector<std::string> &args)
  {
    switch (task)
    {
    case TaskKind::Dev:
      return vix::commands::DevCommand::run(args);

    case TaskKind::Run:
      return vix::commands::RunCommand::run(args);

    case TaskKind::Fmt:
      return vix::commands::FmtCommand::run(args);

    case TaskKind::Check:
      return vix::commands::CheckCommand::run(args);

    case TaskKind::Build:
      return run_build_builtin(args);

    case TaskKind::Test:
      return vix::commands::TestsCommand::run(args);
    }

    return 1;
  }

  static std::string replace_all_copy(
      std::string text,
      const std::string &from,
      const std::string &to)
  {
    if (from.empty())
      return text;

    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos)
    {
      text.replace(pos, from.size(), to);
      pos += to.size();
    }

    return text;
  }

  static std::string expand_variables(
      std::string text,
      const std::map<std::string, std::string> &vars)
  {
    for (int pass = 0; pass < 8; ++pass)
    {
      std::string before = text;
      for (const auto &kv : vars)
        text = replace_all_copy(text, "${" + kv.first + "}", kv.second);

      if (text == before)
        break;
    }

    return text;
  }

  static std::string resolve_manifest_dir(const TaskRegistry &reg)
  {
    if (!reg.manifestPath.has_value())
    {
      std::error_code ec;
      const fs::path cwd = fs::current_path(ec);
      if (ec)
        return ".";
      return cwd.string();
    }

    return reg.manifestPath->parent_path().string();
  }

  static std::map<std::string, std::string> make_task_vars(
      const TaskRegistry &reg,
      const TaskDefinition &task)
  {
    std::map<std::string, std::string> vars = reg.globalVars;

    const std::string manifestDir = resolve_manifest_dir(reg);

    vars["task"] = task.name;
    vars["task_name"] = task.name;
    vars["platform"] = current_platform();
    vars["project_dir"] = manifestDir;
    vars["root"] = manifestDir;

    for (const auto &kv : task.vars)
      vars[kv.first] = kv.second;

    return vars;
  }

  static int run_shell_command_with_context(
      const std::string &command,
      const std::string &cwd,
      const std::map<std::string, std::string> &env)
  {
    ScopedCwd scopedCwd(cwd);
    ScopedEnv scopedEnv(env);

    return normalize_exit_code_local(std::system(command.c_str()));
  }

  static std::string custom_task_summary(const TaskDefinition &task)
  {
    if (!task.description.empty())
      return task.description;

    if (task.commands.empty() && !task.deps.empty())
      return "Run dependent tasks";

    if (task.commands.size() == 1)
      return task.commands.front();

    if (!task.commands.empty())
      return std::to_string(task.commands.size()) + " commands";

    return "Custom task";
  }

  static std::vector<MenuEntry> build_menu_entries(const TaskRegistry &reg)
  {
    std::vector<MenuEntry> out;

    for (const auto &task : kBuiltins)
    {
      MenuEntry e;
      e.name = task.name;
      e.description = task.description;
      e.isCustom = false;
      e.builtin = task.kind;
      out.push_back(e);
    }

    for (const auto &kv : reg.customTasks)
    {
      MenuEntry e;
      e.name = kv.first;
      e.description = custom_task_summary(kv.second);
      e.isCustom = true;
      out.push_back(e);
    }

    return out;
  }

  static void render_task_menu(
      const std::vector<MenuEntry> &entries,
      int selected,
      bool firstDraw)
  {
    const int total = 1 + static_cast<int>(entries.size());

    if (!firstDraw)
      move_cursor_up(total);

    clear_line();
    std::cout << PAD << BOLD << CYAN << "Task" << RESET << "\n";

    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
      clear_line();

      const bool active = (i == selected);
      const std::string kindLabel = entries[static_cast<std::size_t>(i)].isCustom ? "custom" : "built-in";

      if (active)
      {
        std::cout << PAD
                  << CYAN << BOLD << "❯ " << RESET
                  << CYAN << BOLD << entries[static_cast<std::size_t>(i)].name << RESET
                  << GRAY << " [" << kindLabel << "] " << entries[static_cast<std::size_t>(i)].description << RESET
                  << "\n";
      }
      else
      {
        std::cout << PAD
                  << "  "
                  << entries[static_cast<std::size_t>(i)].name
                  << GRAY << " [" << kindLabel << "] " << entries[static_cast<std::size_t>(i)].description << RESET
                  << "\n";
      }
    }

    std::cout.flush();
  }

  static SelectResult<MenuEntry> choose_task_interactive(const TaskRegistry &reg)
  {
    SelectResult<MenuEntry> res{};

    const std::vector<MenuEntry> entries = build_menu_entries(reg);
    if (entries.empty())
    {
      res.cancelled = true;
      return res;
    }

    res.value = entries.front();

    if (!can_interact())
      return res;

    CursorGuard cursor(true);
    SignalGuard sig;

#if !defined(_WIN32)
    RawMode raw;
    if (!raw.active)
      return res;
#endif

    int selected = 0;
    bool firstDraw = true;

    render_task_menu(entries, selected, firstDraw);
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
          selected = static_cast<int>(entries.size()) - 1;
        render_task_menu(entries, selected, firstDraw);
        continue;
      }

      if (k == Key::Down)
      {
        selected = (selected + 1) % static_cast<int>(entries.size());
        render_task_menu(entries, selected, firstDraw);
        continue;
      }

      if (k == Key::Enter)
      {
        std::cout << "\n";
        res.cancelled = false;
        res.value = entries[static_cast<std::size_t>(selected)];
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

  static void print_task_list(const TaskRegistry &reg)
  {
    ui::section(std::cout, "Task");

    std::cout << "  Built-in\n";
    for (const auto &task : kBuiltins)
      ui::kv(std::cout, task.name, task.description);

    if (!reg.customTasks.empty())
    {
      std::cout << "\n";
      std::cout << "  Custom\n";
      for (const auto &kv : reg.customTasks)
        ui::kv(std::cout, kv.first, custom_task_summary(kv.second));
    }
  }

  static int execute_named_task(
      ExecuteContext &ctx,
      const std::string &name,
      const std::vector<std::string> &finalArgs,
      bool passArgsToThisTask);

  static int execute_custom_task(
      ExecuteContext &ctx,
      const TaskDefinition &task,
      const std::vector<std::string> &finalArgs,
      bool passArgsToThisTask)
  {
    if (ctx.done.count(task.name) != 0)
      return 0;

    if (ctx.visiting.count(task.name) != 0)
    {
      ui::err_line(std::cerr, "task dependency cycle detected: " + task.name);
      return 1;
    }

    ctx.visiting.insert(task.name);

    for (const auto &dep : task.deps)
    {
      const int rc = execute_named_task(ctx, dep, {}, false);
      if (rc != 0)
      {
        ctx.visiting.erase(task.name);
        return rc;
      }
    }

    const auto vars = make_task_vars(ctx.registry, task);

    std::map<std::string, std::string> resolvedEnv;
    for (const auto &kv : task.env)
      resolvedEnv[kv.first] = expand_variables(kv.second, vars);

    std::string resolvedCwd = task.cwd.empty()
                                  ? resolve_manifest_dir(ctx.registry)
                                  : expand_variables(task.cwd, vars);

    for (std::size_t i = 0; i < task.commands.size(); ++i)
    {
      std::string command = expand_variables(task.commands[i], vars);

      if (passArgsToThisTask && i + 1 == task.commands.size())
        command = append_args_to_command(command, finalArgs);

      if (!command.empty())
      {
        const int rc = run_shell_command_with_context(command, resolvedCwd, resolvedEnv);
        if (rc != 0)
        {
          ctx.visiting.erase(task.name);
          return rc;
        }
      }
    }

    ctx.visiting.erase(task.name);
    ctx.done.insert(task.name);
    return 0;
  }

  static int execute_named_task(
      ExecuteContext &ctx,
      const std::string &name,
      const std::vector<std::string> &finalArgs,
      bool passArgsToThisTask)
  {
    auto it = ctx.registry.customTasks.find(name);
    if (it != ctx.registry.customTasks.end())
      return execute_custom_task(ctx, it->second, finalArgs, passArgsToThisTask);

    TaskKind builtin{};
    if (parse_builtin_task_name(name, builtin))
    {
      if (ctx.done.count(name) != 0)
        return 0;

      const std::vector<std::string> args = passArgsToThisTask ? finalArgs : std::vector<std::string>{};
      const int rc = dispatch_builtin_task(builtin, args);
      if (rc == 0)
        ctx.done.insert(name);
      return rc;
    }

    ui::err_line(std::cerr, "unknown task: " + name);
    return 1;
  }

} // namespace

namespace vix::commands
{
  int TaskCommand::run(const std::vector<std::string> &args)
  {
    if (!args.empty() && is_help_flag(args[0]))
      return help();

    const TaskRegistry registry = load_task_registry();

    if (!args.empty() && is_list_flag(args[0]))
    {
      print_task_list(registry);
      return 0;
    }

    ExecuteContext ctx{
        .registry = registry,
        .done = {},
        .visiting = {}};

    if (args.empty())
    {
      if (!can_interact())
      {
        ui::err_line(std::cerr, "missing task name");
        ui::tip_line(std::cerr, "Use 'vix task --help' to see available tasks");
        return 1;
      }

      const auto selected = choose_task_interactive(registry);
      if (selected.cancelled)
        return 2;

      return execute_named_task(ctx, selected.value.name, {}, true);
    }

    const std::string taskName = args[0];
    const std::vector<std::string> forwarded = tail_args(args);

    return execute_named_task(ctx, taskName, forwarded, true);
  }

  int TaskCommand::help()
  {
    std::ostream &out = std::cout;

    out
        << "vix task\n"
        << "Run reusable project tasks.\n\n"

        << "Usage\n"
        << "  vix task <name> [args...]\n"
        << "  vix task --list\n"
        << "  vix task --help\n\n"

        << "Built-in tasks\n"
        << "  dev       Run in dev mode with hot reload\n"
        << "  run       Run the application\n"
        << "  fmt       Format code\n"
        << "  check     Validate build, tests, runtime and sanitizers\n"
        << "  build     Build project\n"
        << "  test      Run tests with CTest\n\n"

        << "Custom tasks in vix.json\n"
        << "  Root keys supported:\n"
        << "    vars   Global variables\n"
        << "    tasks  Task definitions\n\n"

        << "Task definition formats\n"
        << "  \"tasks\": {\n"
        << "    \"fmt\": \"vix fmt\",\n"
        << "    \"ci\": [\"vix fmt\", \"vix check --tests\"],\n"
        << "    \"release\": {\n"
        << "      \"description\": \"Release pipeline\",\n"
        << "      \"deps\": [\"fmt\", \"test\"],\n"
        << "      \"vars\": { \"preset\": \"release\" },\n"
        << "      \"env\": { \"VIX_LOG_LEVEL\": \"info\" },\n"
        << "      \"cwd\": \"${project_dir}\",\n"
        << "      \"commands\": [\n"
        << "        \"vix build --preset ${preset}\",\n"
        << "        \"vix check --preset ${preset} --tests\"\n"
        << "      ],\n"
        << "      \"linux\": {\n"
        << "        \"command\": \"vix build --preset ${preset}\"\n"
        << "      },\n"
        << "      \"windows\": {\n"
        << "        \"command\": \"vix build --preset dev-msvc\"\n"
        << "      }\n"
        << "    }\n"
        << "  }\n\n"

        << "Variables\n"
        << "  Global vars come from the root 'vars' object.\n"
        << "  Task vars come from task.vars and override global vars.\n"
        << "  Built-in variables:\n"
        << "    ${task}\n"
        << "    ${task_name}\n"
        << "    ${platform}\n"
        << "    ${project_dir}\n"
        << "    ${root}\n\n"

        << "Rules\n"
        << "  - Custom tasks take priority over built-in tasks.\n"
        << "  - Dependencies are executed before the selected task.\n"
        << "  - Arrays in 'commands' run in order.\n"
        << "  - CLI args are appended only to the final command of the selected task.\n"
        << "  - Platform overrides can be defined with: windows, linux, macos.\n\n"

        << "Examples\n"
        << "  vix task dev\n"
        << "  vix task check\n"
        << "  vix task ci\n"
        << "  vix task release -- --verbose\n\n"

        << "Environment\n"
        << "  VIX_NONINTERACTIVE=1   Disable prompts\n"
        << "  CI=1                   Disable prompts\n";

    return 0;
  }
}
