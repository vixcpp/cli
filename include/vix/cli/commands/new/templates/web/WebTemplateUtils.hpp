/**
 * @file WebTemplateUtils.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#ifndef VIX_CLI_COMMANDS_NEW_TEMPLATES_WEB_TEMPLATE_UTILS_HPP
#define VIX_CLI_COMMANDS_NEW_TEMPLATES_WEB_TEMPLATE_UTILS_HPP

#include <string>

namespace vix::commands::new_cmd::templates
{
  /**
   * @brief Build a stable include guard for a generated web header.
   *
   * @param projectName Name of the generated web project.
   * @param suffix File-specific suffix, for example APP_BOOTSTRAP.
   * @return Include guard name.
   */
  std::string make_web_header_guard(
      const std::string &projectName,
      const std::string &suffix);

} // namespace vix::commands::new_cmd::templates

#endif // VIX_CLI_COMMANDS_NEW_TEMPLATES_WEB_TEMPLATE_UTILS_HPP
