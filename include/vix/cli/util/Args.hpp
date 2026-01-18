/**
 *
 *  @file Args.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_CLI_ARGS_HPP
#define VIX_CLI_ARGS_HPP

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vix/cli/process/Process.hpp>

namespace vix::cli::util
{
  namespace process = vix::cli::process;

  inline bool is_option(std::string_view s) noexcept
  {
    return !s.empty() && s.front() == '-';
  }

  // Returns the next token if present and not an option. Advances i on success.
  inline std::optional<std::string_view>
  take_value(const std::vector<std::string> &args, std::size_t &i) noexcept
  {
    if (i + 1 >= args.size())
      return std::nullopt;

    const std::string &next = args[i + 1];
    if (is_option(next))
      return std::nullopt;

    ++i;
    return std::string_view(args[i]);
  }

  inline bool iequals_ascii(std::string_view a, std::string_view b) noexcept
  {
    if (a.size() != b.size())
      return false;

    for (std::size_t i = 0; i < a.size(); ++i)
    {
      const unsigned char ca = static_cast<unsigned char>(a[i]);
      const unsigned char cb = static_cast<unsigned char>(b[i]);
      if (std::tolower(ca) != std::tolower(cb))
        return false;
    }
    return true;
  }

  inline std::optional<process::LinkerMode>
  parse_linker_mode(std::string_view v) noexcept
  {
    if (iequals_ascii(v, "auto"))
      return process::LinkerMode::Auto;
    if (iequals_ascii(v, "default"))
      return process::LinkerMode::Default;
    if (iequals_ascii(v, "mold"))
      return process::LinkerMode::Mold;
    if (iequals_ascii(v, "lld"))
      return process::LinkerMode::Lld;

    return std::nullopt;
  }

  inline std::optional<process::LauncherMode>
  parse_launcher_mode(std::string_view v) noexcept
  {
    if (iequals_ascii(v, "auto"))
      return process::LauncherMode::Auto;
    if (iequals_ascii(v, "none"))
      return process::LauncherMode::None;
    if (iequals_ascii(v, "sccache"))
      return process::LauncherMode::Sccache;
    if (iequals_ascii(v, "ccache"))
      return process::LauncherMode::Ccache;

    return std::nullopt;
  }

} // namespace vix::cli::util

#endif
