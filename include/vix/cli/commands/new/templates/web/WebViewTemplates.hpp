#pragma once

/**
 * @file WebViewTemplates.hpp
 * @author Gaspard Kirira
 *
 * HTML view and public asset file-content templates for the server-rendered web
 * `vix new` template.
 */

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_web_view_base_html(const std::string &projectName);
  std::string make_web_view_header_html(const std::string &projectName);
  std::string make_web_view_index_html(const std::string &projectName);
  std::string make_web_view_dashboard_html(const std::string &projectName);

  std::string make_web_public_app_css(const std::string &projectName);
  std::string make_web_public_app_js(const std::string &projectName);

} // namespace vix::commands::new_cmd::templates
