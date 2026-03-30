/**
 *
 *  @file MakeDispatcher.hpp
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
#ifndef VIX_MAKE_DISPATCHER_HPP
#define VIX_MAKE_DISPATCHER_HPP

#include <vix/cli/commands/make/MakeOptions.hpp>
#include <vix/cli/commands/make/MakePaths.hpp>
#include <vix/cli/commands/make/MakeResult.hpp>
#include <vix/cli/commands/make/MakeTypes.hpp>

#include <string>

namespace vix::cli::make
{
  struct MakeContext
  {
    MakeOptions options;
    MakeKind kind = MakeKind::Unknown;
    MakeLayout layout;
    std::string name;
    std::string name_space;
  };

  [[nodiscard]] MakeKind parse_make_kind(const std::string &kind);
  [[nodiscard]] bool is_valid_make_kind(MakeKind kind) noexcept;

  [[nodiscard]] MakeResult dispatch_make(const MakeOptions &options);
}

#endif
