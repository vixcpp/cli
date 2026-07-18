/**
 *
 *  @file NoteCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#include <vix/cli/commands/NoteCommand.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef VIX_CLI_HAS_NOTE

#include <vix/note/note.hpp>
#include <vix/note/extensions/NoteExtensionRegistry.hpp>

#include <vix/utils/Logger.hpp>
#include <vix/utils/ServerPrettyLogs.hpp>

#ifdef VIX_CLI_HAS_UI
#include <vix/ui/platform/Platform.hpp>
#include <vix/ui/shell/AppShell.hpp>
#include <vix/ui/shell/ShellConfig.hpp>
#endif

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace fs = std::filesystem;

namespace
{
  using Log = vix::utils::Logger;

  // -------------------------------------------------------------------------
  //  Output mode
  // -------------------------------------------------------------------------

  enum class NoteLogMode
  {
    Normal, // runtime banner + logger lifecycle lines
    Quiet,  // errors only
    Json    // machine-readable lifecycle events
  };

  void note_set_env(const char *key, const char *value)
  {
#if defined(_WIN32)
    _putenv_s(key, value);
#else
    ::setenv(key, value, 1);
#endif
  }

  void note_apply_color_override(int colorOverride)
  {
    if (colorOverride == 0)
    {
      note_set_env("VIX_COLOR", "never");
      return;
    }

    if (colorOverride == 1)
    {
      note_set_env("VIX_COLOR", "always");
      return;
    }
  }

  void note_configure_logger(NoteLogMode mode)
  {
    auto &log = Log::getInstance();

    Log::Context ctx;
    ctx.module = "note";
    log.setContext(std::move(ctx));

    if (mode == NoteLogMode::Json)
    {
      // Keep stdout clean for JSON lifecycle events.
      log.setLevel(Log::Level::Off);
      return;
    }

    if (mode == NoteLogMode::Quiet)
    {
      log.setLevel(Log::Level::Error);
      return;
    }

    log.setLevelFromEnv("VIX_LOG_LEVEL");
    log.setFormatFromEnv("VIX_LOG_FORMAT");
  }

  void note_json_escape(std::ostream &os, std::string_view value)
  {
    for (const char ch : value)
    {
      switch (ch)
      {
      case '"':
        os << "\\\"";
        break;
      case '\\':
        os << "\\\\";
        break;
      case '\b':
        os << "\\b";
        break;
      case '\f':
        os << "\\f";
        break;
      case '\n':
        os << "\\n";
        break;
      case '\r':
        os << "\\r";
        break;
      case '\t':
        os << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20)
        {
          static constexpr char hex[] = "0123456789abcdef";
          const unsigned char uc = static_cast<unsigned char>(ch);

          os << "\\u00";
          os << hex[(uc >> 4) & 0x0F];
          os << hex[uc & 0x0F];
        }
        else
        {
          os << ch;
        }
        break;
      }
    }
  }

  struct NoteReporter
  {
    NoteLogMode mode = NoteLogMode::Normal;

    bool normal() const { return mode == NoteLogMode::Normal; }
    bool json() const { return mode == NoteLogMode::Json; }

    void event(std::string_view name) const
    {
      if (!json())
        return;

      std::cout << "{\"event\":\"";
      note_json_escape(std::cout, name);
      std::cout << "\"}\n";
      std::cout.flush();
    }

    void event(std::string_view name,
               std::string_view key,
               std::string_view value) const
    {
      if (!json())
        return;

      std::cout << "{\"event\":\"";
      note_json_escape(std::cout, name);
      std::cout << "\",\"";
      note_json_escape(std::cout, key);
      std::cout << "\":\"";
      note_json_escape(std::cout, value);
      std::cout << "\"}\n";
      std::cout.flush();
    }

    void info(std::string_view message) const
    {
      if (!normal())
        return;

      Log::getInstance().info("{}", message);
    }

    void success(std::string_view message) const
    {
      if (!normal())
        return;

      Log::getInstance().info("{}", message);
    }

    void error(std::string_view message) const
    {
      if (json())
      {
        event("error", "message", message);
        return;
      }

      Log::getInstance().error("{}", message);
    }

    void error_hint(std::string_view message) const
    {
      if (message.empty())
        return;

      if (json())
      {
        event("hint", "message", message);
        return;
      }

      Log::getInstance().error("hint: {}", message);
    }
  };

  // -------------------------------------------------------------------------
  //  Small argument helpers
  // -------------------------------------------------------------------------

  bool note_is_help_arg(std::string_view arg) noexcept
  {
    return arg == "-h" || arg == "--help";
  }

  bool note_take_prefix(std::string_view arg,
                        std::string_view prefix,
                        std::string &out)
  {
    if (arg.size() < prefix.size() ||
        arg.compare(0, prefix.size(), prefix) != 0)
    {
      return false;
    }

    out.assign(arg.substr(prefix.size()));
    return true;
  }

  bool note_parse_port(std::string_view value, std::uint16_t &out) noexcept
  {
    if (value.empty())
      return false;

    unsigned long port = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    const auto result = std::from_chars(begin, end, port);

    if (result.ec != std::errc{} || result.ptr != end)
      return false;

    if (port == 0 || port > 65535)
      return false;

    out = static_cast<std::uint16_t>(port);
    return true;
  }

  bool note_parse_positive_int(std::string_view value, int &out) noexcept
  {
    if (value.empty())
      return false;

    long parsed = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    const auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end)
      return false;

    if (parsed <= 0 || parsed > 100000)
      return false;

    out = static_cast<int>(parsed);
    return true;
  }

  // -------------------------------------------------------------------------
  //  Shared output-flag parsing
  // -------------------------------------------------------------------------

  bool note_take_output_flag(std::string_view arg,
                             NoteLogMode &mode,
                             int &colorOverride)
  {
    if (arg == "--quiet" || arg == "-q" || arg == "--silent")
    {
      mode = NoteLogMode::Quiet;
      return true;
    }

    if (arg == "--json")
    {
      mode = NoteLogMode::Json;
      return true;
    }

    if (arg == "--no-color" || arg == "--no-colour")
    {
      colorOverride = 0;
      return true;
    }

    if (arg == "--color" || arg == "--colour")
    {
      colorOverride = 1;
      return true;
    }

    return false;
  }

  void note_scan_output_flags(const std::vector<std::string> &args,
                              NoteLogMode &mode,
                              int &colorOverride)
  {
    for (const std::string &arg : args)
    {
      NoteLogMode scannedMode = mode;
      int scannedColor = colorOverride;

      if (note_take_output_flag(arg, scannedMode, scannedColor))
      {
        mode = scannedMode;
        colorOverride = scannedColor;
      }
    }
  }

  // -------------------------------------------------------------------------
  //  Workspace defaults
  // -------------------------------------------------------------------------

  std::string note_default_workspace_title()
  {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);

    if (!ec)
    {
      const std::string name = cwd.filename().string();

      if (!name.empty())
        return name;
    }

    return "Untitled Note";
  }

  fs::path note_next_untitled_path()
  {
    for (int i = 0; i < 1000; ++i)
    {
      fs::path candidate =
          i == 0
              ? fs::path("Untitled.vixnote")
              : fs::path("Untitled-" + std::to_string(i) + ".vixnote");

      std::error_code ec;

      if (!fs::exists(candidate, ec) && !ec)
        return candidate;
    }

    return fs::path("Untitled.vixnote");
  }

  vix::note::NoteDocument note_make_workspace_document()
  {
    const std::string title = note_default_workspace_title();

    vix::note::NoteDocument document(title);
    document.set_path(note_next_untitled_path().string());

    document.add_cell(
        vix::note::NoteCell(
            "intro",
            vix::note::NoteCellKind::Markdown,
            "# " + title + "\n\nStart writing your note here."));

    return document;
  }

  fs::path note_context_path(const fs::path &notePath)
  {
    if (!notePath.empty())
      return notePath;

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);

    if (!ec)
      return cwd;

    return fs::path(".");
  }


  // Returns 0 if valid, otherwise a non-zero exit code.
  int note_validate_path(const NoteReporter &out, const fs::path &notePath)
  {
    std::error_code ec;

    if (!fs::exists(notePath, ec) || ec)
    {
      out.error("Note file not found: " + notePath.string());
      out.error_hint("Create a .vixnote file or pass the correct path.");
      return 1;
    }

    if (!fs::is_regular_file(notePath, ec) || ec)
    {
      out.error("Note path is not a file: " + notePath.string());
      return 1;
    }

    if (notePath.extension() != ".vixnote")
    {
      out.error("Invalid note file extension: " + notePath.string());
      out.error_hint("Expected a .vixnote file.");
      return 1;
    }

    return 0;
  }

  // -------------------------------------------------------------------------
  //  Pretty runtime banner + structured startup logs
  // -------------------------------------------------------------------------

  void note_emit_ready_banner(const std::string &host,
                              std::uint16_t port,
                              int readyMs)
  {
    vix::utils::ServerReadyInfo info;

    info.app = "Vix Note";
    info.ready_ms = readyMs;
    info.mode = vix::utils::RuntimeBanner::mode_from_env();
    info.status = "listening";

    info.scheme = "http";
    info.host = host;
    info.port = static_cast<int>(port);
    info.base_path = "/";

    info.show_ws = false;
    info.show_hints = true;

    vix::utils::RuntimeBanner::emit_server_ready(info);
  }

  // -------------------------------------------------------------------------
  //  vix note export
  // -------------------------------------------------------------------------

  int note_print_export_help(bool asError)
  {
    std::cout
        << "Usage:\n"
        << "  vix note export <file.vixnote> --out <file.html> [options]\n\n"

        << "Description:\n"
        << "  Export a Vix Note document to a standalone HTML lesson.\n\n"

        << "Options:\n"
        << "  --out <file.html>       Output HTML file\n"
        << "  --out=<file.html>       Same as --out <file.html>\n"
        << "  --no-outputs            Export without cell outputs\n"
        << "  --with-outputs          Export with cell outputs (default)\n"
        << "  --quiet, -q             Only print errors\n"
        << "  --json                  Emit machine-readable events\n"
        << "  --no-color              Disable ANSI colors\n"
        << "  --color                 Force ANSI colors\n"
        << "  -h, --help              Show this help\n\n"

        << "Examples:\n"
        << "  vix note export examples/hello.vixnote --out hello.html\n"
        << "  vix note export lessons/pointers.vixnote --out pointers.html --no-outputs\n";

    return asError ? 1 : 0;
  }

  int note_run_export(const std::vector<std::string> &args)
  {
    if (args.empty())
      return note_print_export_help(/*asError=*/true);

    if (note_is_help_arg(args[0]))
      return note_print_export_help(/*asError=*/false);

    NoteLogMode mode = NoteLogMode::Normal;
    int colorOverride = -1;

    note_scan_output_flags(args, mode, colorOverride);
    note_apply_color_override(colorOverride);
    note_configure_logger(mode);

    NoteReporter out;
    out.mode = mode;

    fs::path notePath;
    fs::path outPath;
    bool includeOutputs = true;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];
      std::string value;

      if (note_is_help_arg(arg))
        return note_print_export_help(/*asError=*/false);

      if (note_take_output_flag(arg, mode, colorOverride))
        continue;

      if (arg == "--out")
      {
        if (i + 1 >= args.size())
        {
          out.error("Missing value for --out.");
          out.error_hint("Usage: vix note export <file.vixnote> --out <file.html>");
          return 1;
        }

        outPath = args[++i];
        continue;
      }

      if (note_take_prefix(arg, "--out=", value))
      {
        if (value.empty())
        {
          out.error("Missing value for --out.");
          return 1;
        }

        outPath = value;
        continue;
      }

      if (arg == "--no-outputs")
      {
        includeOutputs = false;
        continue;
      }

      if (arg == "--with-outputs")
      {
        includeOutputs = true;
        continue;
      }

      if (!arg.empty() && arg.front() == '-')
      {
        out.error("Unknown option: " + arg);
        out.error_hint("Run 'vix note export --help' to see available options.");
        return 1;
      }

      if (!notePath.empty())
      {
        out.error("Unexpected argument: " + arg);
        out.error_hint("Usage: vix note export <file.vixnote> --out <file.html>");
        return 1;
      }

      notePath = arg;
    }

    if (notePath.empty())
    {
      out.error("No Vix Note file provided.");
      out.error_hint("Usage: vix note export <file.vixnote> --out <file.html>");
      return 1;
    }

    if (outPath.empty())
    {
      out.error("No output HTML file provided.");
      out.error_hint("Use --out <file.html>.");
      return 1;
    }

    if (const int rc = note_validate_path(out, notePath); rc != 0)
      return rc;

    vix::note::NoteLoadResult loaded = vix::note::load_note(notePath);

    if (!loaded.ok)
    {
      out.error("Unable to load note file.");
      out.error_hint(loaded.error.empty() ? notePath.string() : loaded.error);
      return 1;
    }

    vix::note::HtmlExporterOptions exportOptions;
    exportOptions.includeOutputs = includeOutputs;
    exportOptions.standalone = true;
    exportOptions.includeDocumentMetadata = true;
    exportOptions.includeTableOfContents = true;
    exportOptions.includeOutputLabels = true;
    exportOptions.printableLayout = true;

    vix::note::HtmlExporter exporter(exportOptions);

    vix::note::NoteResult exported =
        exporter.export_to_file(loaded.document, outPath);

    if (!exported.ok())
    {
      out.error("Unable to export note.");
      out.error_hint(exported.message().empty() ? outPath.string() : exported.message());
      return exported.exit_code() == 0 ? 1 : exported.exit_code();
    }

    out.event("exported", "out", outPath.string());

    if (out.normal())
    {
      Log::getInstance().logf(Log::Level::Info,
                              "Vix Note exported",
                              "input",
                              notePath.string(),
                              "out",
                              outPath.string());
    }

    out.success("Vix Note exported -> " + outPath.string());
    return 0;
  }

  // -------------------------------------------------------------------------
  //  Desktop shell
  // -------------------------------------------------------------------------

