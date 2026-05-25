/**
 * @file BackendTestTemplates.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/templates/backend/BackendTestTemplates.hpp>

#include <string>

namespace vix::commands::new_cmd::templates
{

  std::string make_backend_basic_test_cpp(const std::string &projectName)
  {
    std::string s;
    s.reserve(1000);

    s += "#include <vix/tests/tests.hpp>\n\n";
    s += "int main()\n";
    s += "{\n";
    s += "  using namespace vix::tests;\n\n";
    s += "  auto &registry = TestRegistry::instance();\n";
    s += "  registry.clear();\n\n";
    s += "  registry.add(TestCase(\"" + projectName + " backend basic test\", [] {\n";
    s += "    Assert::equal(2 + 2, 4);\n";
    s += "  }));\n\n";
    s += "  return TestRunner::run_all_and_exit();\n";
    s += "}\n";

    return s;
  }

} // namespace vix::commands::new_cmd::templates
