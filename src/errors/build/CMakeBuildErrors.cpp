/**
 *
 *  @file CMakeBuildErrors.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/errors/build/CMakeBuildErrors.hpp>
#include <vix/cli/build/BuildStyle.hpp>

#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::build
{
  namespace
  {
    // -------------------------------------------------------------------------
    // Low-level string helpers
    // -------------------------------------------------------------------------

    std::string trim_copy(std::string text)
    {
      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.front())) != 0)
      {
        text.erase(text.begin());
      }

      while (!text.empty() &&
             std::isspace(static_cast<unsigned char>(text.back())) != 0)
      {
        text.pop_back();
      }

      return text;
    }

    bool starts_with(std::string_view text, std::string_view prefix)
    {
      return text.size() >= prefix.size() &&
             text.substr(0, prefix.size()) == prefix;
    }

    bool contains(std::string_view log, std::string_view needle)
    {
      return log.find(needle) != std::string_view::npos;
    }

    // -------------------------------------------------------------------------
    // Regex extraction helper
    // -------------------------------------------------------------------------

    std::string extract(
        std::string_view log,
        const std::regex &re,
        std::size_t group = 1)
    {
      std::match_results<std::string_view::const_iterator> match;

      if (std::regex_search(log.begin(), log.end(), match, re) &&
          match.size() > group)
      {
        return match[group].str();
      }

      return {};
    }

    // Extract the first quoted string that appears after `marker` in `log`.
    std::string extract_quoted_after(std::string_view log,
                                     std::string_view marker)
    {
      const std::size_t pos = log.find(marker);
      if (pos == std::string_view::npos)
        return {};

      const std::string_view tail = log.substr(pos + marker.size());
      const std::size_t q1 = tail.find('"');
      if (q1 == std::string_view::npos)
        return {};

      const std::size_t q2 = tail.find('"', q1 + 1);
      if (q2 == std::string_view::npos)
        return {};

      return std::string(tail.substr(q1 + 1, q2 - q1 - 1));
    }

    // -------------------------------------------------------------------------
    // Noise-line classification
    // -------------------------------------------------------------------------

    // Returns true for CMake status lines that carry no actionable information
    // and must never be used as the "reason" in a diagnostic.
    bool is_cmake_noise_line(std::string_view line)
    {
      // Trim leading whitespace for the comparison.
      const std::size_t first = line.find_first_not_of(" \t\r\n");
      const std::string_view t =
          (first == std::string_view::npos) ? line : line.substr(first);

      return starts_with(t, "-- Configuring done") ||
             starts_with(t, "-- Generating done") ||
             starts_with(t, "CMake Generate step failed") ||
             starts_with(t, "Build files cannot be regenerated") ||
             starts_with(t, "Build files have been written to") ||
             starts_with(t, "-- Build files have been");
    }

    // -------------------------------------------------------------------------
    // Generic CMake error extraction
    // -------------------------------------------------------------------------

    // Returns the first meaningful block of text following a "CMake Error" line,
    // skipping noise lines.  Never returns a noise line as the reason.
    std::string first_cmake_error_block(std::string_view log)
    {
      const std::size_t pos = log.find("CMake Error");
      if (pos == std::string_view::npos)
        return {};

      // Everything from the first "CMake Error" line onward.
      const std::string_view tail = log.substr(pos);

      std::istringstream stream{std::string(tail)};
      std::string message;
      std::string line;

      while (std::getline(stream, line))
      {
        const std::string trimmed = trim_copy(line);

        if (trimmed.empty())
        {
          // A blank line ends the error block once we have collected something.
          if (!message.empty())
            break;
          continue;
        }

        if (is_cmake_noise_line(trimmed))
          break;

        if (!message.empty())
          message += ' ';

        message += trimmed;
      }

      return message;
    }

    // Returns the single most informative line from the first CMake Error block.
    // Prefers the content after the first colon on the opening "CMake Error" line;
    // falls back to the entire block if the opening line is content-free.
    std::string first_cmake_error_message(std::string_view log)
    {
      const std::size_t pos = log.find("CMake Error");
      if (pos == std::string_view::npos)
        return {};

      const std::size_t lineEnd = log.find('\n', pos);
      const std::string firstLine =
          (lineEnd == std::string_view::npos)
              ? std::string(log.substr(pos))
              : std::string(log.substr(pos, lineEnd - pos));

      // Try to get the message that follows the first colon on the error line.
      const std::size_t colon = firstLine.find(':');
      if (colon != std::string::npos && colon + 1 < firstLine.size())
      {
        std::string sameLine = trim_copy(firstLine.substr(colon + 1));
        if (!sameLine.empty() && !is_cmake_noise_line(sameLine))
          return sameLine;
      }

      // Fall back: collect a multi-line block, skipping noise.
      return first_cmake_error_block(log);
    }

    // -------------------------------------------------------------------------
    // Output helpers  (preserve existing Vix CLI style)
    // -------------------------------------------------------------------------

    void print_error_title(std::string_view message)
    {
      std::cerr << RED
                << "error: "
                << RESET
                << message
                << "\n";
    }

    void print_hint(std::string_view message)
    {
      if (message.empty())
        return;

      std::cerr << YELLOW
                << "hint: "
                << RESET
                << message
                << "\n";
    }

    void print_field(std::string_view label, std::string_view value)
    {
      if (value.empty())
        return;

      std::cerr << PAD << label << value << "\n";
    }

    void print_colored_field(std::string_view label,
                             const char *color,
                             std::string_view value)
    {
      if (value.empty())
        return;

      std::cerr << PAD
                << label
                << color
                << value
                << RESET
                << "\n";
    }

    // -------------------------------------------------------------------------
    // Symbol shortener (unchanged from original)
    // -------------------------------------------------------------------------

    std::string shorten_cpp_symbol(std::string symbol)
    {
      const auto paren = symbol.find('(');
      if (paren != std::string::npos)
        symbol = symbol.substr(0, paren) + "(...)";

      if (symbol.size() > 160)
        symbol = symbol.substr(0, 157) + "...";

      return symbol;
    }

    // =========================================================================
    // Specialised handlers
    // Each returns true when it handled the error, false otherwise.
    // =========================================================================

    // 7. Unknown Ninja target
    bool handle_unknown_ninja_target(std::string_view log)
    {
      constexpr std::string_view marker = "ninja: error: unknown target '";

      const std::size_t pos = log.find(marker);
      if (pos == std::string_view::npos)
        return false;

      const std::size_t start = pos + marker.size();
      const std::size_t end = log.find('\'', start);

      std::string target = "unknown";
      if (end != std::string_view::npos && end > start)
        target = std::string(log.substr(start, end - start));

      std::cerr << PAD
                << RED << BOLD << "✖ Build target not found" << RESET
                << "\n";

      std::cerr << PAD
                << CYAN << "target:" << RESET
                << " " << target << "\n";

      std::cerr << PAD
                << YELLOW << "hint:" << RESET
                << " This project does not define a CMake target named '"
                << target << "'.\n";

      std::cerr << PAD
                << "      "
                << "Check your CMakeLists.txt or use --build-target with an existing target."
                << "\n";

      return true;
    }

    // 5. Stale CMake cache / source mismatch
    bool handleCacheMismatch(std::string_view log)
    {
      const bool hasCurrentCacheDir =
          contains(log, "The current CMakeCache.txt directory");

      const bool hasSourceMismatch =
          contains(log, "does not match the source") &&
          contains(log, "used to generate cache");

      if (!hasCurrentCacheDir && !hasSourceMismatch)
        return false;

      print_error_title("stale CMake build cache");
      print_hint("run vix build --clean or remove the old build directory");
      return true;
    }

    // 1. Missing CMakeLists.txt
    bool handleMissingCMakeLists(std::string_view log)
    {
      const bool missingSourceDirCMakeLists =
          contains(log, "The source directory") &&
          contains(log, "does not appear to contain CMakeLists.txt");

      const bool missingExplicitCMakeLists =
          contains(log, "CMake Error: The source directory") &&
          contains(log, "CMakeLists.txt");

      const bool missingPathExists =
          contains(log, "Source directory") &&
          contains(log, "does not exist");

      if (!missingSourceDirCMakeLists &&
          !missingExplicitCMakeLists &&
          !missingPathExists)
      {
        return false;
      }

      const std::string path = extract(
          log,
          std::regex(R"re((?:The source directory|Source directory)\s+"([^"]+)")re"));

      print_error_title("CMakeLists.txt not found");
      print_field("directory: ", path);
      print_hint("check the source directory passed to CMake");
      return true;
    }

    // 4. Missing compiler
    bool handleCompilerNotFound(std::string_view log)
    {
      if (!contains(log, "No CMAKE_CXX_COMPILER") &&
          !contains(log, "No CMAKE_C_COMPILER") &&
          !contains(log, "Could not find compiler") &&
          !contains(log, "is not a full path to an existing compiler") &&
          !contains(log, "The CXX compiler identification is unknown") &&
          !contains(log, "The C compiler identification is unknown"))
      {
        return false;
      }

      print_error_title("compiler not found");
      print_hint("install a C/C++ compiler or set CMAKE_CXX_COMPILER explicitly");
      return true;
    }

    // 11. CMake version too old
    //     Guard tightly: require "cmake_minimum_required" or an explicit version
    //     mismatch phrase so we don't fire on every log that mentions "version".
    bool handleCMakeVersion(std::string_view log)
    {
      const bool hasMinReq = contains(log, "cmake_minimum_required");
      const bool hasMismatch = contains(log, "does not match the requirement");
      const bool hasOldMsg = contains(log, "CMake") &&
                             (contains(log, "or higher is required") ||
                              contains(log, "version required"));

      if (!hasMinReq && !hasMismatch && !hasOldMsg)
        return false;

      // Require an actual CMake Error line so we don't fire on configure
      // status output that merely mentions cmake_minimum_required.
      if (!contains(log, "CMake Error"))
        return false;

      const std::string required = extract(
          log,
          std::regex(R"re(cmake_minimum_required\s*\(\s*VERSION\s+([\d.]+))re"));

      const std::string requiredAlt = extract(
          log,
          std::regex(R"re(CMake\s+([\d.]+)\s+or higher is required)re"));

      print_error_title("CMake version too old");
      print_field("required: ", required.empty() ? requiredAlt : required);
      print_hint("upgrade CMake or lower cmake_minimum_required() if the project supports it");
      return true;
    }

    // 2. CMake syntax / parse errors
    bool handleCMakeSyntaxError(std::string_view log)
    {
      if (!contains(log, "Syntax error") &&
          !contains(log, "Parse error") &&
          !contains(log, "unexpected token") &&
          !contains(log, "Expected ')'") &&
          !contains(log, "Expected a command name") &&
          !contains(log, "while parsing"))
      {
        return false;
      }

      const std::string file = extract(
          log,
          std::regex(R"re(CMake Error at ([^\s:]+):\d+)re"));

      const std::string line = extract(
          log,
          std::regex(R"re(CMake Error at [^\s:]+:(\d+))re"));

      print_error_title("syntax error in CMakeLists.txt");

      if (!file.empty())
        print_field("at: ", file + ":" + line);

      print_hint("check for mismatched parentheses, invalid commands, or malformed arguments");
      return true;
    }

    // 10. CMake policy errors
    bool handleCMakePolicy(std::string_view log)
    {
      if (!contains(log, "CMP"))
        return false;

      if (!contains(log, "CMake Error") &&
          !contains(log, "cmake_policy") &&
          !contains(log, "Policy"))
      {
        return false;
      }

      const std::string policy = extract(
          log,
          std::regex(R"re((CMP\d{4}))re"));

      print_error_title("CMake policy violation");
      print_field("policy: ", policy);

      if (!policy.empty())
        print_hint("set the policy explicitly with cmake_policy(SET " + policy + " NEW)");
      else
        print_hint("set the required CMake policy or increase cmake_minimum_required()");

      return true;
    }

    // 3. Missing package / find_package failure
    bool handlePackageNotFound(std::string_view log)
    {
      if (!contains(log, "Could not find a package configuration file") &&
          !contains(log, "Could NOT find") &&
          !contains(log, "By not providing") &&
          !contains(log, "No package '") &&
          !contains(log, "pkg_check_modules"))
      {
        return false;
      }

      const std::string packageName = extract(
          log,
          std::regex(R"re(Could NOT find\s+(\S+))re"));

      const std::string packageNameAlt = extract(
          log,
          std::regex(R"re(No package '([^']+)')re"));

      const std::string packageNameAlt2 = extract(
          log,
          std::regex(R"re(configuration file provided by "([^"]+)")re"));

      const std::string best =
          !packageName.empty() ? packageName : !packageNameAlt.empty() ? packageNameAlt
                                                                       : packageNameAlt2;

      print_error_title("required package not found");
      print_colored_field("package: ", RED, best);
      print_hint("install the missing library or set its CMake hint variable, "
                 "for example <Pkg>_ROOT");
      return true;
    }

    // 6. Missing source file
    bool handleMissingSourceFile(std::string_view log)
    {
      if (!contains(log, "Cannot find source file") &&
          !contains(log, "No SOURCES given to target"))
      {
        return false;
      }

      const std::string file = extract(
          log,
          std::regex(R"re(Cannot find source file:\s*\n?\s*([^\s\n]+))re"));

      const std::string target = extract(
          log,
          std::regex(R"re(No SOURCES given to target[:\s]+"?([^\s"\n]+))re"));

      print_error_title("source file not found");

      if (!file.empty())
        print_colored_field("file: ", RED, file);
      else if (!target.empty())
        print_colored_field("target: ", RED, target);

      print_hint("check the filename spelling in add_executable() or add_library()");
      return true;
    }

    // 8. Missing linked CMake target
    bool handleMissingLinkTarget(std::string_view log)
    {
      if (!contains(log, "but the target was not found") ||
          !contains(log, "target_link_libraries"))
      {
        return false;
      }

      const std::string target = extract(
          log,
          std::regex(R"re(Target\s+"([^"]+)")re"));

      const std::string missing = extract(
          log,
          std::regex(R"re(links\s+to:\s*\n\s*\n\s*([^\s][^\n]*))re"));

      print_error_title("unresolved CMake target");
      print_field("target: ", target);
      print_colored_field("missing: ", RED, missing);
      print_hint("define the target before linking it or call find_package() "
                 "for imported targets");
      return true;
    }

    // 9. install(EXPORT ...) dependency not in export set  ← THE KEY FIX
    //    This must run BEFORE the generic fallback.
    bool handleInstallExportMissingDependency(std::string_view log)
    {
      if (!contains(log, "install(EXPORT") ||
          !contains(log, "which requires target") ||
          !contains(log, "that is not in any export set"))
      {
        return false;
      }

      const std::string exportSet = extract(
          log,
          std::regex(R"re(install\(EXPORT\s+"([^"]+)")re"));

      const std::string target = extract(
          log,
          std::regex(R"re(includes target\s+"([^"]+)")re"));

      const std::string dependency = extract(
          log,
          std::regex(R"re(requires target\s+"([^"]+)")re"));

      print_error_title("CMake export dependency missing");
      print_colored_field("export: ", RED, exportSet);
      print_field("target: ", target);
      print_colored_field("dependency: ", RED, dependency);
      print_hint("the exported target links to another target that is not exported; "
                 "install/export the dependency too, or use find_package() "
                 "with an imported target");
      return true;
    }

    // 12. FetchContent / download / git clone errors
    bool handleFetchContentError(std::string_view log)
    {
      const bool hasFetchContent =
          contains(log, "FetchContent") ||
          contains(log, "ExternalProject") ||
          contains(log, "git clone") ||
          contains(log, "Failed to clone repository");

      const bool hasNetworkError =
          contains(log, "Failed to download") ||
          contains(log, "download failed") ||
          contains(log, "error downloading") ||
          contains(log, "Could not resolve host") ||
          contains(log, "connection timed out") ||
          contains(log, "TLS") ||
          contains(log, "SSL") ||
          contains(log, "error: 6"); // curl: Could not resolve host

      if (!hasFetchContent && !hasNetworkError)
        return false;

      // We need at least one fetch-related term to avoid false positives on
      // unrelated TLS/SSL mentions.
      if (!hasFetchContent)
        return false;

      const std::string dependency = extract(
          log,
          std::regex(R"re(FetchContent_(?:Declare|Populate|MakeAvailable)\s*\(\s*([A-Za-z0-9_\-]+))re"));

      print_error_title("dependency fetch failed");

      if (!dependency.empty())
        print_field("dependency: ", dependency);

      print_hint("check your network connection, repository URL, credentials, "
                 "or retry the build");
      return true;
    }

    // 13. Permission denied
    bool handlePermissionDenied(std::string_view log)
    {
      if (!contains(log, "Permission denied") &&
          !contains(log, "cannot create directory") &&
          !contains(log, "cannot open file") &&
          !contains(log, "Operation not permitted"))
      {
        return false;
      }

      // Try to extract a path from the error line.
      const std::string path = extract(
          log,
          std::regex(R"re((?:cannot create directory|cannot open file|Permission denied)[^\n]*?['\s]([/\\][^\s'\n]+))re"));

      print_error_title("filesystem permission denied");

      if (!path.empty())
        print_field("path: ", path);

      print_hint("check file permissions or choose a writable build directory");
      return true;
    }

    // 14. Generator mismatch
    bool handleGeneratorMismatch(std::string_view log)
    {
      const bool hasMismatch =
          contains(log, "does not match the generator used previously") ||
          (contains(log, "CMAKE_GENERATOR") &&
           contains(log, "CMake Error"));

      const bool hasGeneratorError =
          contains(log, "No such generator") ||
          contains(log, "Unknown generator");

      if (!hasMismatch && !hasGeneratorError)
        return false;

      print_error_title("CMake generator mismatch");
      print_hint("clean the build directory before switching generators");
      return true;
    }

    // 15. Invalid or unknown CMake option
    bool handleInvalidCMakeConfiguration(std::string_view log)
    {
      if (!contains(log, "Unknown argument") &&
          !contains(log, "Unknown CMake command") &&
          !contains(log, "is not a command") &&
          !contains(log, "Manually-specified variables were not used by the project"))
      {
        return false;
      }

      const std::string reason = extract(
          log,
          std::regex(R"re((?:Unknown argument|Unknown CMake command)[:\s]+"?([^\n"]+))re"));

      print_error_title("invalid CMake configuration");

      if (!reason.empty())
        print_field("reason: ", trim_copy(reason));

      print_hint("check the CMake option name or remove unused variables");
      return true;
    }

    // ----- Linker undefined symbol (not a CMake configure error, but kept) ----

    bool handleUndefinedSymbol(std::string_view log)
    {
      const bool hasUndefined =
          contains(log, "undefined symbol:") ||
          contains(log, "undefined reference");

      const bool hasLinker =
          contains(log, "mold: error:") ||
          contains(log, "ld:") ||
          contains(log, "collect2: error:");

      if (!hasUndefined || !hasLinker)
        return false;

      const std::string symbol = extract(
          log,
          std::regex(R"re(undefined symbol:\s*([^\n]+))re"));

      const std::string referencedBy = extract(
          log,
          std::regex(R"re(>>>\s+referenced by\s+([^\n]+))re"));

      vix::cli::build::BuildDiagnostic diagnostic;
      diagnostic.title = "Link failed";
      diagnostic.error =
          symbol.empty()
              ? "undefined symbol or undefined reference"
              : "undefined symbol: " + shorten_cpp_symbol(symbol);
      diagnostic.hint =
          "The symbol is declared and used, but no linked object or library "
          "provides its definition.";

      if (!referencedBy.empty())
        diagnostic.message = "Referenced by: " + referencedBy;

      vix::cli::build::print_build_diagnostic(std::cerr, diagnostic);
      return true;
    }

    // ----- Miscellaneous handlers kept from original -------------------------

    bool handleUnknownGenerator(std::string_view log)
    {
      if (!contains(log, "No such generator") &&
          !contains(log, "Unknown generator") &&
          !contains(log, "could not find ninja") &&
          !contains(log, "ninja: not found"))
      {
        return false;
      }

      const std::string generator = extract(
          log,
          std::regex(R"re((?:No such|Unknown) generator[:\s]+"?([^"\n]+)"?)re"));

      print_error_title("CMake generator not available");
      print_field("generator: ", generator);
      print_hint("install Ninja or switch to another generator such as Unix Makefiles");
      return true;
    }

    bool handleMissingCMakeModule(std::string_view log)
    {
      if (!contains(log, "include could not find") &&
          !contains(log, "Could not find load file"))
      {
        return false;
      }

      const std::string module = extract(
          log,
          std::regex(R"re(include could not find\s+(?:requested file|load file):\s*\n?\s*([^\s\n]+))re"));

      print_error_title("missing CMake module");
      print_colored_field("module: ", RED, module);
      print_hint("check CMAKE_MODULE_PATH or install the CMake module providing that file");
      return true;
    }

    bool handleDuplicateTarget(std::string_view log)
    {
      if (!contains(log, "add_library cannot create target") &&
          !contains(log, "add_executable cannot create target") &&
          !contains(log, "because another target with the same name already exists"))
      {
        return false;
      }

      const std::string target = extract(
          log,
          std::regex(R"re(cannot create target\s+"([^"]+)")re"));

      print_error_title("duplicate CMake target");
      print_colored_field("target: ", RED, target);
      print_hint("rename one target so every add_executable() or add_library() name is unique");
      return true;
    }

    bool handlePropertyOnNonexistentTarget(std::string_view log)
    {
      const bool referencesTargetCommand =
          contains(log, "set_target_properties") ||
          contains(log, "get_target_property") ||
          contains(log, "target_compile");

      const bool referencesMissingTarget =
          contains(log, "is not built by this project") ||
          contains(log, "does not exist");

      if (!referencesTargetCommand || !referencesMissingTarget)
        return false;

      const std::string target = extract(
          log,
          std::regex(R"re((?:target|Target)\s+"([^"]+)"\s+(?:is not|does not))re"));

      print_error_title("CMake target does not exist");
      print_colored_field("target: ", RED, target);
      print_hint("create the target before referencing it with target_* or set_target_properties()");
      return true;
    }

    bool handleToolchainNotFound(std::string_view log)
    {
      if (!contains(log, "toolchain") ||
          (!contains(log, "not found") && !contains(log, "does not exist")))
      {
        return false;
      }

      const std::string toolchain = extract(
          log,
          std::regex(R"re(toolchain file\s+"([^"]+)")re"));

      print_error_title("toolchain file not found");
      print_colored_field("toolchain: ", RED, toolchain);
      print_hint("check the path passed to CMAKE_TOOLCHAIN_FILE");
      return true;
    }

    // 16. Generic fallback — must run last
    //     Extracts the first real CMake Error block and never uses noise lines.
    bool handleGenericCMakeError(std::string_view log)
    {
      if (!contains(log, "CMake Error") &&
          !contains(log, "CMake Error at"))
      {
        return false;
      }

      const std::string file = extract(
          log,
          std::regex(R"re(CMake Error at ([^\s:]+):\d+)re"));

      const std::string lineNumber = extract(
          log,
          std::regex(R"re(CMake Error at [^\s:]+:(\d+))re"));

      const std::string message = first_cmake_error_message(log);

      print_error_title("CMake configure failed");

      if (!file.empty())
        print_field("at: ", file + ":" + lineNumber);

      print_field(
          "reason: ",
          message.empty() ? "unclassified CMake error" : message);

      print_hint("run vix build --verbose to inspect the full CMake output");
      return true;
    }

  } // anonymous namespace

  // ===========================================================================
  // Public entry point
  // ===========================================================================

  bool handleCMakeBuildError(std::string_view log)
  {
    // --- Order matters: specific handlers first, generic fallback last ---

    if (handle_unknown_ninja_target(log))
      return true; // 7
    if (handleCacheMismatch(log))
      return true; // 5
    if (handleMissingCMakeLists(log))
      return true; // 1
    if (handleCompilerNotFound(log))
      return true; // 4
    if (handleCMakeVersion(log))
      return true; // 11
    if (handleCMakeSyntaxError(log))
      return true; // 2
    if (handleCMakePolicy(log))
      return true; // 10
    if (handlePackageNotFound(log))
      return true; // 3
    if (handleMissingSourceFile(log))
      return true; // 6
    if (handleMissingLinkTarget(log))
      return true; // 8
    if (handleInstallExportMissingDependency(log))
      return true; // 9  ← fixed
    if (handleFetchContentError(log))
      return true; // 12
    if (handlePermissionDenied(log))
      return true; // 13
    if (handleGeneratorMismatch(log))
      return true; // 14
    if (handleInvalidCMakeConfiguration(log))
      return true; // 15
    if (handleUndefinedSymbol(log))
      return true;
    if (handleUnknownGenerator(log))
      return true;
    if (handleMissingCMakeModule(log))
      return true;
    if (handleDuplicateTarget(log))
      return true;
    if (handlePropertyOnNonexistentTarget(log))
      return true;
    if (handleToolchainNotFound(log))
      return true;
    if (handleGenericCMakeError(log))
      return true; // 16 fallback

    return false;
  }

} // namespace vix::cli::errors::build
