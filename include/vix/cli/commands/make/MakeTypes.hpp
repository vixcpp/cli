/**
 *
 *  @file MakeTypes.hpp
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
#ifndef VIX_MAKE_TYPES_HPP
#define VIX_MAKE_TYPES_HPP

#include <string_view>

namespace vix::cli::make
{
  enum class MakeKind
  {
    Unknown = 0,
    Class,
    Struct,
    Enum,
    Function,
    Lambda,
    Concept,
    Exception,
    Test,
    Module
  };

  [[nodiscard]] constexpr std::string_view to_string(const MakeKind kind) noexcept
  {
    switch (kind)
    {
    case MakeKind::Class:
      return "class";
    case MakeKind::Struct:
      return "struct";
    case MakeKind::Enum:
      return "enum";
    case MakeKind::Function:
      return "function";
    case MakeKind::Lambda:
      return "lambda";
    case MakeKind::Concept:
      return "concept";
    case MakeKind::Exception:
      return "exception";
    case MakeKind::Test:
      return "test";
    case MakeKind::Module:
      return "module";
    case MakeKind::Unknown:
    default:
      return "unknown";
    }
  }
}

#endif
