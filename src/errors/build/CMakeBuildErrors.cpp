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

    // Extract the first non-empty, non-noise line that contains `marker`.
    // Useful for grabbing the single most informative compiler/linker line.
    std::string extract_first_line_with(std::string_view log,
                                        std::string_view marker)
    {
      const std::size_t pos = log.find(marker);
      if (pos == std::string_view::npos)
        return {};

      // Walk backwards to the start of the line.
      std::size_t start = pos;
      while (start > 0 && log[start - 1] != '\n')
        --start;

      // Walk forwards to the end of the line.
      std::size_t end = pos;
      while (end < log.size() && log[end] != '\n')
        ++end;

      return trim_copy(std::string(log.substr(start, end - start)));
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

    // [NEW 15] Corrupted CMake cache (parse error / load failure)
    //
    //   Must run BEFORE handleCacheMismatch — a corrupted cache is a different
    //   problem (file is unreadable, not just outdated) and needs a different
    //   hint.  Guard tightly to phrases that explicitly mention CMakeCache.txt
    //   so we don't fire on unrelated parse errors in user CMakeLists.txt.
    bool handleCorruptedCache(std::string_view log)
    {
      const bool hasParseError =
          (contains(log, "Parse error") && contains(log, "cache")) ||
          contains(log, "error parsing CMakeCache.txt") ||
          contains(log, "Error parsing CMakeCache.txt") ||
          contains(log, "Parse error in cache file") ||
          contains(log, "could not load cache") ||
          contains(log, "Could not load cache");

      if (!hasParseError)
        return false;

      print_error_title("corrupted CMake cache");
      print_hint("run vix build --clean or delete the build directory");
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

    // [NEW 1] Missing build tool (ninja / make / CMAKE_MAKE_PROGRAM)
    //
    //   Must run AFTER handleCompilerNotFound to keep compiler/build-tool
    //   diagnostics distinct, but BEFORE handleUnknownGenerator which uses
    //   some overlapping phrases ("ninja: not found").  Tightly guarded:
    //   require either an explicit CMAKE_MAKE_PROGRAM message or a clear
    //   "command not found" tied to a known build tool.
    bool handleMissingBuildTool(std::string_view log)
    {
      const bool hasCMakeMakeProgram =
          contains(log, "CMAKE_MAKE_PROGRAM is not set") ||
          contains(log, "CMAKE_MAKE_PROGRAM not set");

      const bool hasProgramNotFound =
          contains(log, "Program \"ninja\" not found") ||
          contains(log, "Program \"make\" not found") ||
          contains(log, "Program \"gmake\" not found");

      const bool hasShellNotFound =
          contains(log, "ninja: command not found") ||
          contains(log, "make: command not found") ||
          contains(log, "gmake: command not found");

      if (!hasCMakeMakeProgram && !hasProgramNotFound && !hasShellNotFound)
        return false;

      // Detect which tool is missing.
      std::string tool;
      if (contains(log, "ninja"))
        tool = "ninja";
      else if (contains(log, "gmake"))
        tool = "gmake";
      else if (contains(log, "make"))
        tool = "make";

      print_error_title("build tool not found");
      print_field("tool: ", tool);
      print_hint("install the selected build tool or choose another CMake generator");
      return true;
    }

    // [NEW 2] CMake configure compiler test failed
    //
    //   The classic "compiler is not able to compile a simple test program"
    //   path.  Run this BEFORE compile-time/link-time handlers because the
    //   sub-error inside the test build can look like a normal compile error.
    bool handleCompilerTestFailed(std::string_view log)
    {
      const bool hasCxxTest =
          contains(log, "The C++ compiler") &&
          contains(log, "is not able to compile a simple test program");

      const bool hasCTest =
          contains(log, "The C compiler") &&
          contains(log, "is not able to compile a simple test program");

      const bool hasAbiInfoFailed =
          contains(log, "Detecting CXX compiler ABI info - failed") ||
          contains(log, "Detecting C compiler ABI info - failed");

      if (!hasCxxTest && !hasCTest && !hasAbiInfoFailed)
        return false;

      print_error_title("compiler test failed");
      print_hint("check compiler installation, sysroot, standard library, "
                 "or linker configuration");
      return true;
    }

    // [NEW 3] Unsupported C++ standard
    //
    //   Run BEFORE the generic C++ compile-error handler — the cause here is
    //   the project's CXX_STANDARD setting, not the C++ source itself, so the
    //   hint must point at CMake, not the code.
    bool handleUnsupportedCxxStandard(std::string_view log)
    {
      const bool hasLanguageDialect =
          contains(log, "requires the language dialect") &&
          contains(log, "but CMake does not know the compile flags");

      const bool hasNoStandardSupport =
          contains(log, "compiler does not support CXX_STANDARD") ||
          contains(log, "does not support C++ standard");

      const bool hasInvalidStdValue =
          contains(log, "invalid value 'c++") ||
          contains(log, "invalid value \"c++") ||
          (contains(log, "unrecognized command-line option") &&
           contains(log, "-std=c++"));

      if (!hasLanguageDialect && !hasNoStandardSupport && !hasInvalidStdValue)
        return false;

      // Extract the standard mentioned, e.g. CXX23 / c++23 / 23.
      std::string standard = extract(
          log,
          std::regex(R"re(language dialect\s+"CXX(\d+)")re"));

      if (standard.empty())
        standard = extract(log, std::regex(R"re(-std=c\+\+(\d+))re"));

      if (standard.empty())
        standard = extract(log, std::regex(R"re('c\+\+(\d+)')re"));

      print_error_title("unsupported C++ standard");
      if (!standard.empty())
        print_field("standard: ", "C++" + standard);
      print_hint("upgrade the compiler or lower CMAKE_CXX_STANDARD");
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

    // [NEW 11] Dependency version mismatch
    //
    //   Run BEFORE handlePackageNotFound — the package WAS found, just at the
    //   wrong version, which is a different fix.  Tight match on
    //   PACKAGE_VERSION_UNSUITABLE or explicit "not compatible" phrasing.
    bool handleDependencyVersionMismatch(std::string_view log)
    {
      const bool hasUnsuitable =
          contains(log, "PACKAGE_VERSION_UNSUITABLE") ||
          contains(log, "but it set PACKAGE_VERSION_UNSUITABLE");

      const bool hasNotCompatible =
          contains(log, "is not compatible") &&
          (contains(log, "version") || contains(log, "Version"));

      const bool hasRequestedVersion =
          contains(log, "Could not find a configuration file") &&
          contains(log, "requested version");

      if (!hasUnsuitable && !hasNotCompatible && !hasRequestedVersion)
        return false;

      const std::string package = extract(
          log,
          std::regex(R"re(Could not find a configuration file for package "([^"]+)")re"));

      const std::string packageAlt = extract(
          log,
          std::regex(R"re(Could not find a package configuration file provided by "([^"]+)")re"));

      const std::string required = extract(
          log,
          std::regex(R"re(requested version "?([\d.]+))re"));

      const std::string found = extract(
          log,
          std::regex(R"re((?:found version|VERSION)\s*"?([\d.]+))re"));

      print_error_title("dependency version mismatch");
      print_colored_field("package: ", RED, package.empty() ? packageAlt : package);
      print_field("required: ", required);
      print_field("found: ", found);
      print_hint("install a compatible dependency version or update the project requirement");
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

    // [NEW 12] Broken imported target
    //
    //   The package's CMake config files were found but reference a missing
    //   library binary.  Different fix from "package not found", so run after
    //   it.  Distinguishing phrase is "imported target ... references the file".
    bool handleBrokenImportedTarget(std::string_view log)
    {
      const bool hasImportedRef =
          contains(log, "imported target") &&
          contains(log, "references the file") &&
          contains(log, "but this file does not exist");

      const bool hasImportedLocation =
          contains(log, "IMPORTED_LOCATION") &&
          (contains(log, "does not exist") ||
           contains(log, "references missing file"));

      if (!hasImportedRef && !hasImportedLocation)
        return false;

      const std::string target = extract(
          log,
          std::regex(R"re(imported target "([^"]+)")re"));

      const std::string file = extract(
          log,
          std::regex(R"re(references the file\s*\n?\s*"([^"]+)")re"));

      print_error_title("broken imported CMake target");
      print_colored_field("target: ", RED, target);
      print_field("file: ", file);
      print_hint("reinstall the package or fix the imported target configuration");
      return true;
    }

    // [NEW 13] Submodule / dependency source directory missing
    //
    //   Run before handleMissingSourceFile — different error class.  The
    //   add_subdirectory() failure points at uninitialized submodules, not at
    //   a missing single source file.
    bool handleMissingSubmoduleSource(std::string_view log)
    {
      // Note: CMake hard-wraps long diagnostic messages, so we must match on
      // sub-phrases that survive the wrap (e.g. "which is not an existing"
      // rather than "which is not an existing directory").
      const bool hasAddSubdir =
          contains(log, "add_subdirectory given source") &&
          contains(log, "which is not an existing");

      const bool hasMissingDepsDir =
          contains(log, "The source directory") &&
          contains(log, "does not exist") &&
          !contains(log, "does not appear to contain CMakeLists.txt");

      if (!hasAddSubdir && !hasMissingDepsDir)
        return false;

      std::string directory = extract(
          log,
          std::regex(R"re(add_subdirectory given source\s+"([^"]+)")re"));

      if (directory.empty())
        directory = extract(
            log,
            std::regex(R"re(The source directory\s+"([^"]+)"\s+does not exist)re"));

      print_error_title("dependency source directory missing");
      print_colored_field("directory: ", RED, directory);
      print_hint("initialize submodules (git submodule update --init --recursive) "
                 "or check the dependency path");
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

    // [NEW 9] Missing generated file (ninja "No rule to make target")
    //
    //   Run BEFORE the generic CMake fallback but AFTER handleMissingSourceFile
    //   so that explicit "Cannot find source file" errors get the more precise
    //   diagnostic.  Common when a custom_command output is wrong.
    bool handleMissingGeneratedFile(std::string_view log)
    {
      const bool hasNoRule =
          contains(log, "No rule to make target") ||
          contains(log, "no known rule to make it");

      const bool hasMissingCustom =
          contains(log, "file generated by custom command is missing") ||
          (contains(log, "Cannot find source file") &&
           contains(log, "generated"));

      if (!hasNoRule && !hasMissingCustom)
        return false;

      std::string file = extract(
          log,
          std::regex(R"re(No rule to make target\s+['"]([^'"]+)['"])re"));

      // ninja form: "'generated/foo.cpp', needed by '...', missing and no known rule"
      if (file.empty())
        file = extract(
            log,
            std::regex(R"re(ninja:\s*error:\s*['"]([^'"]+)['"]\s*,\s*needed by)re"));

      if (file.empty())
        file = extract(
            log,
            std::regex(R"re(file generated by custom command is missing[:\s]+['"]?([^'"\n]+)['"]?)re"));

      print_error_title("generated build file missing");
      print_colored_field("file: ", RED, file);
      print_hint("check custom commands, generated outputs, or clean and rebuild");
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

    // 9. install(EXPORT ...) dependency not in export set
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

    // [NEW 10] CMake install prefix / write error
    //
    //   Different from generic "permission denied" because this is specifically
    //   about install() failing on the destination path.  Run BEFORE
    //   handlePermissionDenied so we give the CMAKE_INSTALL_PREFIX-specific hint.
    bool handleInstallPathError(std::string_view log)
    {
      const bool hasInstallFailure =
          contains(log, "file INSTALL cannot make directory") ||
          contains(log, "file INSTALL cannot copy") ||
          (contains(log, "file failed to open for writing") &&
           contains(log, "install"));

      const bool hasInstallCannotCreate =
          contains(log, "cannot create directory") &&
          (contains(log, "install") || contains(log, "/usr/local") ||
           contains(log, "Permission denied"));

      if (!hasInstallFailure && !hasInstallCannotCreate)
        return false;

      // Try to grab the offending path from typical CMake INSTALL error formats.
      std::string path = extract(
          log,
          std::regex(R"re(cannot make directory\s+"([^"]+)")re"));

      if (path.empty())
        path = extract(
            log,
            std::regex(R"re(cannot copy file[^\n]*to\s+"([^"]+)")re"));

      if (path.empty())
        path = extract(
            log,
            std::regex(R"re(failed to open for writing\s*\(?[^\)]*\)?\s*:?\s*([/\\][^\s\n]+))re"));

      print_error_title("install path is not writable");
      print_field("path: ", path);
      print_hint("use a writable CMAKE_INSTALL_PREFIX or run install with proper permissions");
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

    // [NEW 14] CMake preset / configuration preset error
    //
    //   Run BEFORE handleInvalidCMakeConfiguration, which is a broader catch.
    bool handleInvalidPreset(std::string_view log)
    {
      const bool hasPresetError =
          contains(log, "Could not read presets from") ||
          contains(log, "Invalid preset") ||
          contains(log, "configure preset is invalid") ||
          contains(log, "No such preset") ||
          (contains(log, "preset") && contains(log, "not found") &&
           contains(log, "CMakePresets"));

      if (!hasPresetError)
        return false;

      std::string preset = extract(
          log,
          std::regex(R"re(No such preset[^"']*['"]([^'"]+)['"])re"));

      if (preset.empty())
        preset = extract(log, std::regex(R"re(Invalid preset[^"']*['"]([^'"]+)['"])re"));

      if (preset.empty())
        preset = extract(log, std::regex(R"re(preset[^"']*['"]([^'"]+)['"]\s+not found)re"));

      print_error_title("invalid CMake preset");
      print_colored_field("preset: ", RED, preset);
      print_hint("check CMakePresets.json or choose an existing preset");
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

    // -------------------------------------------------------------------------
    // Compile-stage handlers (run AFTER all CMake-configure-stage handlers)
    // -------------------------------------------------------------------------

    // [NEW 4] Missing header during compile
    //
    //   Must run BEFORE handleCppCompileError — a missing header is a compile
    //   error too, but the actionable hint is completely different (install
    //   dep / fix include path vs fix the source).
    bool handleMissingHeader(std::string_view log)
    {
      const bool hasGccFatal =
          contains(log, "fatal error:") &&
          contains(log, "No such file or directory");

      const bool hasMsvcC1083 =
          contains(log, "fatal error C1083") &&
          contains(log, "Cannot open include file");

      if (!hasGccFatal && !hasMsvcC1083)
        return false;

      // GCC/Clang form: "fatal error: foo/bar.hpp: No such file or directory"
      std::string header = extract(
          log,
          std::regex(R"re(fatal error:\s*([^\s:]+(?:\.h|\.hpp|\.hxx|\.hh|\.H|\.inc)?)\s*:\s*No such file or directory)re"));

      // MSVC form: "Cannot open include file: 'foo/bar.hpp'"
      if (header.empty())
        header = extract(
            log,
            std::regex(R"re(Cannot open include file:\s*'([^']+)')re"));

      print_error_title("header file not found");
      print_colored_field("header: ", RED, header);
      print_hint("add the correct include directory, install the dependency, "
                 "or fix the include path");
      return true;
    }

    // [NEW 5] C++ compilation error
    //
    //   Generic compile-error catch.  Must run AFTER handleMissingHeader (more
    //   specific) and AFTER handleUnsupportedCxxStandard.  Must run BEFORE
    //   linker handlers because compile errors typically appear in the same
    //   log as a final "linker did not run" line on some build systems.
    //
    //   Tightly guarded: require a "error:" token plus at least one of the
    //   classic C++ diagnostic phrases — looking for "error:" alone is too
    //   permissive (CMake itself prints "CMake Error:").
    bool handleCppCompileError(std::string_view log)
    {
      const bool hasErrorMarker =
          contains(log, "error:") || contains(log, "error C");

      if (!hasErrorMarker)
        return false;

      const bool hasCppDiagnostic =
          contains(log, "expected ';'") ||
          contains(log, "expected '}'") ||
          contains(log, "expected ')'") ||
          contains(log, "no matching function for call to") ||
          contains(log, "use of deleted function") ||
          contains(log, "use of undeclared identifier") ||
          contains(log, "cannot convert") ||
          contains(log, "was not declared in this scope") ||
          contains(log, "redefinition of") ||
          contains(log, "no member named") ||
          contains(log, "is private within this context") ||
          contains(log, "template argument deduction") ||
          // MSVC compile errors C2001..C3999
          contains(log, "error C2") ||
          contains(log, "error C3");

      if (!hasCppDiagnostic)
        return false;

      // Extract file:line from a GCC/Clang-style "path/to/file.cpp:42:17: error:"
      const std::string location = extract(
          log,
          std::regex(R"re(([^\s:]+\.(?:cpp|cc|cxx|c\+\+|c|h|hpp|hxx|hh)):(\d+)(?::\d+)?:\s*error:)re"));

      const std::string lineNum = extract(
          log,
          std::regex(R"re([^\s:]+\.(?:cpp|cc|cxx|c\+\+|c|h|hpp|hxx|hh):(\d+)(?::\d+)?:\s*error:)re"));

      // Build a "file:line" form.
      std::string at;
      if (!location.empty())
      {
        at = location;
        if (!lineNum.empty())
          at += ":" + lineNum;
      }

      // The first compiler error line itself.
      const std::string reasonLine = extract_first_line_with(log, "error:");

      print_error_title("C++ compilation failed");
      print_field("at: ", at);
      print_field("reason: ", reasonLine);
      print_hint("fix the C++ source error shown above");
      return true;
    }

    // -------------------------------------------------------------------------
    // Link-stage handlers
    // -------------------------------------------------------------------------

    // [NEW 6] Missing library during link
    //
    //   Must run BEFORE handleUndefinedSymbol — a missing -lfoo causes
    //   undefined-symbol cascades but the root cause and hint differ.
    bool handleMissingLinkedLibrary(std::string_view log)
    {
      const bool hasCannotFind =
          contains(log, "cannot find -l") ||
          contains(log, "ld: cannot find -l");

      const bool hasLibraryNotFound =
          contains(log, "library not found for -l");

      if (!hasCannotFind && !hasLibraryNotFound)
        return false;

      std::string library = extract(
          log,
          std::regex(R"re(cannot find\s+-l([^\s\n'"]+))re"));

      if (library.empty())
        library = extract(
            log,
            std::regex(R"re(library not found for\s+-l([^\s\n'"]+))re"));

      print_error_title("linked library not found");
      print_colored_field("library: ", RED, library);
      print_hint("install the library or update the link directories/pkg-config/CMake package config");
      return true;
    }

    // [NEW 7] Multiple definition / duplicate symbol
    //
    //   Must run BEFORE handleUndefinedSymbol so the more specific linker error
    //   wins.  Both can appear in the same link, but multiple-definition is
    //   strictly more informative.
    bool handleMultipleDefinition(std::string_view log)
    {
      const bool hasMultipleDef =
          contains(log, "multiple definition of");

      const bool hasDuplicateSym =
          contains(log, "duplicate symbol");

      if (!hasMultipleDef && !hasDuplicateSym)
        return false;

      // GNU ld form: "multiple definition of `symbol'"
      std::string symbol = extract(
          log,
          std::regex(R"re(multiple definition of\s+[`']([^'`]+)['`])re"));

      // mach-o / clang form: "duplicate symbol 'symbol' in:"
      if (symbol.empty())
        symbol = extract(
            log,
            std::regex(R"re(duplicate symbol\s+[`']?_?([^\s'`\n]+)['`]?)re"));

      print_error_title("duplicate symbol");
      if (!symbol.empty())
        print_colored_field("symbol: ", RED, shorten_cpp_symbol(symbol));
      print_hint("move definitions from headers to source files, mark inline when "
                 "appropriate, or avoid linking the same object twice");
      return true;
    }

    // [NEW 8] Architecture / ABI mismatch
    //
    //   Run AFTER multiple-definition and library-not-found because those are
    //   more specific link failures.  Run BEFORE handleUndefinedSymbol since
    //   an arch mismatch also produces undefined references.
    bool handleArchitectureMismatch(std::string_view log)
    {
      const bool hasWrongFormat =
          contains(log, "file in wrong format") ||
          contains(log, "wrong ELF class") ||
          contains(log, "incompatible target") ||
          contains(log, "is incompatible with") ||
          contains(log, "skipping incompatible") ||
          contains(log, "cputype") /* macOS arch mismatch */;

      if (!hasWrongFormat)
        return false;

      print_error_title("binary architecture mismatch");
      print_hint("clean the build and ensure all libraries are built for the "
                 "same architecture/toolchain");
      return true;
    }

    // ----- Linker undefined symbol (kept from original) ---------------------

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
  //
  // Order rationale (specific → generic):
  //
  //   1. CMake-configure-stage handlers run first because their phrases are
  //      most specific and they would otherwise be swallowed by the generic
  //      CMake fallback.
  //   2. Within the configure stage, more-specific patterns precede broader
  //      ones (e.g. dependency version mismatch before package-not-found,
  //      corrupted cache before stale cache, broken imported target after
  //      package-not-found).
  //   3. Compile-stage handlers (missing header, C++ compile error) run after
  //      configure-stage handlers — they can also appear inside CMake's
  //      "try_compile" test, but a configure-test failure handler already
  //      catches that wrapper case.
  //   4. Link-stage handlers (missing library, multiple definition, arch
  //      mismatch, undefined symbol) run after compile-stage handlers.
  //   5. The remaining miscellaneous handlers and finally the generic CMake
  //      fallback run last.
  //
  bool handleCMakeBuildError(std::string_view log)
  {
    // --- Ninja-level errors -------------------------------------------------
    if (handle_unknown_ninja_target(log))
      return true; // 7

    // --- CMake cache state (corrupted is more specific than stale) ---------
    if (handleCorruptedCache(log))
      return true; // NEW 15
    if (handleCacheMismatch(log))
      return true; // 5

    // --- Source / project layout -------------------------------------------
    if (handleMissingCMakeLists(log))
      return true; // 1

    // --- Compiler / build-tool availability --------------------------------
    if (handleCompilerNotFound(log))
      return true; // 4
    if (handleMissingBuildTool(log))
      return true; // NEW 1
    if (handleCompilerTestFailed(log))
      return true; // NEW 2
    if (handleUnsupportedCxxStandard(log))
      return true; // NEW 3

    // --- CMake version / syntax / policy -----------------------------------
    if (handleCMakeVersion(log))
      return true; // 11
    if (handleCMakeSyntaxError(log))
      return true; // 2
    if (handleCMakePolicy(log))
      return true; // 10

    // --- Package resolution (version-mismatch before not-found before
    //     broken-imported, since each is progressively less specific) -------
    if (handleDependencyVersionMismatch(log))
      return true; // NEW 11
    if (handlePackageNotFound(log))
      return true; // 3
    if (handleBrokenImportedTarget(log))
      return true; // NEW 12

    // --- Source files / submodules / generated files -----------------------
    if (handleMissingSubmoduleSource(log))
      return true; // NEW 13
    if (handleMissingSourceFile(log))
      return true; // 6
    if (handleMissingGeneratedFile(log))
      return true; // NEW 9

    // --- CMake targets and export sets -------------------------------------
    if (handleMissingLinkTarget(log))
      return true; // 8
    if (handleInstallExportMissingDependency(log))
      return true; // 9
    if (handleInstallPathError(log))
      return true; // NEW 10

    // --- Dependency fetching / presets / generator / config ----------------
    if (handleFetchContentError(log))
      return true; // 12
    if (handleInvalidPreset(log))
      return true; // NEW 14
    if (handlePermissionDenied(log))
      return true; // 13
    if (handleGeneratorMismatch(log))
      return true; // 14
    if (handleInvalidCMakeConfiguration(log))
      return true; // 15

    // --- Compile-stage errors (header more specific than generic C++) -----
    if (handleMissingHeader(log))
      return true; // NEW 4
    if (handleCppCompileError(log))
      return true; // NEW 5

    // --- Link-stage errors (missing lib & duplicate sym & arch before
    //     the generic undefined-symbol catch) ---------------------------
    if (handleMissingLinkedLibrary(log))
      return true; // NEW 6
    if (handleMultipleDefinition(log))
      return true; // NEW 7
    if (handleArchitectureMismatch(log))
      return true; // NEW 8
    if (handleUndefinedSymbol(log))
      return true;

    // --- Misc + generic fallback (must remain last) ------------------------
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
