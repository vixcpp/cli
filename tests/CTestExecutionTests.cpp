#include <vix/cli/project/cmake/CTestExecution.hpp>

#include <cassert>

namespace cmake_adapter = vix::cli::project::cmake;

namespace
{
  void test_literal_regex_escaping()
  {
    const std::string name = R"(network.connect[ipv6]+timeout.*?(){}^$|\ with space)";
    assert(cmake_adapter::ctest_exact_name_regex(name) ==
           R"(^network\.connect\[ipv6\]\+timeout\.\*\?\(\)\{\}\^\$\|\\ with space$)");
  }

  void test_execution_argv()
  {
    const auto single = cmake_adapter::ctest_execution_argv(
        "/tmp/build", std::nullopt, std::string{"alpha"});
    assert((single == std::vector<std::string>{
        "ctest", "--test-dir", "/tmp/build", "--output-on-failure", "-R", "^alpha$"}));

    const auto multi = cmake_adapter::ctest_execution_argv(
        "/tmp/build", std::string{"Release"},
        std::string{"network.connect[ipv6]+timeout"}, {"--stop-on-failure"});
    assert((multi == std::vector<std::string>{
        "ctest", "--test-dir", "/tmp/build", "--output-on-failure",
        "-C", "Release", "-R", "^network\\.connect\\[ipv6\\]\\+timeout$",
        "--stop-on-failure"}));
  }
} // namespace

int main()
{
  test_literal_regex_escaping();
  test_execution_argv();
}
