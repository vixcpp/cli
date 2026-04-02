/**
 *
 *  @file ReplCallParser.cpp
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
#include <vix/cli/commands/repl/api/ReplCallParser.hpp>

#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vix::cli::repl::api
{
  namespace
  {
    enum class TokType
    {
      Ident,
      Number,
      String,
      LParen,
      RParen,
      Comma,
      Dot,
      End,
      Invalid
    };

    struct Tok
    {
      TokType type = TokType::End;
      std::string text;
    };

    static bool is_ident_start(char c)
    {
      const unsigned char u = static_cast<unsigned char>(c);
      return std::isalpha(u) || c == '_';
    }

    static bool is_ident_char(char c)
    {
      const unsigned char u = static_cast<unsigned char>(c);
      return std::isalnum(u) || c == '_';
    }

    static std::string trim_copy(std::string_view sv)
    {
      size_t b = 0;
      while (b < sv.size() && std::isspace(static_cast<unsigned char>(sv[b])))
        ++b;

      size_t e = sv.size();
      while (e > b && std::isspace(static_cast<unsigned char>(sv[e - 1])))
        --e;

      return std::string(sv.substr(b, e - b));
    }

    struct Lexer
    {
      std::string_view s;
      size_t i = 0;

      explicit Lexer(std::string_view in) : s(in) {}

      void skip_ws()
      {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
          ++i;
      }

      Tok next()
      {
        skip_ws();

        if (i >= s.size())
          return {TokType::End, ""};

        const char c = s[i];

        if (c == '(')
        {
          ++i;
          return {TokType::LParen, "("};
        }

        if (c == ')')
        {
          ++i;
          return {TokType::RParen, ")"};
        }

        if (c == ',')
        {
          ++i;
          return {TokType::Comma, ","};
        }

        if (c == '.')
        {
          ++i;
          return {TokType::Dot, "."};
        }

        // string literal
        if (c == '"' || c == '\'')
        {
          const char quote = c;
          ++i;

          std::string out;
          bool closed = false;

          while (i < s.size())
          {
            char ch = s[i++];

            if (ch == quote)
            {
              closed = true;
              break;
            }

            if (ch == '\\')
            {
              if (i >= s.size())
                return {TokType::Invalid, "unterminated escape sequence"};

              const char esc = s[i++];

              switch (esc)
              {
              case 'n':
                out.push_back('\n');
                break;
              case 't':
                out.push_back('\t');
                break;
              case 'r':
                out.push_back('\r');
                break;
              case '\\':
                out.push_back('\\');
                break;
              case '"':
                out.push_back('"');
                break;
              case '\'':
                out.push_back('\'');
                break;
              case '0':
                out.push_back('\0');
                break;
              default:
                out.push_back(esc);
                break;
              }

              continue;
            }

            out.push_back(ch);
          }

          if (!closed)
            return {TokType::Invalid, "unterminated string literal"};

          return {TokType::String, out};
        }

        // number: optional leading sign, then digits, optional single dot
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            ((c == '+' || c == '-') && (i + 1) < s.size() &&
             (std::isdigit(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == '.')) ||
            (c == '.' && (i + 1) < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))))
        {
          const size_t start = i;

          if (s[i] == '+' || s[i] == '-')
            ++i;

          bool hasDot = false;
          bool hasDigit = false;

          while (i < s.size())
          {
            const char ch = s[i];

            if (std::isdigit(static_cast<unsigned char>(ch)))
            {
              hasDigit = true;
              ++i;
              continue;
            }

            if (ch == '.' && !hasDot)
            {
              hasDot = true;
              ++i;
              continue;
            }

            break;
          }

          if (!hasDigit)
            return {TokType::Invalid, "invalid number"};

          return {TokType::Number, std::string(s.substr(start, i - start))};
        }

        // identifier
        if (is_ident_start(c))
        {
          const size_t start = i++;
          while (i < s.size() && is_ident_char(s[i]))
            ++i;

          return {TokType::Ident, std::string(s.substr(start, i - start))};
        }

        ++i;
        return {TokType::Invalid, std::string("unexpected character: '") + c + "'"};
      }
    };

    static bool parse_int_strict(const std::string &s, long long &out)
    {
      try
      {
        size_t idx = 0;
        const long long v = std::stoll(s, &idx, 10);
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

    static bool parse_double_strict(const std::string &s, double &out)
    {
      try
      {
        size_t idx = 0;
        const double v = std::stod(s, &idx);
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

    static CallValue make_literal_from_ident(const std::string &ident, bool *ok)
    {
      *ok = true;

      if (ident == "true")
        return {true};

      if (ident == "false")
        return {false};

      if (ident == "null")
        return {std::monostate{}};

      *ok = false;
      return {std::monostate{}};
    }

    static bool parse_value(Lexer &lx, Tok &cur, CallValue &out, std::string &err)
    {
      if (cur.type == TokType::Invalid)
      {
        err = cur.text.empty() ? "invalid token" : cur.text;
        return false;
      }

      if (cur.type == TokType::String)
      {
        out.v = cur.text;
        cur = lx.next();
        return true;
      }

      if (cur.type == TokType::Number)
      {
        const std::string &t = cur.text;
        const bool hasDot = (t.find('.') != std::string::npos);

        if (hasDot)
        {
          double v = 0.0;
          if (!parse_double_strict(t, v))
          {
            err = "invalid number";
            return false;
          }
          out.v = v;
        }
        else
        {
          long long v = 0;
          if (!parse_int_strict(t, v))
          {
            err = "invalid integer";
            return false;
          }
          out.v = v;
        }

        cur = lx.next();
        return true;
      }

      if (cur.type == TokType::Ident)
      {
        bool ok = false;
        CallValue lit = make_literal_from_ident(cur.text, &ok);
        if (!ok)
        {
          err = "unexpected identifier '" + cur.text + "'";
          return false;
        }

        out = std::move(lit);
        cur = lx.next();
        return true;
      }

      err = "expected value";
      return false;
    }

    static bool read_raw_arg_slice(
        std::string_view input,
        size_t &pos,
        std::string &out,
        std::string &err)
    {
      const size_t n = input.size();

      auto skip_ws = [&]()
      {
        while (pos < n && std::isspace(static_cast<unsigned char>(input[pos])))
          ++pos;
      };

      skip_ws();

      if (pos >= n)
      {
        err = "expected value";
        return false;
      }

      const size_t start = pos;

      int parenDepth = 0;
      bool inQuote = false;
      char quote = 0;

      while (pos < n)
      {
        const char c = input[pos];

        if (inQuote)
        {
          if (c == '\\')
          {
            if ((pos + 1) < n)
            {
              pos += 2;
              continue;
            }

            err = "unterminated escape sequence";
            return false;
          }

          if (c == quote)
          {
            inQuote = false;
            ++pos;
            continue;
          }

          ++pos;
          continue;
        }

        if (c == '"' || c == '\'')
        {
          inQuote = true;
          quote = c;
          ++pos;
          continue;
        }

        if (c == '(')
        {
          ++parenDepth;
          ++pos;
          continue;
        }

        if (c == ')')
        {
          if (parenDepth == 0)
            break;

          --parenDepth;
          ++pos;
          continue;
        }

        if (c == ',' && parenDepth == 0)
          break;

        ++pos;
      }

      if (inQuote)
      {
        err = "unterminated string literal";
        return false;
      }

      if (parenDepth != 0)
      {
        err = "unbalanced parentheses in argument";
        return false;
      }

      out = trim_copy(input.substr(start, pos - start));

      if (out.empty())
      {
        err = "expected value";
        return false;
      }

      return true;
    }

    static std::optional<std::pair<std::string, std::string>> split_typed_call(std::string_view raw)
    {
      raw = std::string_view(raw.data(), raw.size());
      raw = std::string_view(trim_copy(raw));

      if (raw.empty() || raw.back() != ')')
        return std::nullopt;

      const size_t lp = raw.find('(');
      if (lp == std::string_view::npos || lp == 0)
        return std::nullopt;

      const std::string type = trim_copy(raw.substr(0, lp));
      const std::string inner = trim_copy(raw.substr(lp + 1, raw.size() - lp - 2));

      if (type.empty() || inner.empty())
        return std::nullopt;

      return std::make_pair(type, inner);
    }

    static bool parse_cpp_typed_literal(const std::string &raw, CallValue &out, std::string &err)
    {
      const auto typed = split_typed_call(raw);
      if (!typed)
        return false;

      const std::string &type = typed->first;
      const std::string &inner = typed->second;

      auto parse_inner_as_value = [&](CallValue &dst) -> bool
      {
        Lexer innerLx(inner);
        Tok innerCur = innerLx.next();

        if (!parse_value(innerLx, innerCur, dst, err))
          return false;

        if (innerCur.type != TokType::End)
        {
          err = "invalid typed literal";
          return false;
        }

        return true;
      };

      if (type == "int" || type == "long" || type == "long long")
      {
        CallValue tmp;
        if (!parse_inner_as_value(tmp))
          return false;

        if (tmp.is_int())
        {
          out.v = tmp.as_int();
          return true;
        }

        if (tmp.is_double())
        {
          const double d = tmp.as_double();
          if (d < static_cast<double>(std::numeric_limits<long long>::min()) ||
              d > static_cast<double>(std::numeric_limits<long long>::max()))
          {
            err = type + " out of range";
            return false;
          }

          out.v = static_cast<long long>(d);
          return true;
        }

        err = type + " expects a numeric value";
        return false;
      }

      if (type == "double" || type == "float")
      {
        CallValue tmp;
        if (!parse_inner_as_value(tmp))
          return false;

        if (tmp.is_double())
        {
          out.v = tmp.as_double();
          return true;
        }

        if (tmp.is_int())
        {
          out.v = static_cast<double>(tmp.as_int());
          return true;
        }

        err = type + " expects a numeric value";
        return false;
      }

      if (type == "bool")
      {
        CallValue tmp;
        if (!parse_inner_as_value(tmp))
          return false;

        if (!tmp.is_bool())
        {
          err = "bool expects true or false";
          return false;
        }

        out.v = tmp.as_bool();
        return true;
      }

      if (type == "string" || type == "std::string")
      {
        CallValue tmp;
        if (!parse_inner_as_value(tmp))
          return false;

        if (!tmp.is_string())
        {
          err = type + " expects a string literal";
          return false;
        }

        out.v = tmp.as_string();
        return true;
      }

      return false;
    }

    static bool parse_single_arg_value_from_raw(const std::string &raw, CallValue &out, std::string &err)
    {
      Lexer lx(raw);
      Tok cur = lx.next();

      std::string firstErr;
      if (parse_value(lx, cur, out, firstErr) && cur.type == TokType::End)
        return true;

      std::string typedErr;
      if (parse_cpp_typed_literal(raw, out, typedErr))
        return true;

      if (!typedErr.empty())
        err = typedErr;
      else if (!firstErr.empty())
        err = firstErr;
      else
        err = "invalid value";

      return false;
    }

    static bool validate_no_trailing_tokens(Lexer &lx, Tok &cur, std::string &err)
    {
      if (cur.type == TokType::Invalid)
      {
        err = cur.text.empty() ? "invalid token" : cur.text;
        return false;
      }

      while (cur.type != TokType::End)
      {
        if (cur.type == TokType::Invalid)
        {
          err = cur.text.empty() ? "invalid token" : cur.text;
          return false;
        }

        err = "unexpected trailing input";
        return false;
      }

      return true;
    }
  }

  bool looks_like_call(std::string_view input)
  {
    const size_t lp = input.find('(');
    const size_t rp = input.rfind(')');

    if (lp == std::string_view::npos || rp == std::string_view::npos || rp < lp)
      return false;

    for (size_t i = 0; i < lp; ++i)
    {
      if (!std::isspace(static_cast<unsigned char>(input[i])))
        return true;
    }

    return false;
  }

  CallParseResult parse_call(std::string_view input)
  {
    CallParseResult res;
    Lexer lx(input);

    Tok cur = lx.next();

    if (cur.type == TokType::Invalid)
    {
      res.ok = false;
      res.error = cur.text.empty() ? "invalid token" : cur.text;
      return res;
    }

    if (cur.type != TokType::Ident)
    {
      res.ok = false;
      res.error = "call must start with identifier";
      return res;
    }

    const std::string first = cur.text;
    cur = lx.next();

    if (cur.type == TokType::Dot)
    {
      cur = lx.next();

      if (cur.type != TokType::Ident)
      {
        res.ok = false;
        res.error = "expected member name after '.'";
        return res;
      }

      res.expr.object = first;
      res.expr.member = cur.text;
      cur = lx.next();
    }
    else
    {
      res.expr.callee = first;
    }

    if (cur.type != TokType::LParen)
    {
      res.ok = false;
      res.error = "expected '('";
      return res;
    }

    const size_t lp = input.find('(');
    if (lp == std::string_view::npos)
    {
      res.ok = false;
      res.error = "expected '('";
      return res;
    }

    size_t rawPos = lp + 1;
    cur = lx.next();

    if (cur.type == TokType::RParen)
    {
      cur = lx.next();

      if (!validate_no_trailing_tokens(lx, cur, res.error))
      {
        res.ok = false;
        return res;
      }

      res.ok = true;
      return res;
    }

    while (true)
    {
      std::string raw;
      std::string rawErr;

      if (!read_raw_arg_slice(input, rawPos, raw, rawErr))
      {
        res.ok = false;
        res.error = rawErr;
        return res;
      }

      CallValue value;
      std::string parseErr;

      if (!parse_single_arg_value_from_raw(raw, value, parseErr))
      {
        value.v = std::monostate{};
      }

      res.expr.args.push_back(std::move(value));
      res.expr.args_raw.push_back(std::move(raw));

      while (rawPos < input.size() && std::isspace(static_cast<unsigned char>(input[rawPos])))
        ++rawPos;

      if (rawPos >= input.size())
      {
        res.ok = false;
        res.error = "expected ')'";
        return res;
      }

      if (input[rawPos] == ',')
      {
        ++rawPos;
        continue;
      }

      if (input[rawPos] == ')')
      {
        ++rawPos;
        break;
      }

      res.ok = false;
      res.error = "expected ',' or ')'";
      return res;
    }

    Lexer tailLx(input.substr(rawPos));
    Tok tail = tailLx.next();
    if (tail.type != TokType::End)
    {
      res.ok = false;
      res.error = (tail.type == TokType::Invalid && !tail.text.empty())
                      ? tail.text
                      : "unexpected trailing input after call";
      return res;
    }

    res.ok = true;
    return res;
  }
}