#ifdef VIX_CLI_HAS_UI
  int note_run_desktop_shell(
      const NoteReporter &out,
      vix::note::NoteServer &server,
      const fs::path &notePath,
      int width,
      int height,
      bool devtools,
      bool fullscreen,
      bool resizable)
  {
    vix::ui::ShellConfig shellConfig;

    shellConfig
        .set_name("Vix Note")
        .set_title("Vix Note - " + notePath.filename().string())
        .set_app_id("com.vixcpp.note")
        .set_vendor("Vix.cpp")
        .set_url(server.url())
        .set_readiness_url(server.url())
        .set_platform(vix::ui::Platform::current())
        .set_width(width)
        .set_height(height)
        .set_resizable(resizable)
        .set_fullscreen(fullscreen)
        .set_devtools(devtools)
        .set_start_server(false)
        .set_wait_for_server(true)
        .set_startup_timeout(std::chrono::milliseconds(30000));

    vix::ui::AppShell shell(shellConfig);

    out.event("desktop_opening", "url", server.url());

    const auto shellStart = std::chrono::steady_clock::now();

    vix::ui::Result<void> shellResult = shell.start();

    const auto shellEnd = std::chrono::steady_clock::now();

    const auto shellLifetimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            shellEnd - shellStart)
            .count();

    vix::note::NoteResult stopped = server.stop();

    if (shellResult.is_failed())
    {
      out.error("Unable to start Vix Note desktop shell.");
      out.error_hint(shellResult.error_message().empty()
                         ? "Unknown desktop shell error."
                         : shellResult.error_message());
      return 1;
    }

    if (shellLifetimeMs < 800)
    {
      out.error("Desktop shell unavailable.");
      return 1;
    }

    if (!stopped.ok())
    {
      out.error("Vix Note server stopped with an error.");
      out.error_hint(stopped.message().empty()
                         ? "Unknown note server error."
                         : stopped.message());
      return stopped.exit_code() == 0 ? 1 : stopped.exit_code();
    }

    out.event("desktop_closed");
    return 0;
  }
