/**
 *
 *  @file LambdaGenerator.hpp
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
#ifndef VIX_LAMBDA_GENERATOR_HPP
#define VIX_LAMBDA_GENERATOR_HPP

#include <vix/cli/commands/make/MakeDispatcher.hpp>
#include <vix/cli/commands/make/MakeResult.hpp>

namespace vix::cli::make::generators
{
  [[nodiscard]] MakeResult generate_lambda(const MakeContext &ctx);
}

#endif
