/**
 *
 *  @file DependencyFile.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  GCC/Clang dependency file parser
 *
 */

#ifndef VIX_CLI_BUILD_DEPENDENCY_FILE_HPP
#define VIX_CLI_BUILD_DEPENDENCY_FILE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vix::cli::build
{
  namespace fs = std::filesystem;

  /**
   * @brief Parsed content of a compiler dependency file.
   *
   * GCC and Clang can emit dependency files with:
   *
   *   -MMD -MP -MF file.d
   *
   * A dependency file usually looks like:
   *
   *   build/main.o: src/main.cpp include/app.hpp include/config.hpp
   *
   * The first entry is the target object file. The following entries are
   * source/header dependencies used to decide whether the object must be
   * rebuilt.
   */
  struct DependencyFile
  {
    fs::path path;                      ///< Path to the .d dependency file
    fs::path target;                    ///< Target produced by the dependency rule
    std::vector<fs::path> dependencies; ///< Files required by the target

    /**
     * @brief Check whether this dependency file has usable data.
     *
     * @return true if target and dependencies are not empty
     */
    bool valid() const;

    /**
     * @brief Check whether a dependency path is already registered.
     *
     * @param dependency Dependency path
     * @return true if the dependency exists in the list
     */
    bool has_dependency(const fs::path &dependency) const;

    /**
     * @brief Add a dependency path if not already present.
     *
     * Empty paths are ignored.
     *
     * @param dependency Dependency path
     */
    void add_dependency(const fs::path &dependency);
  };

  /**
   * @brief Normalize a dependency path for stable comparison.
   *
   * This keeps paths deterministic across parser calls by applying
   * lexically_normal() and generic_string() style normalization.
   *
   * @param path Path to normalize
   * @return Normalized path
   */
  fs::path normalize_dependency_path(const fs::path &path);

  /**
   * @brief Parse a GCC/Clang dependency file from text.
   *
   * This parser supports:
   *   - line continuations using backslash-newline
   *   - escaped spaces using backslash-space
   *   - generated phony rules from -MP
   *   - multiple dependency tokens
   *
   * @param text Dependency file content
   * @param dependencyFilePath Optional source .d path for diagnostics/storage
   * @return Parsed dependency file, or std::nullopt if invalid
   */
  std::optional<DependencyFile> parse_dependency_file_text(
      const std::string &text,
      const fs::path &dependencyFilePath = {});

  /**
   * @brief Parse a GCC/Clang dependency file from disk.
   *
   * @param path Path to the .d dependency file
   * @return Parsed dependency file, or std::nullopt if missing/invalid
   */
  std::optional<DependencyFile> read_dependency_file(const fs::path &path);

  /**
   * @brief Convert a source path to its dependency file path.
   *
   * Example:
   *
   *   build/objects/src/main.o -> build/objects/src/main.d
   *
   * @param objectPath Object file path
   * @return Dependency file path
   */
  fs::path dependency_file_for_object(const fs::path &objectPath);

} // namespace vix::cli::build

#endif