#endif

  // -------------------------------------------------------------------------
  //  Parsed options for `vix note [file] [options]`
  // -------------------------------------------------------------------------

  struct NoteRunOptions
  {
    fs::path notePath;
    std::string host = "127.0.0.1";
    std::uint16_t port = 5179;

    bool desktopShell = false;
    bool devtools = false;
    bool fullscreen = false;
    bool resizable = true;

    int shellWidth = 1280;
    int shellHeight = 820;

    NoteLogMode logMode = NoteLogMode::Normal;
    int colorOverride = -1; // -1 auto, 0 off, 1 on
    bool noExtensions = false;
    bool listExtensions = false;
  };

  // Returns:
  //   0  -> parsed successfully
  //   1+ -> exit code
  //  -1  -> help was requested
  int note_parse_run_options(
      const NoteReporter &out,
      const std::vector<std::string> &args,
      NoteRunOptions &opts)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];
      std::string value;

      if (note_is_help_arg(arg))
        return -1;

      if (note_take_output_flag(arg, opts.logMode, opts.colorOverride))
        continue;

      if (arg == "--host")
      {
        if (i + 1 >= args.size())
        {
          out.error("Missing value for --host.");
          out.error_hint("Usage: vix note <file.vixnote> --host 127.0.0.1");
          return 1;
        }

        opts.host = args[++i];

        if (opts.host.empty())
        {
          out.error("Host cannot be empty.");
          return 1;
        }

        continue;
      }

      if (note_take_prefix(arg, "--host=", value))
      {
        if (value.empty())
        {
          out.error("Host cannot be empty.");
          return 1;
        }

        opts.host = std::move(value);
        continue;
      }

      if (arg == "--port")
      {
        if (i + 1 >= args.size())
        {
          out.error("Missing value for --port.");
          out.error_hint("Usage: vix note <file.vixnote> --port 5179");
          return 1;
        }

        if (!note_parse_port(args[++i], opts.port))
        {
          out.error("Invalid note server port.");
          out.error_hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      if (note_take_prefix(arg, "--port=", value))
      {
        if (!note_parse_port(value, opts.port))
        {
          out.error("Invalid note server port.");
          out.error_hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      if (arg == "--no-extensions")
      {
        opts.noExtensions = true;
        continue;
      }

      if (arg == "--list-extensions")
      {
        opts.listExtensions = true;
        continue;
      }

      if (arg == "--desktop" || arg == "--shell")
      {
        opts.desktopShell = true;
        continue;
      }

      if (arg == "--browser")
      {
        opts.desktopShell = false;
        continue;
      }

      if (arg == "--devtools")
      {
        opts.devtools = true;
        continue;
      }

      if (arg == "--no-devtools")
      {
        opts.devtools = false;
        continue;
      }

      if (arg == "--fullscreen")
      {
        opts.fullscreen = true;
        continue;
      }

      if (arg == "--resizable")
      {
        opts.resizable = true;
        continue;
      }

      if (arg == "--no-resizable")
      {
        opts.resizable = false;
        continue;
      }

      if (arg == "--width")
      {
        if (i + 1 >= args.size())
        {
          out.error("Missing value for --width.");
          out.error_hint("Usage: vix note <file.vixnote> --desktop --width 1280");
          return 1;
        }

        if (!note_parse_positive_int(args[++i], opts.shellWidth))
        {
          out.error("Invalid desktop shell width.");
          out.error_hint("Width must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (note_take_prefix(arg, "--width=", value))
      {
        if (!note_parse_positive_int(value, opts.shellWidth))
        {
          out.error("Invalid desktop shell width.");
          out.error_hint("Width must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (arg == "--height")
      {
        if (i + 1 >= args.size())
        {
          out.error("Missing value for --height.");
          out.error_hint("Usage: vix note <file.vixnote> --desktop --height 820");
          return 1;
        }

        if (!note_parse_positive_int(args[++i], opts.shellHeight))
        {
          out.error("Invalid desktop shell height.");
          out.error_hint("Height must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (note_take_prefix(arg, "--height=", value))
      {
        if (!note_parse_positive_int(value, opts.shellHeight))
        {
          out.error("Invalid desktop shell height.");
          out.error_hint("Height must be greater than zero.");
          return 1;
        }

        continue;
      }

      if (!arg.empty() && arg.front() == '-')
      {
        out.error("Unknown option: " + arg);
        out.error_hint("Run 'vix note --help' to see available options.");
        return 1;
      }

      if (!opts.notePath.empty())
      {
        out.error("Unexpected argument: " + arg);
        out.error_hint("Usage: vix note <file.vixnote> [options]");
        return 1;
      }

      opts.notePath = arg;
    }

    return 0;
  }

  bool note_desktop_shell_backend_available()
  {
#if defined(_WIN32)
    return true;
#elif defined(__APPLE__)
    return true;
#elif defined(__linux__)
#if defined(VIX_UI_ENABLE_LINUX_WEBVIEW) && VIX_UI_ENABLE_LINUX_WEBVIEW
    return true;
#else
    return false;
#endif
#else
    return false;
#endif
  }
}

namespace vix::commands
{
  int NoteCommand::run(const std::vector<std::string> &args)
  {
    if (!args.empty() && note_is_help_arg(args[0]))
      return help();

    if (!args.empty() && args[0] == "export")
    {
      const std::vector<std::string> exportArgs(args.begin() + 1, args.end());
      return note_run_export(exportArgs);
    }

    NoteRunOptions opts;
    note_scan_output_flags(args, opts.logMode, opts.colorOverride);
    note_apply_color_override(opts.colorOverride);
    note_configure_logger(opts.logMode);

    NoteReporter out;
    out.mode = opts.logMode;

    const int parseResult = note_parse_run_options(out, args, opts);

    if (parseResult == -1)
      return help();

    if (parseResult != 0)
      return parseResult;

    note_apply_color_override(opts.colorOverride);
    note_configure_logger(opts.logMode);
    out.mode = opts.logMode;

    if (opts.desktopShell && !note_desktop_shell_backend_available())
    {
      out.error("Desktop shell unavailable.");
      return 1;
    }

    const bool workspaceMode = opts.notePath.empty();

    vix::note::NoteDocument document;

    if (workspaceMode)
    {
      document = note_make_workspace_document();
    }
    else
    {
      if (const int rc = note_validate_path(out, opts.notePath); rc != 0)
        return rc;

      vix::note::NoteLoadResult loaded = vix::note::load_note(opts.notePath);

      if (!loaded.ok)
      {
        out.error("Unable to load note file.");
        out.error_hint(loaded.error.empty() ? opts.notePath.string() : loaded.error);
        return 1;
      }

      document = std::move(loaded.document);
    }

    vix::note::ProjectContext projectContext =
        vix::note::detect_project_context(note_context_path(opts.notePath));

    vix::note::NoteExtensionManager extensionManager;
    extensionManager.register_builtins();
    if (!opts.noExtensions)
    {
      extensionManager.discover_global();
      extensionManager.discover_project(projectContext);
    }

    if (opts.listExtensions)
    {
      for (const auto &ext : extensionManager.registry().list_extensions())
      {
        std::cout << ext.id << "  " << ext.version << "  " << (ext.available ? "available" : "unavailable") << "\n";
        for (const auto &diag : ext.diagnostics)
          std::cout << "  diagnostic: " << diag << "\n";
      }
      return 0;
    }

    vix::note::NoteServerOptions options;
    options.host = opts.host;
    options.port = opts.port;
    options.openBrowser = false;
    options.routeOptions.kernelOptions.projectContext = projectContext;
    options.routeOptions.kernelOptions.extensionRegistry = &extensionManager.registry();
    options.logRequests = (opts.logMode == NoteLogMode::Normal);

    vix::note::NoteServer server(std::move(document), options);

    const auto startTime = std::chrono::steady_clock::now();
    vix::note::NoteResult started = server.start();
    const auto readyTime = std::chrono::steady_clock::now();

    const int readyMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            readyTime - startTime)
            .count());

    if (!started.ok())
    {
      out.error("Unable to start Vix Note server.");
      out.error_hint(started.message().empty()
                         ? "Unknown note server error."
                         : started.message());
      return started.exit_code() == 0 ? 1 : started.exit_code();
    }

    out.event("started", "url", server.url());

    if (out.normal())
    {
      note_emit_ready_banner(opts.host, opts.port, readyMs);
    }

    if (opts.desktopShell)
    {
#ifdef VIX_CLI_HAS_UI
      return note_run_desktop_shell(
          out,
          server,
          workspaceMode ? note_context_path(opts.notePath) : opts.notePath,
          opts.shellWidth,
          opts.shellHeight,
          opts.devtools,
          opts.fullscreen,
          opts.resizable);
#else
      (void)opts.devtools;
      (void)opts.fullscreen;
      (void)opts.resizable;
      (void)opts.shellWidth;
      (void)opts.shellHeight;

      (void)server.stop();

      out.error("Vix Note desktop shell is not available in this build.");
      out.error_hint("Build the CLI with the vix::ui module enabled.");
      out.error_hint("Expected compile definition: VIX_CLI_HAS_UI=1");

      return 1;
#endif
    }

    out.event("ready");

    vix::note::NoteResult waited = server.wait();

    if (!waited.ok())
    {
      out.error(waited.message().empty()
                    ? "Vix Note server stopped with an error."
                    : waited.message());
      return waited.exit_code() == 0 ? 1 : waited.exit_code();
    }

    out.event("stopped");
    return 0;
  }

  int NoteCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix note [file.vixnote] [options]\n"
        << "  vix note export <file.vixnote> --out <file.html> [options]\n\n"

        << "Description:\n"
        << "  Open a Vix Note document in a local browser UI or desktop WebView shell.\n"
        << "  Without a file, the command starts a Vix Note workspace in the current directory.\n"
        << "  With a .vixnote file, it loads that document and starts a local note server,\n"
        << "  exposing the visual workspace through HTTP.\n"
        << "  It can also export a .vixnote file to a standalone HTML lesson.\n\n"

        << "Server options:\n"
        << "  --host <host>       Host used by the local server. Default: 127.0.0.1\n"
        << "  --host=<host>       Same as --host <host>\n"
        << "  --port <port>       Port used by the local server. Default: 5179\n"
        << "  --port=<port>       Same as --port <port>\n"
        << "  --no-extensions     Disable external Note extensions, keep builtins\n"
        << "  --list-extensions   Print detected Note extensions and exit\n\n"

        << "Desktop options:\n"
        << "  --desktop, --shell  Open the note UI in a desktop WebView shell\n"
        << "  --browser           Keep browser/server mode (default)\n"
        << "  --width <px>        Desktop shell width. Default: 1280\n"
        << "  --height <px>       Desktop shell height. Default: 820\n"
        << "  --devtools          Enable WebView developer tools when supported\n"
        << "  --fullscreen        Start desktop shell fullscreen\n"
        << "  --no-resizable      Disable desktop shell resizing\n\n"

        << "Output options:\n"
        << "  --quiet, -q         Only print errors\n"
        << "  --json              Emit machine-readable lifecycle events\n"
        << "  --no-color          Disable ANSI colors (also honors NO_COLOR)\n"
        << "  --color             Force ANSI colors\n\n"

        << "Other:\n"
        << "  export              Export a .vixnote document to HTML\n"
        << "  file.vixnote        Optional note file to open. If omitted, starts in the current directory\n"
        << "  -h, --help          Show this help\n\n"

        << "Examples:\n"
        << "  vix note\n"
        << "  vix note --desktop\n"
        << "  vix note examples/hello.vixnote\n"
        << "  vix note lessons/pointers.vixnote --port 5180\n"
        << "  vix note examples/hello.vixnote --quiet\n"
        << "  vix note examples/hello.vixnote --json\n"
        << "  vix note export examples/hello.vixnote --out hello.html\n"
        << "  vix note export examples/hello.vixnote --out hello.html --no-outputs\n"
        << "  vix note examples/hello.vixnote --desktop --width 1400 --height 900\n"
        << "  vix note examples/hello.vixnote --host 127.0.0.1 --port 5179\n\n"

        << "Routes:\n"
        << "  /                   Vix Note UI\n"
        << "  /api/document       Current document JSON\n"
        << "  /api/cells/<i>/run  Run one cell\n"
        << "  /api/run-all        Run all executable cells\n";

    return 0;
  }
}

#else

namespace vix::commands
{
  int NoteCommand::run(const std::vector<std::string> &)
  {
    std::cerr
        << "  error  Vix Note support is not available in this build.\n"
        << "         Rebuild Vix with -DVIX_ENABLE_NOTE=ON.\n"
        << "         If you build the CLI standalone, make sure the note module is\n"
        << "         available as ../note or installed as vix_note.\n";

    return 1;
  }

  int NoteCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix note <file.vixnote> [options]\n\n"

        << "Description:\n"
        << "  Vix Note support is not available in this build.\n\n"

        << "How to enable:\n"
        << "  cmake -S . -B build -DVIX_ENABLE_NOTE=ON\n"
        << "  cmake --build build -j\n";

    return 0;
  }
}

#endif
