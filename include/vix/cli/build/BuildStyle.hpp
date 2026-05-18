/**
 *
 *  @file BuildStyle.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Reusable build output style
 *
 */

#ifndef VIX_CLI_BUILD_BUILD_STYLE_HPP
#define VIX_CLI_BUILD_BUILD_STYLE_HPP

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  enum class BuildMessageKind
  {
    Info,
    Step,
    Success,
    Warning,
    Error
  };

  struct BuildLocation
  {
    fs::path file;
    std::size_t line{0};
    std::size_t column{0};

    bool valid() const;
  };

  struct BuildCodeFrame
  {
    BuildLocation location;
    std::vector<std::string> lines;

    bool valid() const;
  };

  struct BuildDiagnostic
  {
    std::string title{"Build failed"};
    std::string message;
    std::string error;
    std::string hint;

    BuildLocation location;
    BuildCodeFrame codeFrame;

    bool has_location() const;
    bool has_code_frame() const;
  };

  enum class BuildWarningKind
  {
    Unknown,
    UnusedFunction,
    UnusedVariable,
    UnusedParameter,
    ShadowedVariable,
    DeprecatedDeclaration,
    SignCompare,
    Conversion,
    Reorder,
    MissingFieldInitializer
  };

  struct BuildWarning
  {
    fs::path file;
    std::size_t line{0};
    std::size_t column{0};

    BuildWarningKind kind{BuildWarningKind::Unknown};

    std::string symbol;
    std::string message;
    std::string hint;
    std::string flag;
    std::string raw;

    bool has_location() const;
  };

  struct BuildProgress
  {
    std::string target;
    std::string preset;
    std::size_t current{0};
    std::size_t total{0};
    std::string action;

    bool valid() const;
  };

  std::string build_message_prefix(BuildMessageKind kind);

  void print_build_message(
      std::ostream &out,
      BuildMessageKind kind,
      const std::string &message);

  void print_build_info(
      std::ostream &out,
      const std::string &message);

  void print_build_step(
      std::ostream &out,
      const std::string &message);

  void print_build_success(
      std::ostream &out,
      const std::string &message);

  void print_build_warning(
      std::ostream &out,
      const std::string &message);

  void print_build_error(
      std::ostream &out,
      const std::string &message);

  void print_build_header(
      std::ostream &out,
      const std::string &target,
      const std::string &preset);

  void print_build_progress(
      std::ostream &out,
      const BuildProgress &progress);

  void print_build_done(
      std::ostream &out,
      const std::string &profile,
      const std::string &duration);

  void print_build_diagnostic(
      std::ostream &out,
      const BuildDiagnostic &diagnostic);

  std::string format_build_location(
      const BuildLocation &location);

  std::string relative_build_path(
      const fs::path &path,
      const fs::path &base);

  std::optional<BuildLocation> parse_build_location(
      const std::string &text);

  void print_build_header_full(
      std::ostream &out,
      const std::string &target,
      const std::string &preset,
      const std::optional<std::string> &launcher,
      const std::optional<std::string> &fastLinkerFlag,
      int jobs);

  void print_build_success_timed(
      std::ostream &out,
      const std::string &message,
      long long milliseconds);

  void print_task_header_full(
      std::ostream &out,
      const std::string &action,
      const std::string &target,
      const std::string &preset,
      const std::vector<std::pair<std::string, std::string>> &meta);

  void print_task_success_timed(
      std::ostream &out,
      const std::string &message,
      long long milliseconds);

  void print_task_failure_timed(
      std::ostream &out,
      const std::string &message,
      long long milliseconds);

  std::optional<BuildWarning> parse_build_warning(
      const std::string &text);

  void print_build_warnings_summary(
      std::ostream &out,
      const std::vector<BuildWarning> &warnings,
      std::size_t total);

  std::optional<BuildWarning> parse_build_warning(
      const std::string &text);

  BuildWarning humanize_build_warning(
      BuildWarning warning);

  void print_build_warnings_summary(
      std::ostream &out,
      const std::vector<BuildWarning> &warnings,
      std::size_t total);

} // namespace vix::cli::build

#endif
