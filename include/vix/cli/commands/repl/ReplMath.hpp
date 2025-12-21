#pragma once
#include <string>
#include <optional>

namespace vix::cli::repl
{
    struct CalcResult
    {
        double value = 0.0;
        std::string formatted; // pretty
    };

    // Evaluate arithmetic expression:
    // + - * / % parentheses
    // unary +/-
    // numbers: int or float
    // returns error message on failure
    std::optional<CalcResult> eval_expression(const std::string &expr, std::string &error);
}
