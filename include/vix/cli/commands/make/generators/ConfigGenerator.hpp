/**
 *
 *  @file ConfigGenerator.hpp
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
#ifndef VIX_CONFIG_GENERATOR_HPP
#define VIX_CONFIG_GENERATOR_HPP

#include <vix/cli/commands/make/MakeDispatcher.hpp>
#include <vix/cli/commands/make/MakeResult.hpp>

namespace vix::cli::make::generators
{
  struct ConfigSpec
  {
    bool with_server{true};
    bool with_logging{true};
    bool with_waf{true};
    bool with_websocket{false};
    bool with_database{false};
  };

  [[nodiscard]] MakeResult generate_config(const MakeContext &ctx);

  [[nodiscard]] MakeResult generate_config(const MakeContext &ctx,
                                           const ConfigSpec &spec);
} // namespace vix::cli::make::generators

#endif
