#include <vix/cli/errors/CodeFrame.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
  void expect_contains(const std::string &text, const std::string &needle)
  {
    if (text.find(needle) == std::string::npos)
      throw std::runtime_error("missing ANSI diagnostic fragment: " + needle);
  }

  void expect_not_contains(const std::string &text, const std::string &needle)
  {
    if (text.find(needle) != std::string::npos)
      throw std::runtime_error("unexpected ANSI diagnostic fragment: " + needle);
  }
}

int main()
{
  try
  {
    const auto source =
        std::filesystem::temp_directory_path() / "vix-code-frame-style-test.cpp";
    { std::ofstream out(source); out << "int first = 1;\nint second = missing;\n"; }

    vix::cli::errors::CompilerError error;
    error.file = source.string();
    error.line = 2;
    error.column = 14;

    std::ostringstream captured;
    auto *const previous = std::cerr.rdbuf(captured.rdbuf());
    vix::cli::errors::printCodeFrame(
        error,
        vix::cli::errors::ErrorContext{},
        vix::cli::errors::CodeFrameOptions{});
    std::cerr.rdbuf(previous);

    const std::string rendered = captured.str();
    expect_contains(rendered, "\033[1;96m-->\033[0m");
    expect_contains(rendered, source.string());
    expect_contains(rendered, "\033[1mcode:\033[0m");
    expect_contains(rendered, "int second = missing;");
    expect_contains(rendered, "\033[1;91m");
    expect_not_contains(rendered, "\033[90m");

    std::error_code ignored;
    std::filesystem::remove(source, ignored);
  }
  catch (const std::exception &error)
  {
    std::cerr << "CodeFrameStyleTests: " << error.what() << "\n";
    return 1;
  }
}
