/**
 * @file BackendTemplateUtils.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#ifndef VIX_CLI_COMMANDS_NEW_TEMPLATES_BACKEND_TEMPLATE_UTILS_HPP
#define VIX_CLI_COMMANDS_NEW_TEMPLATES_BACKEND_TEMPLATE_UTILS_HPP

#include <string>

namespace vix::commands::new_cmd::templates
{
  /**
   * @brief Build a stable include guard for a generated backend header.
   *
   * The generated guard includes the project name and a file-specific suffix
   * to avoid collisions between generated headers and installed Vix headers.
   *
   * @param projectName Name of the generated backend project.
   * @param suffix File-specific suffix, for example APP_BOOTSTRAP or HOME_CONTROLLER.
   * @return Include guard name.
   */
  std::string make_backend_header_guard(
      const std::string &projectName,
      const std::string &suffix);

} // namespace vix::commands::new_cmd::templates

#endif // VIX_CLI_COMMANDS_NEW_TEMPLATES_BACKEND_TEMPLATE_UTILS_HPP
