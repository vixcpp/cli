/**
 * @file BackendSupportTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendSupportTemplates.hpp>
#include <vix/cli/commands/new/templates/backend/BackendTemplateUtils.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_http_responses_hpp(const std::string &projectName)
  {
    const std::string guard = make_backend_header_guard(
        projectName,
        "HTTP_RESPONSES");

    std::string s;
    s.reserve(2600);

    s += "/**\n";
    s += " * @file HttpResponses.hpp\n";
    s += " * @brief JSON response helpers for the " + projectName + " backend.\n";
    s += " */\n\n";

    s += "#ifndef " + guard + "\n";
    s += "#define " + guard + "\n\n";

    s += "#include <string>\n\n";
    s += "#include <vix.hpp>\n\n";

    s += "namespace " + projectName + "::support\n";
    s += "{\n";
    s += "  /**\n";
    s += "   * @brief Write a standard JSON error response.\n";
    s += "   *\n";
    s += "   * @param res HTTP response wrapper.\n";
    s += "   * @param status HTTP status code to send.\n";
    s += "   * @param code Machine-readable error code.\n";
    s += "   * @param message Human-readable error message.\n";
    s += "   */\n";
    s += "  void json_error(\n";
    s += "      vix::Response &res,\n";
    s += "      int status,\n";
    s += "      const std::string &code,\n";
    s += "      const std::string &message);\n\n";

    s += "  /**\n";
    s += "   * @brief Write a standard successful JSON response with data.\n";
    s += "   *\n";
    s += "   * @param res HTTP response wrapper.\n";
    s += "   * @param data JSON payload to place under the data field.\n";
    s += "   */\n";
    s += "  void json_ok(\n";
    s += "      vix::Response &res,\n";
    s += "      const vix::json::Json &data);\n\n";

    s += "  /**\n";
    s += "   * @brief Write a standard successful JSON response with a message.\n";
    s += "   *\n";
    s += "   * @param res HTTP response wrapper.\n";
    s += "   * @param message Message to send to the client.\n";
    s += "   */\n";
    s += "  void json_message(\n";
    s += "      vix::Response &res,\n";
    s += "      const std::string &message);\n";
    s += "} // namespace " + projectName + "::support\n\n";

    s += "#endif // " + guard + "\n";

    return s;
  }

  std::string make_backend_http_responses_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(2600);

    s += "/**\n";
    s += " * @file HttpResponses.cpp\n";
    s += " * @brief JSON response helper implementation for the " + projectName + " backend.\n";
    s += " */\n\n";

    s += "#include <" + projectName + "/support/HttpResponses.hpp>\n\n";

    s += "namespace " + projectName + "::support\n";
    s += "{\n";
    s += "  void json_error(\n";
    s += "      vix::Response &res,\n";
    s += "      int status,\n";
    s += "      const std::string &code,\n";
    s += "      const std::string &message)\n";
    s += "  {\n";
    s += "    res.status(status).json({\n";
    s += "      \"ok\", false,\n";
    s += "      \"error\", code,\n";
    s += "      \"message\", message\n";
    s += "    });\n";
    s += "  }\n\n";

    s += "  void json_ok(\n";
    s += "      vix::Response &res,\n";
    s += "      const vix::json::Json &data)\n";
    s += "  {\n";
    s += "    res.json({\n";
    s += "      \"ok\", true,\n";
    s += "      \"data\", data\n";
    s += "    });\n";
    s += "  }\n\n";

    s += "  void json_message(\n";
    s += "      vix::Response &res,\n";
    s += "      const std::string &message)\n";
    s += "  {\n";
    s += "    res.json({\n";
    s += "      \"ok\", true,\n";
    s += "      \"message\", message\n";
    s += "    });\n";
    s += "  }\n";
    s += "} // namespace " + projectName + "::support\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
