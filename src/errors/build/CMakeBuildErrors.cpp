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

#include <iostream>
#include <regex>
#include <string>
#include <string_view>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::build
{
  namespace
  {
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

    bool contains(std::string_view log, std::string_view needle)
    {
      return log.find(needle) != std::string_view::npos;
    }

    void printField(std::string_view label, std::string_view value)
    {
      if (value.empty())
      {
        return;
      }

      std::cerr << PAD << label << value << "\n";
    }

    void printColoredField(
        std::string_view label,
        const char *color,
        std::string_view value)
    {
      if (value.empty())
      {
        return;
      }

      std::cerr << PAD << label
                << color << value << RESET << "\n";
    }

    bool handleCacheMismatch(std::string_view log)
    {
      const bool hasCurrentCacheDir =
          contains(log, "The current CMakeCache.txt directory");

      const bool hasSourceMismatch =
          contains(log, "does not match the source") &&
          contains(log, "used to generate cache");

      if (!hasCurrentCacheDir && !hasSourceMismatch)
      {
        return false;
      }

      error("CMake configure failed: stale build cache detected.");
      hint("Your build directory was generated from another source location.");
      hint("Recommended: vix build --clean");
      hint("Manual fix: rm -rf build-ninja build-dev build-release");
      return true;
    }

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

      error("Build failed: unresolved CMake target.");
      printField("target: ", target);
      printColoredField("missing: ", RED, missing);
      blank(std::cerr);
      hint("Fix the target name or ensure it is defined before linking.");
      hint("If it is an imported target, call find_package() first.");
      return true;
    }

    bool handlePackageNotFound(std::string_view log)
    {
      if (!contains(log, "Could not find a package configuration file") &&
          !contains(log, "Could NOT find") &&
          !contains(log, "find_package") &&
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

      error("CMake configure failed: required package not found.");

      if (!packageName.empty())
      {
        printColoredField("package: ", RED, packageName);
      }
      else
      {
        printColoredField("package: ", RED, packageNameAlt);
      }

      blank(std::cerr);
      hint("Install the missing library (e.g. via apt, brew, vcpkg, conan).");
      hint("Or set the hint variable, e.g.: -DBoost_ROOT=/path/to/boost");
      hint("Check that the package provides a <Pkg>Config.cmake file.");
      return true;
    }

    bool handleCompilerNotFound(std::string_view log)
    {
      if (!contains(log, "No CMAKE_CXX_COMPILER") &&
          !contains(log, "No CMAKE_C_COMPILER") &&
          !contains(log, "Could not find compiler") &&
          !contains(log, "is not a full path to an existing compiler"))
      {
        return false;
      }

      error("CMake configure failed: compiler not found.");
      hint("Install a C/C++ compiler (gcc, clang, msvc).");
      hint("Or specify it explicitly: cmake -DCMAKE_CXX_COMPILER=clang++");
      hint("On Ubuntu: sudo apt install build-essential");
      return true;
    }

    bool handleCMakeVersion(std::string_view log)
    {
      if (!contains(log, "CMake") ||
          (!contains(log, "version") &&
           !contains(log, "cmake_minimum_required")))
      {
        return false;
      }

      if (!contains(log, "cmake_minimum_required") &&
          !contains(log, "does not match the requirement") &&
          !contains(log, "version required") &&
          !contains(log, "CMake Error: CMake"))
      {
        return false;
      }

      const std::string required = extract(
          log,
          std::regex(R"re(cmake_minimum_required\s*\(\s*VERSION\s+([\d.]+))re"));

      error("CMake configure failed: CMake version too old.");
      printField("required: ", required);
      hint("Upgrade CMake from the official CMake distribution.");
      hint("On Ubuntu: pip install cmake --upgrade");
      return true;
    }

    bool handleMissingCMakeLists(std::string_view log)
    {
      const bool missingSourceDirCMakeLists =
          contains(log, "The source directory") &&
          contains(log, "does not appear to contain CMakeLists.txt");

      const bool missingExplicitCMakeLists =
          contains(log, "CMake Error: The source directory") &&
          contains(log, "CMakeLists.txt");

      const bool missingPathCMakeLists =
          contains(log, "Source directory") &&
          contains(log, "does not exist") &&
          contains(log, "CMakeLists.txt");

      if (!missingSourceDirCMakeLists &&
          !missingExplicitCMakeLists &&
          !missingPathCMakeLists)
      {
        return false;
      }

      const std::string path = extract(
          log,
          std::regex(R"re((?:The source directory|Source directory)\s+"([^"]+)")re"));

      error("CMake configure failed: CMakeLists.txt not found.");

      if (!path.empty())
        printField("directory: ", path);

      hint("Check the source directory passed to CMake.");
      hint("Run with --verbose to inspect the full configure command.");
      return true;
    }

    bool handleCMakeSyntaxError(std::string_view log)
    {
      if (!contains(log, "Syntax error") &&
          !contains(log, "Parse error") &&
          !contains(log, "unexpected token") &&
          !contains(log, "Expected ')'") &&
          !contains(log, "while parsing"))
      {
        return false;
      }

      const std::string file = extract(
          log,
          std::regex(R"re(CMake Error at ([^\s:]+):(\d+))re"));

      const std::string line = extract(
          log,
          std::regex(R"re(CMake Error at [^\s:]+:(\d+))re"));

      error("CMake configure failed: syntax error in CMakeLists.txt.");

      if (!file.empty())
      {
        const std::string location = file + ":" + line;
        printField("file: ", location);
      }

      hint("Check for mismatched parentheses or invalid CMake syntax.");
      return true;
    }

    bool handleCMakePolicy(std::string_view log)
    {
      if (!contains(log, "CMP"))
      {
        return false;
      }

      if (!contains(log, "CMake Error") &&
          !contains(log, "cmake_policy") &&
          !contains(log, "Policy"))
      {
        return false;
      }

      const std::string policy = extract(
          log,
          std::regex(R"re((CMP\d{4}))re"));

      error("CMake configure failed: policy violation.");
      printField("policy: ", policy);

      if (!policy.empty())
      {
        hint("Add cmake_policy(SET " + policy + " NEW) to your CMakeLists.txt.");
      }

      hint("Or increase cmake_minimum_required(VERSION ...) to a newer version.");
      return true;
    }

    bool handleMissingSourceFile(std::string_view log)
    {
      if (!contains(log, "Cannot find source file") &&
          !contains(log, "does not exist") &&
          !contains(log, "no such file"))
      {
        return false;
      }

      if (!contains(log, ".cpp") &&
          !contains(log, ".c") &&
          !contains(log, ".cxx") &&
          !contains(log, ".cc"))
      {
        return false;
      }

      const std::string file = extract(
          log,
          std::regex(R"re(Cannot find source file:\s*\n?\s*([^\s\n]+))re"));

      error("CMake configure failed: source file not found.");
      printColoredField("file: ", RED, file);
      hint("Check the filename spelling in add_executable() or add_library().");
      hint("Make sure the file exists and the path is relative to CMakeLists.txt.");
      return true;
    }

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

      error("CMake configure failed: build system generator not available.");
      printField("generator: ", generator);
      hint("Install Ninja: sudo apt install ninja-build | brew install ninja");
      hint("Or switch generator: cmake -G \"Unix Makefiles\"");
      return true;
    }

    bool handleMissingCMakeModule(std::string_view log)
    {
      if (!contains(log, "include could not find") &&
          !contains(log, "find_file") &&
          !contains(log, "Could not find load file"))
      {
        return false;
      }

      const std::string module = extract(
          log,
          std::regex(R"re(include could not find\s+(?:requested file|load file):\s*\n?\s*([^\s\n]+))re"));

      error("CMake configure failed: missing CMake module or include file.");
      printColoredField("module: ", RED, module);
      hint("Check that the .cmake file exists in CMAKE_MODULE_PATH.");
      hint("Or install the CMake module providing that file.");
      return true;
    }

    bool handleUnknownOption(std::string_view log)
    {
      if (!contains(log, "Unrecognized option") &&
          !contains(log, "Unknown CMake command") &&
          !contains(log, "is not a command"))
      {
        return false;
      }

      const std::string command = extract(
          log,
          std::regex(R"re(Unknown CMake command\s+"([^"]+)")re"));

      error("CMake configure failed: unknown command or option.");
      printColoredField("command: ", RED, command);
      hint("Check spelling of the CMake command or variable.");
      hint("This may also happen when the CMake version is too old for this command.");
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

      error("CMake configure failed: duplicate target name.");
      printColoredField("target: ", RED, target);
      hint("Each add_executable() / add_library() name must be unique.");
      hint("Rename one of the conflicting targets.");
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
      {
        return false;
      }

      const std::string target = extract(
          log,
          std::regex(R"re((?:target|Target)\s+"([^"]+)"\s+(?:is not|does not))re"));

      error("CMake configure failed: property set on a non-existent target.");
      printColoredField("target: ", RED, target);
      hint("Make sure the target is created with add_executable() or add_library() before referencing it.");
      return true;
    }

    bool handleToolchainNotFound(std::string_view log)
    {
      if (!contains(log, "toolchain") ||
          (!contains(log, "not found") &&
           !contains(log, "does not exist")))
      {
        return false;
      }

      const std::string toolchain = extract(
          log,
          std::regex(R"re(toolchain file\s+"([^"]+)")re"));

      error("CMake configure failed: toolchain file not found.");
      printColoredField("toolchain: ", RED, toolchain);
      hint("Check the path passed to -DCMAKE_TOOLCHAIN_FILE=...");
      return true;
    }

    bool handleFetchContentError(std::string_view log)
    {
      if (!contains(log, "FetchContent") &&
          !contains(log, "ExternalProject"))
      {
        return false;
      }

      if (!contains(log, "Failed to download") &&
          !contains(log, "error: 6") &&
          !contains(log, "download failed") &&
          !contains(log, "error downloading"))
      {
        return false;
      }

      const std::string dependency = extract(
          log,
          std::regex(R"re(FetchContent[^\n]*?([A-Za-z0-9_\-]+)\s)re"));

      error("CMake configure failed: FetchContent / ExternalProject download error.");
      printField("dependency: ", dependency);
      hint("Check your internet connection.");
      hint("Verify the URL in FetchContent_Declare() is still valid.");
      hint("Use GIT_SHALLOW TRUE to speed up fetches.");
      return true;
    }

    bool handleGenericCMakeError(std::string_view log)
    {
      if (!contains(log, "CMake Error") &&
          !contains(log, "CMake Error at"))
      {
        return false;
      }

      const std::string file = extract(
          log,
          std::regex(R"re(CMake Error at ([^\s:]+):(\d+))re"));

      const std::string lineNumber = extract(
          log,
          std::regex(R"re(CMake Error at [^\s:]+:(\d+))re"));

      const std::string message = extract(
          log,
          std::regex(R"re(CMake Error[^\n]*\n\s*([^\n]+))re"));

      error("CMake configure failed.");

      if (!file.empty())
      {
        const std::string location = file + ":" + lineNumber;
        printField("at: ", location);
      }

      printField("reason: ", message);
      hint("Run cmake manually for full output: vix build --verbose");
      return true;
    }

  } // namespace

  bool handleCMakeBuildError(std::string_view log)
  {
    if (handleCacheMismatch(log))
    {
      return true;
    }

    if (handleMissingLinkTarget(log))
    {
      return true;
    }

    if (handlePackageNotFound(log))
    {
      return true;
    }

    if (handleCompilerNotFound(log))
    {
      return true;
    }

    if (handleCMakeVersion(log))
    {
      return true;
    }

    if (handleMissingCMakeLists(log))
    {
      return true;
    }

    if (handleCMakeSyntaxError(log))
    {
      return true;
    }

    if (handleCMakePolicy(log))
    {
      return true;
    }

    if (handleMissingSourceFile(log))
    {
      return true;
    }

    if (handleUnknownGenerator(log))
    {
      return true;
    }

    if (handleMissingCMakeModule(log))
    {
      return true;
    }

    if (handleUnknownOption(log))
    {
      return true;
    }

    if (handleDuplicateTarget(log))
    {
      return true;
    }

    if (handlePropertyOnNonexistentTarget(log))
    {
      return true;
    }

    if (handleToolchainNotFound(log))
    {
      return true;
    }

    if (handleFetchContentError(log))
    {
      return true;
    }

    if (handleGenericCMakeError(log))
    {
      return true;
    }

    return false;
  }

} // namespace vix::cli::errors::build
