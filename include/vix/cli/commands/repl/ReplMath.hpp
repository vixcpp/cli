/**
 *
 *  @file ReplMath.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RELP_MATH_HPP
#define VIX_RELP_MATH_HPP

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

#endif
