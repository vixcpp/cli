/**
 *
 *  @file CompileCommands.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  compile_commands.json parser
 *
 */

#ifndef VIX_CLI_BUILD_COMPILE_COMMANDS_HPP
#define VIX_CLI_BUILD_COMPILE_COMMANDS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief One entry from compile_commands.json.
   *
   * CMake/Ninja can generate compile_commands.json with the exact compiler
   * command used for every translation unit. Vix imports this data to build
   * graph tasks without guessing include paths, defines, flags, object paths,
   * or compiler arguments.
   */
  struct CompileCommandEntry
  {
    fs::path directory;                 ///< Working directory used by the compiler
    fs::path source;                    ///< Source file being compiled
    fs::path output;                    ///< Object file when available
    std::vector<std::string> arguments; ///< Compiler argv
    std::string rawCommand;             ///< Raw command string when provided

    /**
     * @brief Check whether this entry has enough data to create a compile task.
     *
     * @return true when directory, source and arguments are present
     */
    bool valid() const;

    /**
     * @brief Check whether the entry has an object output path.
     *
     * @return true when output is not empty
     */
    bool has_output() const;
  };

  /**
   * @brief Return the default compile_commands.json path for a build directory.
   *
   * @param buildDir Build directory
   * @return buildDir / compile_commands.json
   */
  fs::path default_compile_commands_path(const fs::path &buildDir);

  /**
   * @brief Split a compiler command string into argv tokens.
   *
   * This supports the shell forms usually emitted by CMake:
   *   - whitespace separated tokens
   *   - single quotes
   *   - double quotes
   *   - backslash escaping
   *
   * It does not expand environment variables, globs, or shell substitutions.
   *
   * @param command Raw command string
   * @return Parsed argv tokens
   */
  std::vector<std::string> split_compile_command(const std::string &command);

  /**
   * @brief Parse compile_commands.json content.
   *
   * The parser accepts both formats supported by the compilation database:
   *   - "command": "c++ ..."
   *   - "arguments": ["c++", "..."]
   *
   * @param text JSON content
   * @param sourcePath Path of the compile_commands.json file
   * @return Parsed entries, or std::nullopt on invalid JSON/schema
   */
  std::optional<std::vector<CompileCommandEntry>> parse_compile_commands_text(
      const std::string &text,
      const fs::path &sourcePath = {});

  /**
   * @brief Read and parse compile_commands.json from disk.
   *
   * @param path Path to compile_commands.json
   * @return Parsed entries, or std::nullopt when missing/invalid
   */
  std::optional<std::vector<CompileCommandEntry>> read_compile_commands(
      const fs::path &path);

  /**
   * @brief Resolve a possibly relative path against a base directory.
   *
   * @param base Base directory
   * @param path Path to resolve
   * @return Absolute normalized path when possible
   */
  fs::path resolve_compile_command_path(
      const fs::path &base,
      const fs::path &path);

  /**
   * @brief Extract the object output path from compiler argv.
   *
   * Supports:
   *   - -o file.o
   *   - -ofile.o
   *
   * @param arguments Compiler argv
   * @param workingDirectory Working directory used to resolve relative paths
   * @return Object path, or empty path if not found
   */
  fs::path extract_compile_output_path(
      const std::vector<std::string> &arguments,
      const fs::path &workingDirectory);

} // namespace vix::cli::build

#endif
