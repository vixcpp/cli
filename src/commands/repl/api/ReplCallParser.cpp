#include <vix/cli/commands/repl/api/ReplCallParser.hpp>

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
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
            End
        };

        struct Tok
        {
            TokType type = TokType::End;
            std::string text;
        };

        struct Lexer
        {
            std::string_view s;
            size_t i = 0;

            explicit Lexer(std::string_view in) : s(in) {}

            static bool is_ident_start(char c)
            {
                return std::isalpha((unsigned char)c) || c == '_';
            }
            static bool is_ident_char(char c)
            {
                return std::isalnum((unsigned char)c) || c == '_';
            }

            void skip_ws()
            {
                while (i < s.size() && std::isspace((unsigned char)s[i]))
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
                    while (i < s.size())
                    {
                        char ch = s[i++];
                        if (ch == quote)
                            break;
                        if (ch == '\\' && i < s.size())
                        {
                            char esc = s[i++];
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
                            default:
                                out.push_back(esc);
                                break;
                            }
                            continue;
                        }
                        out.push_back(ch);
                    }
                    return {TokType::String, out};
                }

                // number
                if (std::isdigit((unsigned char)c) || c == '.')
                {
                    size_t start = i;
                    bool dot = (c == '.');
                    ++i;
                    while (i < s.size())
                    {
                        char ch = s[i];
                        if (std::isdigit((unsigned char)ch))
                        {
                            ++i;
                            continue;
                        }
                        if (ch == '.' && !dot)
                        {
                            dot = true;
                            ++i;
                            continue;
                        }
                        break;
                    }
                    return {TokType::Number, std::string(s.substr(start, i - start))};
                }

                // identifier
                if (is_ident_start(c))
                {
                    size_t start = i++;
                    while (i < s.size() && is_ident_char(s[i]))
                        ++i;
                    return {TokType::Ident, std::string(s.substr(start, i - start))};
                }

                // unknown char
                ++i;
                return {TokType::End, ""};
            }
        };

        static inline std::string trim_copy(std::string_view sv)
        {
            size_t b = 0;
            while (b < sv.size() && std::isspace((unsigned char)sv[b]))
                ++b;

            size_t e = sv.size();
            while (e > b && std::isspace((unsigned char)sv[e - 1]))
                --e;

            return std::string(sv.substr(b, e - b));
        }

        // Read a raw argument slice from the ORIGINAL INPUT, until ',' or ')'
        // while respecting:
        //  - quotes ( "..." or '...' ) with escapes
        //  - nested parentheses: ( ... ) inside the arg
        // This lets us support print(1+2), println(3*(2+1)), etc.
        static bool read_raw_arg_slice(std::string_view input, size_t &pos, std::string &out, std::string &err)
        {
            // pos is currently at the beginning of the arg (may have whitespace)
            const auto n = input.size();

            auto skip_ws = [&]()
            {
                while (pos < n && std::isspace((unsigned char)input[pos]))
                    ++pos;
            };

            skip_ws();
            if (pos >= n)
            {
                err = "expected value";
                return false;
            }

            size_t start = pos;

            int parenDepth = 0;
            bool inQuote = false;
            char quote = 0;

            while (pos < n)
            {
                char c = input[pos];

                if (inQuote)
                {
                    // handle escapes inside quotes
                    if (c == '\\' && (pos + 1) < n)
                    {
                        pos += 2;
                        continue;
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

                // entering quote
                if (c == '"' || c == '\'')
                {
                    inQuote = true;
                    quote = c;
                    ++pos;
                    continue;
                }

                // nested parentheses inside arg
                if (c == '(')
                {
                    ++parenDepth;
                    ++pos;
                    continue;
                }
                if (c == ')')
                {
                    if (parenDepth == 0)
                        break; // end of arg list, do not consume ')'
                    --parenDepth;
                    ++pos;
                    continue;
                }

                // arg separator only if not nested
                if (c == ',' && parenDepth == 0)
                    break;

                ++pos;
            }

            size_t end = pos;
            out = trim_copy(input.substr(start, end - start));

            if (out.empty())
            {
                err = "expected value";
                return false;
            }

            return true;
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
            if (cur.type == TokType::String)
            {
                out.v = cur.text;
                cur = lx.next();
                return true;
            }

            if (cur.type == TokType::Number)
            {
                const std::string &t = cur.text;
                bool hasDot = (t.find('.') != std::string::npos);
                try
                {
                    if (hasDot)
                        out.v = std::stod(t);
                    else
                        out.v = (long long)std::stoll(t);
                }
                catch (...)
                {
                    err = "invalid number";
                    return false;
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
                    err = "unexpected identifier '" + cur.text + "' (only true/false/null allowed as bare identifiers)";
                    return false;
                }
                out = lit;
                cur = lx.next();
                return true;
            }

            err = "expected value (string/number/true/false/null)";
            return false;
        }

    } // namespace

    bool looks_like_call(std::string_view input)
    {
        size_t lp = input.find('(');
        size_t rp = input.rfind(')');
        if (lp == std::string_view::npos || rp == std::string_view::npos || rp < lp)
            return false;

        for (size_t i = 0; i < lp; ++i)
        {
            if (!std::isspace((unsigned char)input[i]))
                return true;
        }
        return false;
    }

    CallParseResult parse_call(std::string_view input)
    {
        CallParseResult res;
        Lexer lx(input);

        Tok cur = lx.next();
        if (cur.type != TokType::Ident)
        {
            res.ok = false;
            res.error = "call must start with identifier";
            return res;
        }

        std::string first = cur.text;
        cur = lx.next();

        // Optional: Obj.member(...)
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

        // Find the '(' in the original input starting from where we are.
        // We’ll set rawPos right after '(' and then read slices until ')' / ','.
        size_t lp = input.find('(');
        if (lp == std::string_view::npos)
        {
            res.ok = false;
            res.error = "expected '('";
            return res;
        }
        size_t rawPos = lp + 1;

        cur = lx.next();

        // Handle empty args: f()
        if (cur.type == TokType::RParen)
        {
            cur = lx.next();
            res.ok = true;
            return res;
        }

        // Parse args in a loop, but we will read raw slices ourselves.
        while (true)
        {
            // 1) read raw slice for this argument
            std::string raw;
            std::string rawErr;
            size_t savedPos = rawPos;

            if (!read_raw_arg_slice(input, rawPos, raw, rawErr))
            {
                res.ok = false;
                res.error = rawErr;
                return res;
            }

            // 2) Try parse_value from tokens.
            // For expressions like "1+2", parse_value will fail because cur sees Number then next token is End.
            // We'll accept that and store a placeholder. invoke_call will evaluate using args_raw.
            CallValue v;
            std::string parseErr;
            size_t before = lx.i;
            Tok savedTok = cur;

            bool ok = parse_value(lx, cur, v, parseErr);

            // If parse_value failed, restore lexer/token state and use null placeholder.
            if (!ok)
            {
                lx.i = before;
                cur = savedTok;
                v.v = std::monostate{};
                lx.i = savedPos; // reset to start of raw to rescan
                // Move lexer cursor forward to current rawPos
                lx.i = rawPos;
                cur = lx.next(); // re-sync
            }

            res.expr.args.push_back(std::move(v));
            res.expr.args_raw.push_back(raw);

            // 3) Now rawPos is at ',' or ')' or end.
            // Skip whitespace
            while (rawPos < input.size() && std::isspace((unsigned char)input[rawPos]))
                ++rawPos;

            if (rawPos >= input.size())
            {
                res.ok = false;
                res.error = "expected ')'";
                return res;
            }

            if (input[rawPos] == ',')
            {
                ++rawPos; // consume ','
                // Advance token stream if needed
                if (cur.type == TokType::Comma)
                    cur = lx.next();
                else
                    cur = lx.next();
                continue;
            }

            if (input[rawPos] == ')')
            {
                ++rawPos; // consume ')'
                // Advance token stream if needed
                if (cur.type == TokType::RParen)
                    cur = lx.next();
                res.ok = true;
                return res;
            }

            res.ok = false;
            res.error = "expected ',' or ')'";
            return res;
        }
    }
}
