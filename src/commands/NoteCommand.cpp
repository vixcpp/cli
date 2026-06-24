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
#include <vix/cli/Style.hpp>

#include <iostream>
#include <string>
#include <vector>

#ifdef VIX_CLI_HAS_NOTE

#include <vix/note/note.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
  bool is_help_arg(const std::string &arg)
  {
    return arg == "-h" || arg == "--help";
  }

  bool parse_port(const std::string &value, std::uint16_t &out)
  {
    if (value.empty())
    {
      return false;
    }

    unsigned int port = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    auto result = std::from_chars(begin, end, port);

    if (result.ec != std::errc{} || result.ptr != end)
    {
      return false;
    }

    if (port == 0 || port > 65535)
    {
      return false;
    }

    out = static_cast<std::uint16_t>(port);
    return true;
  }

  int run_export_command(const std::vector<std::string> &args)
  {
    using namespace vix::cli::style;

    if (args.empty() || is_help_arg(args[0]))
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
          << "  --with-outputs          Export with cell outputs, default\n"
          << "  -h, --help              Show this help\n\n"

          << "Examples:\n"
          << "  vix note export examples/hello.vixnote --out hello.html\n"
          << "  vix note export lessons/pointers.vixnote --out pointers.html --no-outputs\n";

      return args.empty() ? 1 : 0;
    }

    fs::path notePath;
    fs::path outPath;
    bool includeOutputs = true;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (is_help_arg(arg))
      {
        return run_export_command({"--help"});
      }

      if (arg == "--out")
      {
        if (i + 1 >= args.size())
        {
          error("Missing value for --out.");
          hint("Usage: vix note export <file.vixnote> --out <file.html>");
          return 1;
        }

        outPath = args[++i];
        continue;
      }

      constexpr const char outPrefix[] = "--out=";

      if (arg.rfind(outPrefix, 0) == 0)
      {
        outPath = arg.substr(sizeof(outPrefix) - 1);
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

      if (!notePath.empty())
      {
        error("Unexpected argument: " + arg);
        hint("Usage: vix note export <file.vixnote> --out <file.html>");
        return 1;
      }

      notePath = arg;
    }

    if (notePath.empty())
    {
      error("No Vix Note file provided.");
      hint("Usage: vix note export <file.vixnote> --out <file.html>");
      return 1;
    }

    if (outPath.empty())
    {
      error("No output HTML file provided.");
      hint("Use --out <file.html>.");
      return 1;
    }

    std::error_code ec;

    if (!fs::exists(notePath, ec) || ec)
    {
      error("Note file not found: " + notePath.string());
      return 1;
    }

    if (!fs::is_regular_file(notePath, ec) || ec)
    {
      error("Note path is not a file: " + notePath.string());
      return 1;
    }

    if (notePath.extension() != ".vixnote")
    {
      error("Invalid note file extension: " + notePath.string());
      hint("Expected a .vixnote file.");
      return 1;
    }

    vix::note::NoteLoadResult loaded =
        vix::note::load_note(notePath);

    if (!loaded.ok)
    {
      error("Unable to load note file.");
      hint(loaded.error.empty() ? notePath.string() : loaded.error);
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
      error("Unable to export note.");
      hint(exported.message().empty()
               ? outPath.string()
               : exported.message());

      return exported.exit_code() == 0 ? 1 : exported.exit_code();
    }

    success("Vix Note exported.");
    step(outPath.string());

    return 0;
  }
}

