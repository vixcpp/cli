/**
 * @file TestsFlow.cpp
 * Public argument parsing for the generic `vix test` command.
 */
#include <vix/cli/commands/tests/TestsDetail.hpp>

#include <filesystem>
#include <system_error>

namespace vix::commands::TestsCommand::detail
{
  namespace fs = std::filesystem;

  static fs::path normalize_project_dir(const fs::path &path)
  {
    std::error_code ec;
    const auto absolute = fs::absolute(path, ec);
    if (ec)
      return path;
    const auto canonical = fs::weakly_canonical(absolute, ec);
    return ec ? absolute : canonical;
  }

  Options parse(const std::vector<std::string> &args)
  {
    Options opt{};
    bool directorySelected = false;
    bool filterSelected = false;
    bool verboseSelected = false;
    bool quietSelected = false;
    std::optional<::vix::cli::commands::DiagnosticRequest> diagnosticRequest;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const auto &arg = args[i];
      if (arg == "--ctest")
      {
        opt.ctestMode = true;
        if (i + 1 >= args.size())
        {
          opt.parseFailed = true;
          opt.parseError = "--ctest requires CTest arguments.";
          return opt;
        }
        for (++i; i < args.size(); ++i)
          opt.ctestArgs.push_back(args[i]);
        break;
      }
      std::string diagnosticError;
      if (vix::cli::commands::select_diagnostic_request_option(
              arg, diagnosticRequest, diagnosticError))
      {
        if (!diagnosticError.empty())
        {
          opt.parseFailed = true;
          opt.parseError = std::move(diagnosticError);
          return opt;
        }
        opt.diagnosticRequest = *diagnosticRequest;
        continue;
      }
      if (arg == "--watch") opt.watch = true;
      else if (arg == "--list") opt.list = true;
      else if (arg == "--fail-fast") opt.failFast = true;
      else if (arg == "--verbose") { opt.verbose = true; verboseSelected = true; }
      else if (arg == "--quiet" || arg == "-q") { opt.quiet = true; quietSelected = true; }
      else if (arg == "-d" || arg == "--dir")
      {
        if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1].front() == '-')
        {
          opt.parseFailed = true;
          opt.parseError = "Missing value for --dir.";
          return opt;
        }
        opt.projectDir = normalize_project_dir(args[++i]);
        directorySelected = true;
      }
      else if (arg.rfind("--dir=", 0) == 0)
      {
        if (arg.size() == 6)
        {
          opt.parseFailed = true;
          opt.parseError = "Missing value for --dir.";
          return opt;
        }
        opt.projectDir = normalize_project_dir(arg.substr(6));
        directorySelected = true;
      }
      else if (arg == "--")
      {
        opt.parseFailed = true;
        opt.parseError = "Raw test-runner arguments are not part of the public vix test interface.";
        return opt;
      }
      else if (!arg.empty() && arg.front() == '-')
      {
        opt.parseFailed = true;
        opt.parseError = "Unknown test option: " + arg;
        return opt;
      }
      else if (!filterSelected)
      {
        opt.testPattern = arg;
        filterSelected = true;
      }
      else
      {
        opt.parseFailed = true;
        opt.parseError = "vix test accepts one optional filter.";
        return opt;
      }
    }

    if (verboseSelected && quietSelected)
    {
      opt.parseFailed = true;
      opt.parseError = "--verbose and --quiet cannot be used together.";
      return opt;
    }
    if (opt.ctestMode && (filterSelected || opt.list || opt.watch || opt.failFast))
    {
      opt.parseFailed = true;
      opt.parseError = "--ctest cannot be combined with a filter, --list, --watch, or --fail-fast.";
      return opt;
    }
    if (opt.ctestMode &&
        opt.diagnosticRequest.kind != ::vix::cli::commands::DiagnosticRequestKind::Unspecified)
    {
      opt.parseFailed = true;
      opt.parseError = "Diagnostic options cannot be combined with terminal --ctest mode.";
      return opt;
    }
    if (!directorySelected)
      opt.projectDir = normalize_project_dir(fs::current_path());
    if (opt.failFast && !opt.ctestMode)
      opt.ctestArgs.push_back("--stop-on-failure");
    return opt;
  }
} // namespace vix::commands::TestsCommand::detail
