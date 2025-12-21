#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vix::cli::repl::api
{
    struct CallValue
    {
        using Value = std::variant<std::monostate, double, long long, bool, std::string>;
        Value v;

        bool is_null() const { return std::holds_alternative<std::monostate>(v); }
        bool is_string() const { return std::holds_alternative<std::string>(v); }
        bool is_int() const { return std::holds_alternative<long long>(v); }
        bool is_double() const { return std::holds_alternative<double>(v); }
        bool is_bool() const { return std::holds_alternative<bool>(v); }

        const std::string &as_string() const { return std::get<std::string>(v); }
        long long as_int() const { return std::get<long long>(v); }
        double as_double() const { return std::get<double>(v); }
        bool as_bool() const { return std::get<bool>(v); }
    };

    struct CallExpr
    {
        // examples:
        //   println("hi") -> callee="println"
        //   Vix.cd("..")  -> object="Vix", member="cd"
        std::string object; // empty if global
        std::string member; // for object calls
        std::string callee; // for global calls
        std::vector<CallValue> args;
        std::vector<std::string> args_raw;
    };

    struct CallParseResult
    {
        bool ok = false;
        CallExpr expr;
        std::string error;
    };

    // Parses:
    //   name(arg1, arg2, ...)
    //   Obj.name(arg1, ...)
    // Supported literals:
    //   "string", 'string'
    //   123, 12.34
    //   true/false/null
    CallParseResult parse_call(std::string_view input);

    // Helper: detect if input looks like a call at all.
    bool looks_like_call(std::string_view input);
}
