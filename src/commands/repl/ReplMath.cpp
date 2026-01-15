/**
 *
 *  @file RelpMath.cpp
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
#include <vix/cli/commands/repl/ReplMath.hpp>

#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

namespace vix::cli::repl
{
  namespace
  {
    enum class TokType
    {
      Num,
      Op,
      LParen,
      RParen
    };

    struct Tok
    {
      TokType type;
      double num = 0.0;
      char op = 0;
    };

    static bool is_op(char c)
    {
      return c == '+' || c == '-' || c == '*' || c == '/' || c == '%';
    }

    static int prec(char op)
    {
      if (op == '+' || op == '-')
        return 1;
      if (op == '*' || op == '/' || op == '%')
        return 2;
      return 0;
    }

    static bool tokenize(const std::string &s, std::vector<Tok> &out, std::string &err)
    {
      out.clear();
      size_t i = 0;

      auto skip_ws = [&]()
      {
        while (i < s.size() && std::isspace((unsigned char)s[i]))
          ++i;
      };

      // unary handling: if we expect a number and see '+'/'-', treat as unary sign
      bool expectValue = true;

      while (true)
      {
        skip_ws();
        if (i >= s.size())
          break;

        char c = s[i];

        if (c == '(')
        {
          out.push_back({TokType::LParen});
          ++i;
          expectValue = true;
          continue;
        }

        if (c == ')')
        {
          out.push_back({TokType::RParen});
          ++i;
          expectValue = false;
          continue;
        }

        if (is_op(c))
        {
          // unary +/-
          if (expectValue && (c == '+' || c == '-'))
          {
            // parse signed number after unary
            char sign = c;
            ++i;
            skip_ws();
            if (i >= s.size())
            {
              err = "dangling unary operator";
              return false;
            }

            // if next is '(' then treat as 0 +/- ( ... )
            if (s[i] == '(')
            {
              out.push_back({TokType::Num, 0.0, 0});
              out.push_back({TokType::Op, 0.0, sign});
              expectValue = true;
              continue;
            }

            // parse number with sign
            size_t start = i;
            bool dot = false;
            while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.'))
            {
              if (s[i] == '.')
              {
                if (dot)
                  break;
                dot = true;
              }
              ++i;
            }

            if (start == i)
            {
              err = "expected number after unary operator";
              return false;
            }

            double v = 0.0;
            try
            {
              v = std::stod(s.substr(start, i - start));
            }
            catch (...)
            {
              err = "invalid number";
              return false;
            }

            if (sign == '-')
              v = -v;
            out.push_back({TokType::Num, v, 0});
            expectValue = false;
            continue;
          }

          // binary op
          out.push_back({TokType::Op, 0.0, c});
          ++i;
          expectValue = true;
          continue;
        }

        // number
        if (std::isdigit((unsigned char)c) || c == '.')
        {
          size_t start = i;
          bool dot = false;
          while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.'))
          {
            if (s[i] == '.')
            {
              if (dot)
                break;
              dot = true;
            }
            ++i;
          }

          double v = 0.0;
          try
          {
            v = std::stod(s.substr(start, i - start));
          }
          catch (...)
          {
            err = "invalid number";
            return false;
          }

          out.push_back({TokType::Num, v, 0});
          expectValue = false;
          continue;
        }

        err = std::string("unexpected character: '") + c + "'";
        return false;
      }

      return true;
    }

    static bool to_rpn(const std::vector<Tok> &in, std::vector<Tok> &out, std::string &err)
    {
      out.clear();
      std::vector<Tok> ops;

      for (const auto &t : in)
      {
        if (t.type == TokType::Num)
        {
          out.push_back(t);
          continue;
        }

        if (t.type == TokType::Op)
        {
          while (!ops.empty())
          {
            const auto &top = ops.back();
            if (top.type == TokType::Op && prec(top.op) >= prec(t.op))
            {
              out.push_back(top);
              ops.pop_back();
              continue;
            }
            break;
          }
          ops.push_back(t);
          continue;
        }

        if (t.type == TokType::LParen)
        {
          ops.push_back(t);
          continue;
        }

        if (t.type == TokType::RParen)
        {
          bool matched = false;
          while (!ops.empty())
          {
            auto top = ops.back();
            ops.pop_back();
            if (top.type == TokType::LParen)
            {
              matched = true;
              break;
            }
            out.push_back(top);
          }
          if (!matched)
          {
            err = "mismatched ')'";
            return false;
          }
          continue;
        }
      }

      while (!ops.empty())
      {
        auto top = ops.back();
        ops.pop_back();
        if (top.type == TokType::LParen)
        {
          err = "mismatched '('";
          return false;
        }
        out.push_back(top);
      }

      return true;
    }

    static bool eval_rpn(const std::vector<Tok> &rpn, double &result, std::string &err)
    {
      std::vector<double> st;

      for (const auto &t : rpn)
      {
        if (t.type == TokType::Num)
        {
          st.push_back(t.num);
          continue;
        }

        if (t.type == TokType::Op)
        {
          if (st.size() < 2)
          {
            err = "invalid expression";
            return false;
          }

          double b = st.back();
          st.pop_back();
          double a = st.back();
          st.pop_back();

          switch (t.op)
          {
          case '+':
            st.push_back(a + b);
            break;
          case '-':
            st.push_back(a - b);
            break;
          case '*':
            st.push_back(a * b);
            break;
          case '/':
            if (b == 0.0)
            {
              err = "division by zero";
              return false;
            }
            st.push_back(a / b);
            break;
          case '%':
            if (b == 0.0)
            {
              err = "mod by zero";
              return false;
            }
            st.push_back(std::fmod(a, b));
            break;
          default:
            err = "unknown operator";
            return false;
          }
          continue;
        }

        err = "invalid token";
        return false;
      }

      if (st.size() != 1)
      {
        err = "invalid expression";
        return false;
      }

      result = st[0];
      return true;
    }

    static std::string format_double(double v)
    {
      std::ostringstream oss;
      oss.setf(std::ios::fixed);
      oss.precision(12);
      oss << v;

      // trim trailing zeros
      std::string s = oss.str();
      while (s.size() > 1 && s.find('.') != std::string::npos && s.back() == '0')
        s.pop_back();
      if (!s.empty() && s.back() == '.')
        s.pop_back();
      return s;
    }
  }

  std::optional<CalcResult> eval_expression(const std::string &expr, std::string &error)
  {
    std::vector<Tok> toks;
    if (!tokenize(expr, toks, error))
      return std::nullopt;

    std::vector<Tok> rpn;
    if (!to_rpn(toks, rpn, error))
      return std::nullopt;

    double out = 0.0;
    if (!eval_rpn(rpn, out, error))
      return std::nullopt;

    CalcResult r;
    r.value = out;
    r.formatted = format_double(out);
    return r;
  }
}