namespace vix::commands
{
  int NoteCommand::run(const std::vector<std::string> &args)
  {
    using namespace vix::cli::style;

    if (!args.empty() && is_help_arg(args[0]))
    {
      return help();
    }

    if (!args.empty() && args[0] == "export")
    {
      std::vector<std::string> exportArgs(
          args.begin() + 1,
          args.end());

      return run_export_command(exportArgs);
    }

    fs::path notePath;
    std::string host = "127.0.0.1";
    std::uint16_t port = 5179;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (is_help_arg(arg))
      {
        return help();
      }

      if (arg == "--host")
      {
        if (i + 1 >= args.size())
        {
          error("Missing value for --host.");
          hint("Usage: vix note <file.vixnote> --host 127.0.0.1");
          return 1;
        }

        host = args[++i];

        if (host.empty())
        {
          error("Host cannot be empty.");
          return 1;
        }

        continue;
      }

      constexpr const char hostPrefix[] = "--host=";

      if (arg.rfind(hostPrefix, 0) == 0)
      {
        host = arg.substr(sizeof(hostPrefix) - 1);

        if (host.empty())
        {
          error("Host cannot be empty.");
          return 1;
        }

        continue;
      }

      if (arg == "--port")
      {
        if (i + 1 >= args.size())
        {
          error("Missing value for --port.");
          hint("Usage: vix note <file.vixnote> --port 5179");
          return 1;
        }

        if (!parse_port(args[++i], port))
        {
          error("Invalid note server port.");
          hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      constexpr const char portPrefix[] = "--port=";

      if (arg.rfind(portPrefix, 0) == 0)
      {
        if (!parse_port(arg.substr(sizeof(portPrefix) - 1), port))
        {
          error("Invalid note server port.");
          hint("Port must be between 1 and 65535.");
          return 1;
        }

        continue;
      }

      if (!notePath.empty())
      {
        error("Unexpected argument: " + arg);
        hint("Usage: vix note <file.vixnote> [options]");
        return 1;
      }

      notePath = arg;
    }

    if (notePath.empty())
    {
      error("No Vix Note file provided.");
      hint("Usage: vix note <file.vixnote> [options]");
      hint("Example: vix note examples/hello.vixnote");
      return 1;
    }

    std::error_code ec;

    if (!fs::exists(notePath, ec) || ec)
    {
      error("Note file not found: " + notePath.string());
      hint("Create a .vixnote file or pass the correct path.");
      return 1;
    }

    if (!fs::is_regular_file(notePath, ec) || ec)
    {
      error("Note path is not a file: " + notePath.string());
      return 1;
    }

    if (notePath.extension() != ".vixnote")
    {
      error("Invalid note file extension: " + notePath.string());
      hint("Expected a .vixnote file.");
      return 1;
    }

    vix::note::NoteLoadResult loaded = vix::note::load_note(notePath);

    if (!loaded.ok)
    {
      error("Unable to load note file.");
      hint(loaded.error.empty() ? notePath.string() : loaded.error);
      return 1;
    }

    vix::note::ProjectContext projectContext =
        vix::note::detect_project_context(notePath);

    vix::note::NoteServerOptions options;
    options.host = host;
    options.port = port;
    options.openBrowser = false;
    options.routeOptions.kernelOptions.projectContext = projectContext;

    vix::note::NoteServer server(
        std::move(loaded.document),
        options);

    vix::note::NoteResult started = server.start();

    if (!started.ok())
    {
      error("Unable to start Vix Note server.");
      hint(started.message().empty()
               ? "Unknown note server error."
               : started.message());

      return started.exit_code() == 0 ? 1 : started.exit_code();
    }

    success("Vix Note started.");
    info("Open this URL in your browser:");
    step(server.url());

    if (projectContext.enabled)
    {
      info("Project context:");
      step(projectContext.projectName.empty()
               ? projectContext.projectRoot.string()
               : projectContext.projectName);
    }

    std::cout << "\n";
    hint("Press Ctrl+C to stop the note server.");

    vix::note::NoteResult waited = server.wait();

    if (!waited.ok())
    {
      error(waited.message().empty()
                ? "Vix Note server stopped with an error."
                : waited.message());

      return waited.exit_code() == 0 ? 1 : waited.exit_code();
    }

    return 0;
  }

  int NoteCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix note <file.vixnote> [options]\n"
        << "  vix note export <file.vixnote> --out <file.html> [options]\n\n"

        << "Description:\n"
        << "  Open a Vix Note document in a local browser UI.\n"
        << "  The command loads a .vixnote file, starts a local note server,\n"
        << "  and exposes the visual workspace through HTTP.\n"
        << "  It can also export a .vixnote file to a standalone HTML lesson.\n\n"

        << "Options:\n"
        << "  --host <host>       Host used by the local server. Default: 127.0.0.1\n"
        << "  --host=<host>       Same as --host <host>\n"
        << "  --port <port>       Port used by the local server. Default: 5179\n"
        << "  --port=<port>       Same as --port <port>\n"
        << "  export              Export a .vixnote document to HTML\n"
        << "  -h, --help          Show this help\n\n"

        << "Examples:\n"
        << "  vix note examples/hello.vixnote\n"
        << "  vix note lessons/pointers.vixnote --port 5180\n"
        << "  vix note export examples/hello.vixnote --out hello.html\n"
        << "  vix note export examples/hello.vixnote --out hello.html --no-outputs\n"
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
    using namespace vix::cli::style;

    error("Vix Note support is not available in this build.");
    hint("Rebuild Vix with -DVIX_ENABLE_NOTE=ON.");
    hint("If you build the CLI standalone, make sure the note module is available as ../note or installed as vix_note.");

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
