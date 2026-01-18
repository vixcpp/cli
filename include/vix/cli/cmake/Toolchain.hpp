/**
 *
 *  @file Toolchain.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_TOOLCHAIN_HPP
#define VIX_CLI_TOOLCHAIN_HPP

#include <string>
#include <string_view>
#include <vector>

namespace vix::cli::build
{
  std::string infer_processor_from_triple(std::string_view triple);
  std::string toolchain_contents_for_triple(
      const std::string &triple,
      const std::string &sysroot);

  std::vector<std::string> detect_available_targets();
} // namespace vix::cli::build

#endif
