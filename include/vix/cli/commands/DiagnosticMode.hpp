#ifndef VIX_CLI_COMMANDS_DIAGNOSTIC_MODE_HPP
#define VIX_CLI_COMMANDS_DIAGNOSTIC_MODE_HPP

#include <optional>
#include <string>
#include <string_view>

#include <vix/engine/SanitizerMode.hpp>

namespace vix::cli::commands
{
  using DiagnosticMode = vix::engine::SanitizerMode;

  enum class DiagnosticRequestKind
  {
    Unspecified,
    Off,
    Explicit
  };

  struct DiagnosticRequest
  {
    DiagnosticRequestKind kind{DiagnosticRequestKind::Unspecified};
    DiagnosticMode mode{DiagnosticMode::None};
  };

  enum class DiagnosticCommand
  {
    Build,
    Run,
    Dev,
    Test
  };

  enum class DiagnosticProjectKind
  {
    Unknown,
    NativeVix,
    StandaloneCpp,
    ExternalCMake
  };

  inline DiagnosticMode resolve_diagnostic_mode(
      DiagnosticCommand command,
      bool release,
      DiagnosticProjectKind projectKind,
      const DiagnosticRequest &cliRequest,
      const DiagnosticRequest &projectRequest = {}) noexcept
  {
    const auto resolve_request = [](const DiagnosticRequest &request)
    {
      if (request.kind == DiagnosticRequestKind::Off)
        return DiagnosticMode::None;
      if (request.kind == DiagnosticRequestKind::Explicit)
        return request.mode;
      return DiagnosticMode::None;
    };

    if (cliRequest.kind != DiagnosticRequestKind::Unspecified)
      return resolve_request(cliRequest);
    if (projectRequest.kind != DiagnosticRequestKind::Unspecified)
      return resolve_request(projectRequest);

    if (release || projectKind == DiagnosticProjectKind::ExternalCMake)
      return DiagnosticMode::None;

    const bool native = projectKind == DiagnosticProjectKind::NativeVix;
    const bool standalone = projectKind == DiagnosticProjectKind::StandaloneCpp;
    if ((command == DiagnosticCommand::Run && (native || standalone)) ||
        (command == DiagnosticCommand::Dev && (native || standalone)) ||
        (command == DiagnosticCommand::Test && native))
      return DiagnosticMode::AddressUndefined;

    return DiagnosticMode::None;
  }

  inline bool select_diagnostic_request_option(
      std::string_view argument,
      std::optional<DiagnosticRequest> &selected,
      std::string &error)
  {
    std::optional<DiagnosticRequest> requested;
    if (argument == "--diagnostics=off")
      requested = DiagnosticRequest{DiagnosticRequestKind::Off, DiagnosticMode::None};
    else if (argument == "--diagnostics" || argument.rfind("--diagnostics=", 0) == 0)
    {
      error = "Use --diagnostics=off to disable automatic diagnostics, or select --san, --asan, --ubsan, or --tsan.";
      return true;
    }
    else
    {
      std::optional<DiagnosticMode> mode;
      if (argument == "--san")
        mode = DiagnosticMode::AddressUndefined;
      else if (argument == "--asan")
        mode = DiagnosticMode::Address;
      else if (argument == "--ubsan")
        mode = DiagnosticMode::Undefined;
      else if (argument == "--tsan")
        mode = DiagnosticMode::Thread;
      else
        return false;

      requested = DiagnosticRequest{DiagnosticRequestKind::Explicit, *mode};
    }

    if (selected)
    {
      error = "Choose at most one diagnostic request (a diagnostic mode or --diagnostics=off).";
      return true;
    }

    selected = requested;
    return true;
  }

  // Parses only the four canonical Vix diagnostic switches. A recognized
  // second switch is rejected even when it repeats the selected mode.
  inline bool select_diagnostic_mode_option(
      std::string_view argument,
      std::optional<DiagnosticMode> &selected,
      std::string &error)
  {
    std::optional<DiagnosticMode> requested;
    if (argument == "--san")
      requested = DiagnosticMode::AddressUndefined;
    else if (argument == "--asan")
      requested = DiagnosticMode::Address;
    else if (argument == "--ubsan")
      requested = DiagnosticMode::Undefined;
    else if (argument == "--tsan")
      requested = DiagnosticMode::Thread;
    else
      return false;

    if (selected)
    {
      error = "Choose at most one diagnostic mode (--san, --asan, --ubsan, or --tsan).";
      return true;
    }

    selected = requested;
    return true;
  }
}

#endif
