/**
 *
 *  @file TestsDetail.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_TESTS_DETAIL_HPP
#define VIX_TESTS_DETAIL_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <vix/cli/commands/DiagnosticMode.hpp>

namespace vix::commands::TestsCommand::detail
{
  namespace fs = std::filesystem;
  using DiagnosticMode = ::vix::cli::commands::DiagnosticMode;

  struct Options
  {
    bool watch = false;
    bool list = false;
    bool failFast = false;
    bool ctestMode = false;
    bool verbose = false;
    bool quiet = false;
    DiagnosticMode diagnosticMode{DiagnosticMode::None};
    ::vix::cli::commands::DiagnosticRequest diagnosticRequest{};
    bool parseFailed = false;
    std::string parseError;
    bool runAfter = false; // --run (tests + runtime)
    bool raw = false;
    // Literal, case-sensitive substring filter over registered test names.
    std::optional<std::string> exactTestName;
    std::string testPattern;

    fs::path projectDir;
    std::vector<std::string> forwarded;
    std::vector<std::string> ctestArgs;
  };

  Options parse(const std::vector<std::string> &args);
}

#endif
