#include <vix/cli/errors/ClangGccParser.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
  try
  {
    const std::string withRange =
        "/tmp/example.cpp:12:9: error: invalid expression\n"
        "   12 |   bad_call()\n"
        "      |   ^~~~~~~~\n";
    const auto ranged = vix::cli::errors::ClangGccParser::parse(withRange);
    if (ranged.size() != 1 || ranged[0].column != 9 || ranged[0].endColumn != 16)
      throw std::runtime_error("compiler underline range was not preserved");

    const std::string singleColumn =
        "/tmp/example.cpp:7:4: error: invalid expression\n";
    const auto fallback = vix::cli::errors::ClangGccParser::parse(singleColumn);
    if (fallback.size() != 1 || fallback[0].column != 4 || fallback[0].endColumn != 0)
      throw std::runtime_error("single-column diagnostic gained an invented range");
  }
  catch (const std::exception &error)
  {
    std::cerr << "ClangGccParserTests: " << error.what() << "\n";
    return 1;
  }
}
