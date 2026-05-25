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

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_http_responses_hpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1200);

    s += "#pragma once\n\n";
    s += "#include <string>\n\n";
    s += "namespace vix\n";
    s += "{\n";
    s += "  namespace json\n";
    s += "  {\n";
    s += "    class Json;\n";
    s += "  }\n\n";
    s += "  namespace http\n";
    s += "  {\n";
    s += "    class ResponseWrapper;\n";
    s += "  }\n\n";
    s += "  using Response = vix::http::ResponseWrapper;\n";
    s += "}\n\n";
    s += "namespace " + projectName + "::support\n";
    s += "{\n";
    s += "  void json_error(\n";
    s += "      vix::Response &res,\n";
    s += "      int status,\n";
    s += "      const std::string &code,\n";
    s += "      const std::string &message);\n\n";
    s += "  void json_ok(\n";
    s += "      vix::Response &res,\n";
    s += "      const vix::json::Json &data);\n\n";
    s += "  void json_message(\n";
    s += "      vix::Response &res,\n";
    s += "      const std::string &message);\n";
    s += "} // namespace " + projectName + "::support\n";

    return s;
  }

  std::string make_backend_http_responses_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1800);

    s += "#include <" + projectName + "/support/HttpResponses.hpp>\n\n";
    s += "#include <vix.hpp>\n\n";
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
