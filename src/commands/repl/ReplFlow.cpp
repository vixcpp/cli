/**
 *
 *  @file RelpFlow.cpp
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
#include <vix/cli/commands/repl/ReplDetail.hpp>
#include <vix/cli/commands/repl/ReplDispatcher.hpp>
#include <vix/cli/commands/repl/ReplHistory.hpp>
#include <vix/cli/commands/repl/ReplMath.hpp>
#include <vix/cli/commands/repl/ReplUtils.hpp>
#include <vix/cli/commands/repl/ReplFlow.hpp>
#include <vix/cli/commands/repl/ReplLineEditor.hpp>
#include <vix/cli/commands/repl/api/Vix.hpp>
#include <vix/cli/commands/repl/api/ReplCallParser.hpp>
#include <vix/cli/commands/repl/api/ReplApi.hpp>
#include <vix/utils/Logger.hpp>

#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <atomic>
#include <string>
#include <cstring>
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <cmath>
#include <limits>

namespace fs = std::filesystem;

#ifndef VIX_CLI_VERSION
#define VIX_CLI_VERSION "dev"
#endif

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#endif

namespace
{
  // Ctrl keys (lowercase / not "majuscule")
  // In terminals:
  //   Ctrl+C -> 0x03
  //   Ctrl+D -> 0x04
  //   Ctrl+L -> 0x0C
  [[maybe_unused]] constexpr unsigned char KEY_CTRL_C = 0x03;
  [[maybe_unused]] constexpr unsigned char KEY_CTRL_D = 0x04;
  [[maybe_unused]] constexpr unsigned char KEY_CTRL_L = 0x0C;
  [[maybe_unused]] constexpr unsigned char KEY_BACKSPACE_1 = 0x08;
  [[maybe_unused]] constexpr unsigned char KEY_BACKSPACE_2 = 0x7F;
  [[maybe_unused]] constexpr unsigned char KEY_ENTER_1 = '\n';
  [[maybe_unused]] constexpr unsigned char KEY_ENTER_2 = '\r';

  enum class ReadStatus
  {
    Ok,
    Eof,         // Ctrl+D on empty line
    Interrupted, // Ctrl+C
    Clear        // Ctrl+L
  };

  static bool try_eval_math_with_vars(
      const std::string &expr,
      const std::unordered_map<std::string, nlohmann::json> &vars,
      nlohmann::json &valueOut,
      std::string &formattedOut,
      std::string &err);

#ifndef _WIN32
  struct TerminalRawMode
  {
    termios old{};
    bool enabled = false;

    TerminalRawMode()
    {
      if (!isatty(STDIN_FILENO))
        return;

      if (tcgetattr(STDIN_FILENO, &old) != 0)
        return;

      termios raw = old;

      // raw-ish: disable canonical & echo, keep signals disabled (we handle Ctrl+C ourselves)
      raw.c_lflag &= static_cast<unsigned>(~(ICANON | ECHO));
      raw.c_lflag &= static_cast<unsigned>(~(IEXTEN));
      raw.c_lflag &= static_cast<unsigned>(~(ISIG));

      // input tweaks
      raw.c_iflag &= static_cast<unsigned>(~(IXON | ICRNL));

      // one byte at a time
      raw.c_cc[VMIN] = 1;
      raw.c_cc[VTIME] = 0;

      if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
        enabled = true;
    }

    ~TerminalRawMode()
    {
      if (enabled)
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
    }
  };

#endif

  static void print_banner()
  {
    std::cout
        << "Vix " << VIX_CLI_VERSION << "  REPL\n";

#if defined(__clang__)
    std::cout << "clang " << __clang_major__ << "." << __clang_minor__;
#elif defined(__GNUC__)
    std::cout << "gcc " << __GNUC__ << "." << __GNUC_MINOR__;
#else
    std::cout << "c++";
#endif

#if defined(_WIN32)
    std::cout << "  windows\n";
#elif defined(__APPLE__)
    std::cout << "  macos\n";
#else
    std::cout << "  linux\n";
#endif

    std::cout
        << "exit: Ctrl+D | clear: Ctrl+L | help\n\n";
  }
  static void print_commands_from_dispatcher()
  {
    // CLI commands are intentionally disabled in REPL.
    std::cout << "Commands:\n";
    std::cout << "  (disabled in REPL)\n";
  }

  static void print_help()
  {
    std::cout
        << "\n"
        << "REPL commands:\n"
        << "  help                    Show this help\n"
        << "  help <command>          Show help for a Vix command (ex: help run)\n"
        << "  exit                    Exit the REPL (or Ctrl+C / Ctrl+D)\n"
        << "  version                 Print version\n"
        << "  pwd                     Print current directory\n"
        << "  cd <dir>                Change directory\n"
        << "  clear                   Clear screen (or Ctrl+L)\n"
        << "  history                 Show history\n"
        << "  history clear           Clear history\n"
        << "\n"
        << "Math:\n"
        << "  <expr>                  Evaluate expression (e.g. 1+2*(3+4))\n"
        << "  calc <expr>             Evaluate expression (explicit)\n"
        << "\n";

    print_commands_from_dispatcher();
    std::cout << "\n";
  }

  static std::string strip_prefix(std::string s, const std::string &prefix)
  {
    if (s.rfind(prefix, 0) == 0)
      s = s.substr(prefix.size());
    return vix::cli::repl::trim_copy(s);
  }

  static std::string to_string(const vix::cli::repl::api::CallValue &v)
  {
    if (v.is_string())
      return v.as_string();
    if (v.is_bool())
      return v.as_bool() ? "true" : "false";
    if (v.is_int())
      return std::to_string(v.as_int());
    if (v.is_double())
      return std::to_string(v.as_double());
    return "null";
  }

  static bool contains_math_ops(const std::string &s)
  {
    for (char c : s)
    {
      if (c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')')
        return true;
    }
    return false;
  }

  static bool is_ident_start(char c)
  {
    unsigned char u = (unsigned char)c;
    return std::isalpha(u) || c == '_';
  }
  static bool is_ident_char(char c)
  {
    unsigned char u = (unsigned char)c;
    return std::isalnum(u) || c == '_';
  }

  static bool looks_like_ident(const std::string &s)
  {
    if (s.empty())
      return false;

    if (!is_ident_start(s.front()))
      return false;

    for (char c : s)
      if (!is_ident_char(c))
        return false;

    return true;
  }

  static std::string format_json_scalar(const nlohmann::json &j);

  static bool resolve_value_at_path(
      const std::unordered_map<std::string, nlohmann::json> &vars,
      const std::string &raw,
      nlohmann::json &out,
      std::string &err);

  static std::string arg_to_text(
      const vix::cli::repl::api::CallExpr &call,
      size_t i,
      const std::unordered_map<std::string, nlohmann::json> &vars,
      std::string &err)
  {
    using namespace vix::cli::repl;

    err.clear();

    if (i >= call.args.size())
      return {};

    if (call.args[i].is_string())
      return call.args[i].as_string();

    if (i < call.args_raw.size())
    {
      const std::string raw = trim_copy(call.args_raw[i]);

      // path resolution first: data.name, data["name"], items[0]
      if (looks_like_ident(raw) ||
          raw.find('.') != std::string::npos ||
          raw.find('[') != std::string::npos)
      {
        nlohmann::json resolved;
        if (resolve_value_at_path(vars, raw, resolved, err))
          return format_json_scalar(resolved);

        // if path-like lookup failed, stop here with real error
        if (!err.empty())
          return {};
      }

      // math expression with vars: println(x+2)
      if (contains_math_ops(raw))
      {
        nlohmann::json val;
        std::string formatted;

        if (!try_eval_math_with_vars(raw, vars, val, formatted, err))
          return {};

        return formatted;
      }
    }

    return to_string(call.args[i]);
  }

  static std::string format_json_scalar(const nlohmann::json &j)
  {
    if (j.is_string())
      return j.get<std::string>();

    if (j.is_boolean())
      return j.get<bool>() ? "true" : "false";

    if (j.is_number_integer())
      return std::to_string(j.get<long long>());

    if (j.is_number_unsigned())
      return std::to_string(j.get<unsigned long long>());

    if (j.is_number_float())
      return j.dump();

    if (j.is_null())
      return "null";

    return j.dump();
  }

  static bool try_parse_int_text(const std::string &s, long long &out)
  {
    try
    {
      size_t idx = 0;
      long long v = std::stoll(s, &idx, 10);
      if (idx != s.size())
        return false;
      out = v;
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  static bool try_parse_double_text(const std::string &s, double &out)
  {
    try
    {
      size_t idx = 0;
      double v = std::stod(s, &idx);
      if (idx != s.size())
        return false;
      out = v;
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  static bool json_number_to_long_long(const nlohmann::json &j, long long &out)
  {
    if (j.is_number_integer())
    {
      out = j.get<long long>();
      return true;
    }

    if (j.is_number_unsigned())
    {
      const auto v = j.get<unsigned long long>();
      if (v > static_cast<unsigned long long>(LLONG_MAX))
        return false;
      out = static_cast<long long>(v);
      return true;
    }

    if (j.is_number_float())
    {
      const double d = j.get<double>();
      const double id = std::round(d);

      if (std::fabs(d - id) < 1e-12 &&
          id >= static_cast<double>(LLONG_MIN) &&
          id <= static_cast<double>(LLONG_MAX))
      {
        out = static_cast<long long>(id);
        return true;
      }
    }

    return false;
  }

  static bool json_number_to_double(const nlohmann::json &j, double &out)
  {
    if (!j.is_number())
      return false;

    out = j.get<double>();
    return true;
  }

  static bool resolve_value_at_path(
      const std::unordered_map<std::string, nlohmann::json> &vars,
      const std::string &raw,
      nlohmann::json &out,
      std::string &err)
  {
    err.clear();
    const std::string expr = vix::cli::repl::trim_copy(raw);

    if (expr.empty())
    {
      err = "empty expression";
      return false;
    }

    size_t i = 0;

    auto parse_ident = [&](std::string &name) -> bool
    {
      name.clear();

      if (i >= expr.size() || !is_ident_start(expr[i]))
        return false;

      size_t start = i++;
      while (i < expr.size() && is_ident_char(expr[i]))
        ++i;

      name = expr.substr(start, i - start);
      return true;
    };

    std::string root;
    if (!parse_ident(root))
    {
      err = "expected variable name";
      return false;
    }

    auto it = vars.find(root);
    if (it == vars.end())
    {
      err = "unknown variable: " + root;
      return false;
    }

    out = it->second;

    while (i < expr.size())
    {
      if (expr[i] == '.')
      {
        ++i;

        std::string key;
        if (!parse_ident(key))
        {
          err = "expected property name after '.'";
          return false;
        }

        if (!out.is_object())
        {
          err = "value is not an object";
          return false;
        }

        auto objIt = out.find(key);
        if (objIt == out.end())
        {
          err = "unknown property: " + key;
          return false;
        }

        out = *objIt;
        continue;
      }

      if (expr[i] == '[')
      {
        ++i;
        while (i < expr.size() && std::isspace((unsigned char)expr[i]))
          ++i;

        if (i >= expr.size())
        {
          err = "unterminated '['";
          return false;
        }

        // string key: ["name"] or ['name']
        if (expr[i] == '"' || expr[i] == '\'')
        {
          const char quote = expr[i++];
          std::string key;

          while (i < expr.size() && expr[i] != quote)
          {
            if (expr[i] == '\\' && i + 1 < expr.size())
            {
              key.push_back(expr[i + 1]);
              i += 2;
              continue;
            }

            key.push_back(expr[i++]);
          }

          if (i >= expr.size() || expr[i] != quote)
          {
            err = "unterminated string key";
            return false;
          }

          ++i;
          while (i < expr.size() && std::isspace((unsigned char)expr[i]))
            ++i;

          if (i >= expr.size() || expr[i] != ']')
          {
            err = "expected ']'";
            return false;
          }

          ++i;

          if (!out.is_object())
          {
            err = "value is not an object";
            return false;
          }

          auto objIt = out.find(key);
          if (objIt == out.end())
          {
            err = "unknown property: " + key;
            return false;
          }

          out = *objIt;
          continue;
        }

        // numeric index
        size_t start = i;
        if (i < expr.size() && (expr[i] == '+' || expr[i] == '-'))
          ++i;

        while (i < expr.size() && std::isdigit((unsigned char)expr[i]))
          ++i;

        const std::string idxText = expr.substr(start, i - start);

        while (i < expr.size() && std::isspace((unsigned char)expr[i]))
          ++i;

        if (i >= expr.size() || expr[i] != ']')
        {
          err = "expected ']'";
          return false;
        }

        ++i;

        long long index = 0;
        if (!try_parse_int_text(idxText, index))
        {
          err = "invalid array index";
          return false;
        }

        if (!out.is_array())
        {
          err = "value is not an array";
          return false;
        }

        if (index < 0 || static_cast<size_t>(index) >= out.size())
        {
          err = "array index out of range";
          return false;
        }

        out = out[static_cast<size_t>(index)];
        continue;
      }

      err = std::string("unexpected character in path: '") + expr[i] + "'";
      return false;
    }

    return true;
  }

  static bool resolve_arg_json(
      const vix::cli::repl::api::CallExpr &call,
      size_t i,
      const std::unordered_map<std::string, nlohmann::json> &vars,
      nlohmann::json &out,
      std::string &err)
  {
    err.clear();

    if (i >= call.args.size())
    {
      err = "missing argument";
      return false;
    }

    const auto &arg = call.args[i];

    if (arg.is_string())
    {
      out = arg.as_string();
      return true;
    }

    if (arg.is_bool())
    {
      out = arg.as_bool();
      return true;
    }

    if (arg.is_int())
    {
      out = arg.as_int();
      return true;
    }

    if (arg.is_double())
    {
      out = arg.as_double();
      return true;
    }

    if (i < call.args_raw.size())
    {
      const std::string raw = vix::cli::repl::trim_copy(call.args_raw[i]);

      if (looks_like_ident(raw) || raw.find('.') != std::string::npos || raw.find('[') != std::string::npos)
      {
        if (resolve_value_at_path(vars, raw, out, err))
          return true;
      }

      if (raw == "null")
      {
        out = nullptr;
        return true;
      }

      long long iv = 0;
      if (try_parse_int_text(raw, iv))
      {
        out = iv;
        return true;
      }

      double dv = 0.0;
      if (try_parse_double_text(raw, dv))
      {
        out = dv;
        return true;
      }
    }

    out = nullptr;
    return true;
  }

  static bool get_len_of_json(const nlohmann::json &j, long long &out)
  {
    if (j.is_string())
    {
      out = static_cast<long long>(j.get<std::string>().size());
      return true;
    }

    if (j.is_array() || j.is_object())
    {
      out = static_cast<long long>(j.size());
      return true;
    }

    return false;
  }

  static bool invoke_call(
      const vix::cli::repl::api::CallExpr &call,
      vix::cli::repl::api::Vix &VixObj,
      const std::unordered_map<std::string, nlohmann::json> &vars)
  {
    auto print_error = [](const std::string &msg)
    {
      vix::cli::repl::api::println("error: " + msg);
    };

    auto join_all = [&](std::string_view sep, std::string &err) -> std::optional<std::string>
    {
      std::string out;
      for (size_t i = 0; i < call.args.size(); ++i)
      {
        if (i)
          out += sep;

        auto t = arg_to_text(call, i, vars, err);
        if (!err.empty())
          return std::nullopt;

        out += t;
      }
      return out;
    };

    auto emit_joined = [&](auto emit, std::string_view sep = " ") -> bool
    {
      std::string err;
      auto txt = join_all(sep, err);
      if (!txt)
      {
        print_error(err.empty() ? "invalid arguments" : err);
        return true;
      }

      emit(*txt);
      return true;
    };

    auto get_string_arg = [&](size_t i, std::string &out, std::string &err) -> bool
    {
      err.clear();

      nlohmann::json j;
      if (!resolve_arg_json(call, i, vars, j, err))
        return false;

      if (!j.is_string())
      {
        err = "expected string";
        return false;
      }

      out = j.get<std::string>();
      return true;
    };

    auto get_bool_arg = [&](size_t i, bool &out, std::string &err) -> bool
    {
      err.clear();

      nlohmann::json j;
      if (!resolve_arg_json(call, i, vars, j, err))
        return false;

      if (!j.is_boolean())
      {
        err = "expected bool";
        return false;
      }

      out = j.get<bool>();
      return true;
    };

    auto get_int_arg = [&](size_t i, long long &out, std::string &err) -> bool
    {
      err.clear();

      nlohmann::json j;
      if (!resolve_arg_json(call, i, vars, j, err))
        return false;

      if (!json_number_to_long_long(j, out))
      {
        err = "expected int";
        return false;
      }

      return true;
    };

    // GLOBAL: print / println / eprint / eprintln / builtins
    if (call.object.empty())
    {
      const std::string &fn = call.callee;

      if (fn == "print")
      {
        return emit_joined([](const std::string &s)
                           { vix::cli::repl::api::print(s); });
      }

      if (fn == "println")
      {
        return emit_joined([](const std::string &s)
                           { vix::cli::repl::api::println(s); });
      }

      if (fn == "eprint")
      {
        return emit_joined([](const std::string &s)
                           { vix::cli::repl::api::eprint(s); });
      }

      if (fn == "eprintln")
      {
        return emit_joined([](const std::string &s)
                           { vix::cli::repl::api::eprintln(s); });
      }

      if (fn == "cwd")
      {
        if (!call.args.empty())
        {
          print_error("cwd() takes no arguments");
          return true;
        }

        vix::cli::repl::api::println(VixObj.cwd().string());
        return true;
      }

      if (fn == "pid")
      {
        if (!call.args.empty())
        {
          print_error("pid() takes no arguments");
          return true;
        }

        vix::cli::repl::api::println_int((long long)VixObj.pid());
        return true;
      }

      if (fn == "len")
      {
        if (call.args.size() != 1)
        {
          print_error("len(value)");
          return true;
        }

        nlohmann::json j;
        std::string err;
        if (!resolve_arg_json(call, 0, vars, j, err))
        {
          print_error(err);
          return true;
        }

        long long len = 0;
        if (!get_len_of_json(j, len))
        {
          print_error("len() expects string, array or object");
          return true;
        }

        vix::cli::repl::api::println(std::to_string(len));
        return true;
      }

      if (fn == "double" || fn == "float")
      {
        if (call.args.size() != 1)
        {
          print_error("float(value)");
          return true;
        }

        nlohmann::json j;
        std::string err;
        if (!resolve_arg_json(call, 0, vars, j, err))
        {
          print_error(err);
          return true;
        }

        double out = 0.0;

        if (j.is_string())
        {
          if (!try_parse_double_text(j.get<std::string>(), out))
          {
            print_error("cannot convert string to float");
            return true;
          }
        }
        else if (!json_number_to_double(j, out))
        {
          print_error("float() expects numeric value or numeric string");
          return true;
        }

        vix::cli::repl::api::println(std::to_string(out));
        return true;
      }

      if (fn == "str")
      {
        if (call.args.size() != 1)
        {
          print_error("str(value)");
          return true;
        }

        nlohmann::json j;
        std::string err;
        if (!resolve_arg_json(call, 0, vars, j, err))
        {
          print_error(err);
          return true;
        }

        vix::cli::repl::api::println(format_json_scalar(j));
        return true;
      }

      if (fn == "int")
      {
        if (call.args.size() != 1)
        {
          print_error("int(value)");
          return true;
        }

        nlohmann::json j;
        std::string err;
        if (!resolve_arg_json(call, 0, vars, j, err))
        {
          print_error(err);
          return true;
        }

        long long out = 0;

        if (j.is_string())
        {
          if (!try_parse_int_text(j.get<std::string>(), out))
          {
            print_error("cannot convert string to int");
            return true;
          }
        }
        else if (!json_number_to_long_long(j, out))
        {
          print_error("int() expects numeric value or numeric string");
          return true;
        }

        vix::cli::repl::api::println(std::to_string(out));
        return true;
      }

      if (fn == "float")
      {
        if (call.args.size() != 1)
        {
          print_error("float(value)");
          return true;
        }

        nlohmann::json j;
        std::string err;
        if (!resolve_arg_json(call, 0, vars, j, err))
        {
          print_error(err);
          return true;
        }

        double out = 0.0;

        if (j.is_string())
        {
          if (!try_parse_double_text(j.get<std::string>(), out))
          {
            print_error("cannot convert string to float");
            return true;
          }
        }
        else if (!json_number_to_double(j, out))
        {
          print_error("float() expects numeric value or numeric string");
          return true;
        }

        vix::cli::repl::api::println(std::to_string(out));
        return true;
      }

      if (fn == "type")
      {
        if (call.args.size() != 1)
        {
          print_error("type(value)");
          return true;
        }

        nlohmann::json j;
        std::string err;
        if (!resolve_arg_json(call, 0, vars, j, err))
        {
          print_error(err);
          return true;
        }

        if (j.is_string())
          vix::cli::repl::api::println("string");
        else if (j.is_boolean())
          vix::cli::repl::api::println("bool");
        else if (j.is_number_integer() || j.is_number_unsigned())
          vix::cli::repl::api::println("int");
        else if (j.is_number_float())
          vix::cli::repl::api::println("double");
        else if (j.is_array())
          vix::cli::repl::api::println("array");
        else if (j.is_object())
          vix::cli::repl::api::println("object");
        else if (j.is_null())
          vix::cli::repl::api::println("null");
        else
          vix::cli::repl::api::println("unknown");

        return true;
      }

      return false;
    }

    const std::string obj = call.object;
    if (obj != "Vix" && obj != "vix")
      return false;

    const std::string &m = call.member;

    if (m == "cd")
    {
      if (call.args.size() != 1)
      {
        print_error("Vix.cd(path:string)");
        return true;
      }

      std::string path;
      std::string err;
      if (!get_string_arg(0, path, err))
      {
        print_error("Vix.cd(path:string)");
        return true;
      }

      auto r = VixObj.cd(path);
      if (r.code != 0 && !r.message.empty())
        print_error(r.message);

      return true;
    }

    if (m == "cwd")
    {
      if (!call.args.empty())
      {
        print_error("Vix.cwd() takes no arguments");
        return true;
      }

      vix::cli::repl::api::println(VixObj.cwd().string());
      return true;
    }

    if (m == "mkdir")
    {
      if (call.args.empty() || call.args.size() > 2)
      {
        print_error("Vix.mkdir(path:string, recursive?:bool)");
        return true;
      }

      std::string path;
      std::string err;
      if (!get_string_arg(0, path, err))
      {
        print_error("Vix.mkdir(path:string, recursive?:bool)");
        return true;
      }

      bool recursive = true;
      if (call.args.size() == 2)
      {
        if (!get_bool_arg(1, recursive, err))
        {
          print_error("recursive must be bool");
          return true;
        }
      }

      auto r = VixObj.mkdir(path, recursive);
      if (r.code != 0 && !r.message.empty())
        print_error(r.message);

      return true;
    }

    if (m == "env")
    {
      if (call.args.size() != 1)
      {
        print_error("Vix.env(key:string)");
        return true;
      }

      std::string key;
      std::string err;
      if (!get_string_arg(0, key, err))
      {
        print_error("Vix.env(key:string)");
        return true;
      }

      auto v = VixObj.env(key);
      if (!v)
        vix::cli::repl::api::println("null");
      else
        vix::cli::repl::api::println(*v);

      return true;
    }

    if (m == "pid")
    {
      if (!call.args.empty())
      {
        print_error("Vix.pid() takes no arguments");
        return true;
      }

      vix::cli::repl::api::println_int((long long)VixObj.pid());
      return true;
    }

    if (m == "exit")
    {
      if (call.args.size() > 1)
      {
        print_error("Vix.exit(code?:int)");
        return true;
      }

      int code = 0;
      if (!call.args.empty())
      {
        long long tmp = 0;
        std::string err;
        if (!get_int_arg(0, tmp, err))
        {
          print_error("Vix.exit(code?:int)");
          return true;
        }

        if (tmp < (long long)std::numeric_limits<int>::min() ||
            tmp > (long long)std::numeric_limits<int>::max())
        {
          print_error("exit code out of range");
          return true;
        }

        code = (int)tmp;
      }

      VixObj.exit(code);
      return true;
    }

    if (m == "args")
    {
      if (!call.args.empty())
      {
        print_error(
            "Vix.args() takes no arguments.\n"
            "hint: use Vix.args() to read CLI args.");
        return true;
      }

      nlohmann::json j = nlohmann::json::array();
      for (const auto &a : VixObj.args())
        j.push_back(a);

      vix::cli::repl::api::println(j.dump());
      return true;
    }

    if (m == "history")
    {
      if (!call.args.empty())
      {
        print_error("Vix.history() takes no arguments");
        return true;
      }

      auto r = VixObj.history();
      if (r.code != 0 && !r.message.empty())
        print_error(r.message);

      return true;
    }

    if (m == "history_clear")
    {
      if (!call.args.empty())
      {
        print_error("Vix.history_clear() takes no arguments");
        return true;
      }

      auto r = VixObj.history_clear();
      if (r.code != 0 && !r.message.empty())
        print_error(r.message);

      return true;
    }

    if (m == "run")
    {
      vix::cli::repl::api::println("error: Vix.run() is disabled in REPL.");
      return true;
    }

    return false;
  }

  static bool contains_forbidden_code_chars(const std::string &s)
  {
    for (char c : s)
    {
      if (c == ';' || c == '#')
        return true;
    }
    return false;
  }

  static bool contains_math_ops_anywhere(const std::string &s)
  {
    for (char c : s)
    {
      if (c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')')
        return true;
    }
    return false;
  }

  static std::optional<std::pair<std::string, std::string>> parse_assignment(const std::string &line)
  {
    using vix::cli::repl::trim_copy;

    bool inQuote = false;
    char quote = 0;

    size_t eq = std::string::npos;

    for (size_t i = 0; i < line.size(); ++i)
    {
      char c = line[i];

      if (inQuote)
      {
        if (c == '\\' && i + 1 < line.size())
        {
          ++i; // skip escaped char
          continue;
        }

        if (c == quote)
        {
          inQuote = false;
        }

        continue;
      }

      if (c == '"' || c == '\'')
      {
        inQuote = true;
        quote = c;
        continue;
      }

      if (c == '=')
      {
        eq = i;
        break;
      }
    }

    if (eq == std::string::npos)
      return std::nullopt;

    std::string left = trim_copy(line.substr(0, eq));
    std::string right = trim_copy(line.substr(eq + 1));

    if (left.empty() || right.empty())
      return std::nullopt;

    // validation identifiant
    if (!is_ident_start(left.front()))
      return std::nullopt;

    for (char c : left)
      if (!is_ident_char(c))
        return std::nullopt;

    return std::make_pair(left, right);
  }
  static bool looks_like_json_literal(const std::string &rhs)
  {
    auto t = vix::cli::repl::trim_copy(rhs);
    if (t.empty())
      return false;
    return (t.front() == '{' || t.front() == '[');
  }

  // Replace identifiers by numeric values from vars (only if var is number).
  static bool substitute_numeric_vars(
      const std::string &expr,
      const std::unordered_map<std::string, nlohmann::json> &vars,
      std::string &out,
      std::string &err)
  {
    out.clear();
    err.clear();

    bool inQuote = false;
    char quote = 0;

    for (size_t i = 0; i < expr.size();)
    {
      char c = expr[i];

      if (inQuote)
      {
        if (c == '\\' && i + 1 < expr.size())
        {
          out.push_back(expr[i]);
          out.push_back(expr[i + 1]);
          i += 2;
          continue;
        }
        out.push_back(c);
        if (c == quote)
          inQuote = false;
        ++i;
        continue;
      }

      if (c == '"' || c == '\'')
      {
        inQuote = true;
        quote = c;
        out.push_back(c);
        ++i;
        continue;
      }

      if (is_ident_start(c))
      {
        size_t j = i + 1;
        while (j < expr.size() && is_ident_char(expr[j]))
          ++j;

        std::string name = expr.substr(i, j - i);
        auto it = vars.find(name);
        if (it == vars.end())
        {
          err = "unknown variable: " + name;
          return false;
        }
        if (!it->second.is_number())
        {
          err = "variable '" + name + "' is not a number";
          return false;
        }

        // dump number without quotes
        out += it->second.dump();
        i = j;
        continue;
      }

      out.push_back(c);
      ++i;
    }

    return true;
  }

  static bool try_eval_math_with_vars(
      const std::string &expr,
      const std::unordered_map<std::string, nlohmann::json> &vars,
      nlohmann::json &valueOut,
      std::string &formattedOut,
      std::string &err)
  {
    err.clear();
    formattedOut.clear();

    std::string substituted;
    if (!substitute_numeric_vars(expr, vars, substituted, err))
      return false;

    auto r = vix::cli::repl::eval_expression(substituted, err);
    if (!r)
      return false;

    formattedOut = r->formatted;

    try
    {
      double v = std::stod(r->formatted);
      double iv = std::round(v);

      if (std::fabs(v - iv) < 1e-12 &&
          iv >= (double)LLONG_MIN &&
          iv <= (double)LLONG_MAX)
      {
        valueOut = (long long)iv;
      }
      else
      {
        valueOut = v;
      }
    }
    catch (...)
    {
      err = "math result is not a number";
      return false;
    }

    return true;
  }

  static bool looks_like_path_expr(const std::string &s)
  {
    if (s.empty())
      return false;

    if (!is_ident_start(s.front()))
      return false;

    for (char c : s)
    {
      if (std::isspace((unsigned char)c))
        return false;
    }

    return s.find('.') != std::string::npos || s.find('[') != std::string::npos;
  }

  static bool try_print_resolved_path_expr(
      const std::string &line,
      const std::unordered_map<std::string, nlohmann::json> &vars)
  {
    const std::string expr = vix::cli::repl::trim_copy(line);
    if (!looks_like_path_expr(expr))
      return false;

    nlohmann::json out;
    std::string err;
    if (!resolve_value_at_path(vars, expr, out, err))
    {
      std::cout << "error: " << err << "\n";
      return true;
    }

    std::cout << format_json_scalar(out) << "\n";
    return true;
  }

  static bool try_parse_simple_literal(const std::string &raw, nlohmann::json &out)
  {
    const std::string t = vix::cli::repl::trim_copy(raw);
    if (t.empty())
      return false;

    // string literal
    if (t.size() >= 2 &&
        ((t.front() == '"' && t.back() == '"') ||
         (t.front() == '\'' && t.back() == '\'')))
    {
      std::string s;
      s.reserve(t.size() - 2);

      for (size_t i = 1; i + 1 < t.size(); ++i)
      {
        char c = t[i];

        if (c == '\\' && i + 1 < t.size() - 1)
        {
          const char esc = t[++i];
          switch (esc)
          {
          case 'n':
            s.push_back('\n');
            break;
          case 't':
            s.push_back('\t');
            break;
          case 'r':
            s.push_back('\r');
            break;
          case '\\':
            s.push_back('\\');
            break;
          case '"':
            s.push_back('"');
            break;
          case '\'':
            s.push_back('\'');
            break;
          case '0':
            s.push_back('\0');
            break;
          default:
            s.push_back(esc);
            break;
          }
          continue;
        }

        s.push_back(c);
      }

      out = s;
      return true;
    }

    // bool / null
    if (t == "true")
    {
      out = true;
      return true;
    }

    if (t == "false")
    {
      out = false;
      return true;
    }

    if (t == "null")
    {
      out = nullptr;
      return true;
    }

    // int
    long long iv = 0;
    if (try_parse_int_text(t, iv))
    {
      out = iv;
      return true;
    }

    // double
    double dv = 0.0;
    if (try_parse_double_text(t, dv))
    {
      out = dv;
      return true;
    }

    // typed literals
    auto starts_with = [](const std::string &s, const std::string &prefix) -> bool
    {
      return s.rfind(prefix, 0) == 0;
    };

    if ((starts_with(t, "int(") || starts_with(t, "long(") || starts_with(t, "long long(")) && !t.empty() && t.back() == ')')
    {
      const size_t lp = t.find('(');
      const std::string inner = vix::cli::repl::trim_copy(t.substr(lp + 1, t.size() - lp - 2));

      long long v = 0;
      if (try_parse_int_text(inner, v))
      {
        out = v;
        return true;
      }

      double d = 0.0;
      if (try_parse_double_text(inner, d))
      {
        out = static_cast<long long>(d);
        return true;
      }

      return false;
    }

    if ((starts_with(t, "double(") || starts_with(t, "float(")) &&
        !t.empty() && t.back() == ')')
    {
      const size_t lp = t.find('(');
      const std::string inner = vix::cli::repl::trim_copy(t.substr(lp + 1, t.size() - lp - 2));

      double v = 0.0;
      if (try_parse_double_text(inner, v))
      {
        out = v;
        return true;
      }

      long long iv2 = 0;
      if (try_parse_int_text(inner, iv2))
      {
        out = static_cast<double>(iv2);
        return true;
      }

      return false;
    }

    if (starts_with(t, "bool(") && t.back() == ')')
    {
      const std::string inner = vix::cli::repl::trim_copy(t.substr(5, t.size() - 6));
      if (inner == "true")
      {
        out = true;
        return true;
      }
      if (inner == "false")
      {
        out = false;
        return true;
      }
      return false;
    }

    if ((starts_with(t, "string(") || starts_with(t, "std::string(")) &&
        !t.empty() && t.back() == ')')
    {
      const size_t lp = t.find('(');
      const std::string inner = vix::cli::repl::trim_copy(t.substr(lp + 1, t.size() - lp - 2));

      if (inner.size() >= 2 &&
          ((inner.front() == '"' && inner.back() == '"') ||
           (inner.front() == '\'' && inner.back() == '\'')))
      {
        out = inner.substr(1, inner.size() - 2);
        return true;
      }

      return false;
    }

    return false;
  }

} // namespace

namespace vix::commands::ReplCommand
{
  int repl_flow_run(const std::vector<std::string> &replArgs)
  {
    using vix::cli::repl::History;
    using vix::cli::repl::ReplConfig;

    auto &logger = vix::utils::Logger::getInstance();
    (void)logger;

    ReplConfig cfg;
    cfg.historyFile = (vix::cli::repl::user_home_dir() / ".vix_history").string();

    History history(cfg.maxHistory);
    vix::cli::repl::api::Vix VixObj(&history);
    std::unordered_map<std::string, nlohmann::json> vars;

    VixObj.set_args(replArgs);

    if (cfg.enableFileHistory)
    {
      std::string err;
      history.loadFromFile(cfg.historyFile, &err);
    }

    if (cfg.showBannerOnStart)
      print_banner();

#ifndef _WIN32
    TerminalRawMode rawMode; // enables Ctrl+C/Ctrl+D/Ctrl+L reliably without Enter
    // History navigation state
    int histIndex = -1;    // -1 means "not browsing"
    std::string histDraft; // keep current typed line before browsing
#endif

    while (true)
    {
      const fs::path cwd = fs::current_path();
      const std::string prompt = vix::cli::repl::make_prompt(cwd);

      std::string line;

#ifndef _WIN32
      // HISTORY (↑ / ↓)
      auto onHistoryUp = [&](std::string &ioLine) -> bool
      {
        const auto &items = history.items();
        if (items.empty())
          return false;

        if (histIndex == -1)
        {
          histDraft = ioLine;
          histIndex = (int)items.size() - 1;
          ioLine = items[(size_t)histIndex];
          return true;
        }

        if (histIndex > 0)
        {
          --histIndex;
          ioLine = items[(size_t)histIndex];
          return true;
        }

        return false;
      };

      auto onHistoryDown = [&](std::string &ioLine) -> bool
      {
        const auto &items = history.items();
        if (items.empty())
          return false;

        if (histIndex == -1)
          return false;

        if (histIndex < (int)items.size() - 1)
        {
          ++histIndex;
          ioLine = items[(size_t)histIndex];
          return true;
        }

        histIndex = -1;
        ioLine = histDraft;
        return true;
      };

      // Reset history browsing as soon as user starts a new fresh command after execution
      // (we also reset it when user presses Enter below)
      auto reset_history_browse = [&]()
      {
        histIndex = -1;
        histDraft.clear();
      };

      // COMPLETION (TAB)
      // - commands
      // - options per command
      // - cd path completion
      auto completer = [&](const std::string &current) -> vix::cli::repl::CompletionResult
      {
        using vix::cli::repl::CompletionResult;

        CompletionResult r;

        std::string trimmed = vix::cli::repl::trim_copy(current);
        auto parts = vix::cli::repl::split_command_line(trimmed);

        auto starts_with = [](const std::string &s, const std::string &p) -> bool
        {
          return s.rfind(p, 0) == 0;
        };

        // Builtins and global options (REPL commands)
        const std::vector<std::string> builtins = {
            "help", "exit", "clear", "pwd", "cd",
            "history", "version", "commands", "calc"};

        // Options by command (REPL only)
        auto options_for = [&](const std::string &) -> std::vector<std::string>
        {
          return {}; // no CLI options in REPL
        };

        // Utility: unique/sort
        auto normalize_list = [](std::vector<std::string> &v)
        {
          std::sort(v.begin(), v.end());
          v.erase(std::unique(v.begin(), v.end()), v.end());
        };

        // COMMAND completion (first token)
        if (parts.size() <= 1 && trimmed.find(' ') == std::string::npos)
        {
          const std::string prefix = trimmed;
          std::vector<std::string> matches;

          // builtins
          for (const auto &b : builtins)
          {
            if (starts_with(b, prefix))
              matches.push_back(b);
          }

          normalize_list(matches);

          if (matches.size() == 1)
          {
            r.changed = true;
            r.newLine = matches[0] + " ";
            return r;
          }
          if (matches.size() > 1)
          {
            r.suggestions = matches;
            return r;
          }
          return r;
        }

        if (parts.empty())
          return r;

        const std::string cmd = parts[0];

        // CONTEXT: cd <path> completion
        if (cmd == "cd" && trimmed.size() >= 2)
        {
          // Determine what user is completing: the last token (path)
          std::string pathPrefix;
          if (parts.size() >= 2)
            pathPrefix = parts.back();
          else
            pathPrefix.clear();

          namespace fs2 = std::filesystem;

          fs2::path baseDir = ".";
          std::string filePrefix = pathPrefix;

          // If prefix contains '/', split base dir / prefix
          auto pos = pathPrefix.find_last_of("/\\");
          if (pos != std::string::npos)
          {
            baseDir = fs2::path(pathPrefix.substr(0, pos + 1));
            filePrefix = pathPrefix.substr(pos + 1);
          }

          std::vector<std::string> dirs;

          std::error_code ec;
          if (fs2::exists(baseDir, ec) && fs2::is_directory(baseDir, ec))
          {
            for (auto &it : fs2::directory_iterator(baseDir, ec))
            {
              if (ec)
                break;

              if (!it.is_directory(ec))
                continue;

              std::string name = it.path().filename().string();
              if (starts_with(name, filePrefix))
              {
                std::string full = (baseDir / name).string();
                // Normalize to trailing slash (nice UX)
                if (!full.empty() && full.back() != '/')
                  full.push_back('/');
                dirs.push_back(full);
              }
            }
          }

          normalize_list(dirs);

          if (dirs.size() == 1)
          {
            // Replace last token with the completed dir
            // rebuild line: "cd " + dirs[0]
            r.changed = true;
            r.newLine = "cd " + dirs[0];
            return r;
          }

          if (dirs.size() > 1)
          {
            r.suggestions = dirs;
            return r;
          }

          return r;
        }

        // OPTIONS completion (TAB after cmd or when typing -)
        const std::vector<std::string> opts = options_for(cmd);

        // detect if we are completing an option (last token starts with '-')
        std::string lastTok = parts.back();
        if (!lastTok.empty() && lastTok[0] == '-')
        {
          std::vector<std::string> matches;
          for (const auto &o : opts)
          {
            if (starts_with(o, lastTok))
              matches.push_back(o);
          }
          normalize_list(matches);

          if (matches.size() == 1)
          {
            // Replace last token
            // naive rebuild:
            std::string rebuilt;
            for (size_t i = 0; i + 1 < parts.size(); ++i)
            {
              rebuilt += parts[i];
              rebuilt += " ";
            }
            rebuilt += matches[0];
            rebuilt += " ";
            r.changed = true;
            r.newLine = rebuilt;
            return r;
          }

          if (matches.size() > 1)
          {
            r.suggestions = matches;
            return r;
          }

          return r;
        }

        // If user pressed TAB right after "cmd " show options list
        if (trimmed.size() >= cmd.size() + 1 && trimmed == (cmd + " "))
        {
          if (!opts.empty())
          {
            r.suggestions = opts;
            normalize_list(r.suggestions);
            return r;
          }
        }

        return r;
      };

      vix::cli::repl::ReadStatus st =
          vix::cli::repl::read_line_edit(prompt, line, completer, onHistoryUp, onHistoryDown);

      if (st == vix::cli::repl::ReadStatus::Interrupted)
        break;
      if (st == vix::cli::repl::ReadStatus::Eof)
        break;
      if (st == vix::cli::repl::ReadStatus::Clear)
      {
        vix::cli::repl::clear_screen();
        if (cfg.showBannerOnClear)
          print_banner();
        continue;
      }

      // Enter pressed -> reset browse state
      reset_history_browse();

      line = vix::cli::repl::trim_copy(line);

      if (line.empty())
        continue;

      // History
      history.add(line);

      // VARIABLES: x = 42, x = "hi", x = true, x = null, x = 1+2, x = {"a":1}, x = [1,2]
      if (auto asg = parse_assignment(line))
      {
        const std::string &name = asg->first;
        const std::string &rhs = asg->second;

        // JSON literal first
        if (looks_like_json_literal(rhs))
        {
          try
          {
            nlohmann::json j = nlohmann::json::parse(rhs);
            vars[name] = j;
            std::cout << name << " = " << j.dump() << "\n";
          }
          catch (const std::exception &e)
          {
            std::cout << "error: " << e.what() << "\n";

            auto t = vix::cli::repl::trim_copy(rhs);
            if (!t.empty() && t.front() == '{')
            {
              if (t.find("\",") != std::string::npos && t.find("\":") == std::string::npos)
              {
                std::cout << "hint: JSON object uses ':' between key and value.\n";
                std::cout << "      example: {\"name\":\"Gaspard\",\"age\":10}\n";
              }
            }
          }

          continue;
        }

        // simple literal: string, int, double, bool, null, typed literal
        {
          nlohmann::json parsed;
          if (try_parse_simple_literal(rhs, parsed))
          {
            vars[name] = parsed;
            std::cout << name << " = " << vars[name].dump() << "\n";
            continue;
          }
        }

        // forbid code chars for non-literal / non-json expressions
        if (contains_forbidden_code_chars(rhs))
        {
          std::cout << "error: invalid expression\n";
          continue;
        }

        // math expression
        std::string err;
        std::string formatted;
        nlohmann::json val;

        if (!try_eval_math_with_vars(rhs, vars, val, formatted, err))
        {
          std::cout << "error: " << (err.empty() ? "invalid expression" : err) << "\n";
          continue;
        }

        vars[name] = val;
        std::cout << formatted << "\n";
        continue;
      }

      // API CALL PARSER: println("hi"), Vix.cd(".."), etc.
      if (vix::cli::repl::api::looks_like_call(line))
      {
        auto pr = vix::cli::repl::api::parse_call(line);
        if (pr.ok)
        {
          if (invoke_call(pr.expr, VixObj, vars))
          {
            // if user requested exit via Vix.exit(...)
            if (VixObj.exit_requested())
              break;
            continue;
          }
          // parsed call but unknown function/member
          if (!pr.expr.object.empty())
            std::cout << "Unknown call: " << pr.expr.object << "." << pr.expr.member << "(...)\n";
          else
            std::cout << "Unknown call: " << pr.expr.callee << "(...)\n";
          continue;
        }
        else
        {
          std::cout << "error: " << pr.error << "\n";
          continue;
        }
      }

#else
      std::cout << prompt << std::flush;
      if (!std::getline(std::cin, line))
      {
        std::cout << "\n";
        break;
      }
      line = vix::cli::repl::trim_copy(line);
#endif

      // Single identifier => print var
      {
        std::string t = vix::cli::repl::trim_copy(line);
        if (!t.empty() && is_ident_start(t.front()))
        {
          bool ok = true;
          for (char c : t)
          {
            if (!is_ident_char(c))
            {
              ok = false;
              break;
            }
          }

          if (ok)
          {
            auto it = vars.find(t);
            if (it != vars.end())
            {
              std::cout << format_json_scalar(it->second) << "\n";
              continue;
            }
          }
        }
      }

      // Direct path access => user.name / nums[0] / user.tags[1]
      if (try_print_resolved_path_expr(line, vars))
        continue;

      if (line == "exit" || line == ".exit") // keep legacy alias
        break;

      if (line == "help" || vix::cli::repl::starts_with(line, "help ") ||
          line == ".help" || vix::cli::repl::starts_with(line, ".help "))
      {
        auto normalized = line;
        if (vix::cli::repl::starts_with(normalized, ".help"))
          normalized.erase(0, 1);

        auto parts = vix::cli::repl::split_command_line(normalized);

        if (parts.size() == 1)
        {
          print_help();
          continue;
        }

        std::cout << "error: 'help <command>' is disabled in REPL. Use: vix help <command>\n";
        continue;
      }

      if (line == "version" || line == ".version")
      {
        std::cout << "Vix.cpp " << VIX_CLI_VERSION << "\n";
        continue;
      }

      if (line == "pwd" || line == ".pwd")
      {
        std::cout << fs::current_path().string() << "\n";
        continue;
      }

      if (line == "clear" || line == ".clear")
      {
        vix::cli::repl::clear_screen();
        if (cfg.showBannerOnClear)
          print_banner();
        continue;
      }

      if (line == "history" || line == ".history")
      {
        const auto &items = history.items();
        for (std::size_t i = 0; i < items.size(); ++i)
          std::cout << (i + 1) << "  " << items[i] << "\n";
        continue;
      }

      if (line == "history clear" || line == ".history clear")
      {
        history.clear();
        std::cout << "history cleared\n";
        continue;
      }

      if (line == "commands" || line == ".commands")
      {
        std::cout << "error: CLI commands list is disabled in REPL.\n";
        continue;
      }

      if (vix::cli::repl::starts_with(line, "cd ") || vix::cli::repl::starts_with(line, ".cd "))
      {
        auto normalized = line;
        if (vix::cli::repl::starts_with(normalized, ".cd"))
          normalized.erase(0, 1);

        auto parts = vix::cli::repl::split_command_line(normalized);
        if (parts.size() < 2)
        {
          std::cout << "usage: cd <dir>\n";
          continue;
        }

        std::error_code ec;
        fs::current_path(parts[1], ec);
        if (ec)
          std::cout << "cd: " << ec.message() << "\n";
        continue;
      }

      if (cfg.enableCalculator && (vix::cli::repl::starts_with(line, "calc ") || vix::cli::repl::starts_with(line, ".calc ")))
      {
        auto expr = line;
        if (vix::cli::repl::starts_with(expr, ".calc"))
          expr.erase(0, 1); // remove dot
        expr = strip_prefix(expr, "calc");

        if (expr.empty())
        {
          std::cout << "usage: calc <expr>\n";
          continue;
        }

        std::string err;
        auto r = vix::cli::repl::eval_expression(expr, err);
        if (!r)
        {
          std::cout << "calc error: " << err << "\n";
          continue;
        }

        std::cout << r->formatted << "\n";
        continue;
      }

      auto parts = vix::cli::repl::split_command_line(line);
      if (parts.empty())
        continue;

      const std::string cmd = parts[0];
      std::vector<std::string> args(parts.begin() + 1, parts.end());

      // Not a known command -> try math directly (no '=' needed)
      if (cfg.enableCalculator && !contains_forbidden_code_chars(line) &&
          (contains_math_ops_anywhere(line)))
      {
        std::string err;
        std::string formatted;
        nlohmann::json val;

        if (try_eval_math_with_vars(line, vars, val, formatted, err))
        {
          std::cout << formatted << "\n";
          continue;
        }

        if (!err.empty())
        {
          std::cout << "error: " << err << "\n";
          continue;
        }
      }

      std::cout << "Unknown command: " << cmd << " (type help)\n";
    }

    // save history on exit
    if (cfg.enableFileHistory)
    {
      std::string err;
      if (!history.saveToFile(cfg.historyFile, &err))
        std::cout << "warning: " << err << "\n";
    }

    return VixObj.exit_requested() ? VixObj.exit_code() : 0;
  }
} // namespace vix::commands::ReplCommand
