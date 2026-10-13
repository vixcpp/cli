/** @file CTestExecution.cpp */
#include <vix/cli/project/cmake/CTestExecution.hpp>

namespace vix::cli::project::cmake
{
  std::string ctest_exact_name_regex(std::string_view name)
  {
    std::string escaped;
    escaped.reserve(name.size() * 2 + 2);
    escaped.push_back('^');
    for (const char character : name)
    {
      switch (character)
      {
      case '.': case '+': case '*': case '?': case '(':
      case ')': case '[': case ']': case '{': case '}':
      case '^': case '$': case '|': case '\\':
        escaped.push_back('\\');
        break;
      default:
        break;
      }
      escaped.push_back(character);
    }
    escaped.push_back('$');
    return escaped;
  }

  std::vector<std::string> ctest_execution_argv(
      const Path &buildRoot,
      const std::optional<std::string> &configuration,
      const std::optional<std::string> &exactTestName,
      const std::vector<std::string> &nativeArguments)
  {
    std::vector<std::string> argv{
        "ctest", "--test-dir", buildRoot.string(), "--output-on-failure"};
    if (configuration)
    {
      argv.push_back("-C");
      argv.push_back(*configuration);
    }
    if (exactTestName)
    {
      argv.push_back("-R");
      argv.push_back(ctest_exact_name_regex(*exactTestName));
    }
    argv.insert(argv.end(), nativeArguments.begin(), nativeArguments.end());
    return argv;
  }
} // namespace vix::cli::project::cmake
