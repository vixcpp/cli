/**
 * @file BackendTemplateUtils.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendTemplateUtils.hpp>

#include <cctype>
#include <string>

namespace vix::commands::new_cmd::templates
{
  std::string make_backend_header_guard(
      const std::string &projectName,
      const std::string &suffix)
  {
    std::string guard = "VIX_GENERATED_BACKEND_";

    for (char c : projectName)
    {
      const unsigned char ch = static_cast<unsigned char>(c);

      if (std::isalnum(ch))
        guard += static_cast<char>(std::toupper(ch));
      else
        guard += '_';
    }

    guard += "_";
    guard += suffix;
    guard += "_HPP";

    return guard;
  }

} // namespace vix::commands::new_cmd::templates
