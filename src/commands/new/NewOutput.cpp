/**
 * @file NewOutput.cpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 */

#include <vix/cli/commands/new/NewOutput.hpp>
#include <vix/cli/Style.hpp>

#include <iostream>
#include <string>
#include <vector>

using namespace vix::cli::style;

namespace vix::commands::new_cmd::output
{

  // ------------------------------------------------------------------
  // Internal
  // ------------------------------------------------------------------

  static constexpr const char *TEAL = "\033[38;5;35m";

  static void sep()
  {
    std::cout << PAD << GRAY << "─────────────────────────────────────" << RESET << "\n";
  }

  static void section(const std::string &title)
  {
    std::cout << PAD << GRAY << title << RESET << "\n";
  }

  static void print_command_step(int index, const std::string &cmd, const std::string &hint = "")
  {
    std::cout << PAD
              << GRAY << index << RESET
              << "  "
              << CYAN << BOLD << cmd << RESET;

    if (!hint.empty())
      std::cout << "  " << GRAY << hint << RESET;

    std::cout << "\n";
  }

  // ------------------------------------------------------------------
  // Banner
  // ------------------------------------------------------------------

  void print_banner(const std::string &projName, const std::string &kind)
  {
    std::cout << "\n"
              << PAD << TEAL << BOLD << "✔" << RESET
              << "  " << TEAL << BOLD << projName << RESET
              << "  " << GRAY << kind << RESET
              << "\n";
  }

  // ------------------------------------------------------------------
  // Next steps
  // ------------------------------------------------------------------

  static void print_steps_app(const std::string &projName)
  {
    section("next");

    print_command_step(1, "cd " + projName + "/", "enter project");
    print_command_step(2, "vix build", "compile");
    print_command_step(3, "vix run", "start app");
  }

  static void print_steps_vue(const std::string &projName)
  {
    section("next");

    print_command_step(1, "cd " + projName + "/", "enter project");
    print_command_step(2, "cd frontend && npm install", "install Vue dependencies");
    print_command_step(3, "vix run", "start Vix backend");
    print_command_step(4, "cd frontend && npm run dev", "start Vue frontend");
  }

  static void print_steps_lib(const std::string &projName)
  {
    section("next");

    print_command_step(1, "cd " + projName + "/", "enter project");
    print_command_step(2, "vix build --build-target all", "compile");
    print_command_step(
        3,
        "vix build --build-target all -- -D" + projName + "_BUILD_TESTS=ON",
        "enable tests");
    print_command_step(4, "vix tests", "run tests");
  }

  // ------------------------------------------------------------------
  // High-level entry points
  // ------------------------------------------------------------------

  void print_creation_app(
      const std::filesystem::path & /*projectDir*/,
      const std::string &projName,
      const FeaturesSelection & /*features*/)
  {
    print_banner(projName, "application");
    sep();
    print_steps_app(projName);
  }

  void print_creation_vue(
      const std::filesystem::path & /*projectDir*/,
      const std::string &projName)
  {
    print_banner(projName, "Vue + Vix application");
    sep();
    print_steps_vue(projName);
  }

  void print_creation_lib(
      const std::filesystem::path & /*projectDir*/,
      const std::string &projName)
  {
    print_banner(projName, "library");
    sep();
    print_steps_lib(projName);
  }

} // namespace vix::commands::new_cmd::output
