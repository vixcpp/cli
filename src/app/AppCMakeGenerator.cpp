/**
 *
 *  @file AppCMakeGenerator.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  CMake generator for simple vix.app projects.
 *
 */

#include <vix/cli/app/AppCMakeGenerator.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vix::cli::app
{
  namespace
  {
    // ---------------------------------------------------------------
    // Quoting and path helpers
    // ---------------------------------------------------------------

    static std::string cmake_quote(const std::string &value)
    {
      std::string out;
      out.reserve(value.size() + 8);
      out.push_back('"');

      for (char c : value)
      {
        if (c == '\\')
          out += "\\\\";
        else if (c == '"')
          out += "\\\"";
        else
          out.push_back(c);
      }

      out.push_back('"');
      return out;
    }

    static std::string cmake_path(const fs::path &path)
    {
      return path.lexically_normal().generic_string();
    }

    static std::string cmake_quoted_path(const fs::path &path)
    {
      return cmake_quote(cmake_path(path));
    }

    static fs::path absolute_project_path(
        const fs::path &projectDir,
        const std::string &relativePath)
    {
      const fs::path path(relativePath);

      if (path.is_absolute())
        return path.lexically_normal();

      return (projectDir / path).lexically_normal();
    }

    static std::string lower_copy(std::string value)
    {
      for (char &c : value)
      {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
      }

      return value;
    }

    // ---------------------------------------------------------------
    // Standard normalization
    // ---------------------------------------------------------------

    static int cpp_standard_number(const std::string &standard)
    {
      const std::string s = lower_copy(standard);

      if (s == "c++11" || s == "cpp11" || s == "11")
        return 11;

      if (s == "c++14" || s == "cpp14" || s == "14")
        return 14;

      if (s == "c++17" || s == "cpp17" || s == "17")
        return 17;

      if (s == "c++20" || s == "cpp20" || s == "20")
        return 20;

      if (s == "c++23" || s == "cpp23" || s == "23")
        return 23;

      if (s == "c++26" || s == "cpp26" || s == "26")
        return 26;

      return 20;
    }

    // ---------------------------------------------------------------
    // Atomic file write
    // ---------------------------------------------------------------

    static bool write_text_file_atomic(
        const fs::path &path,
        const std::string &content)
    {
      const fs::path parent = path.parent_path();

      if (!parent.empty())
      {
        std::error_code ec;
        fs::create_directories(parent, ec);
      }

      const fs::path tmp = path.string() + ".tmp";

      {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);

        if (!out)
          return false;

        out << content;

        if (!out)
          return false;
      }

      std::error_code ec;
      fs::rename(tmp, path, ec);

      if (!ec)
        return true;

      fs::remove(path, ec);
      ec.clear();

      fs::rename(tmp, path, ec);

      return !ec;
    }

    // ---------------------------------------------------------------
    // Target type helpers
    // ---------------------------------------------------------------

    static std::string cmake_target_command(AppTargetType type)
    {
      switch (type)
      {
      case AppTargetType::Executable:
        return "add_executable";
      case AppTargetType::StaticLibrary:
        return "add_library";
      case AppTargetType::SharedLibrary:
        return "add_library";
      default:
        return "add_executable";
      }
    }

    static std::string cmake_library_kind(AppTargetType type)
    {
      switch (type)
      {
      case AppTargetType::StaticLibrary:
        return " STATIC";
      case AppTargetType::SharedLibrary:
        return " SHARED";
      case AppTargetType::Executable:
      default:
        return "";
      }
    }

    // ---------------------------------------------------------------
    // Generic emit helpers
    // ---------------------------------------------------------------

    static void emit_quoted_list(
        std::ostringstream &out,
        const std::string &command,
        const std::string &targetName,
        const std::vector<std::string> &values)
    {
      if (values.empty())
        return;

      out << command << "(" << targetName << " PRIVATE\n";

      for (const std::string &value : values)
        out << "  " << cmake_quote(value) << "\n";

      out << ")\n\n";
    }

    // Emit a list without quoting each entry. Used for CMake items that
    // must remain syntactically meaningful — imported targets such as
    // Threads::Threads or fmt::fmt, generator expressions, raw compiler
    // and linker flags, and compile features like cxx_std_23. Quoting
    // these would either cause CMake to treat them as plain strings or
    // suppress flag expansion.
    static void emit_raw_list(
        std::ostringstream &out,
        const std::string &command,
        const std::string &targetName,
        const std::vector<std::string> &values)
    {
      if (values.empty())
        return;

      out << command << "(" << targetName << " PRIVATE\n";

      for (const std::string &value : values)
        out << "  " << value << "\n";

      out << ")\n\n";
    }

    static void emit_path_list(
        std::ostringstream &out,
        const std::string &command,
        const std::string &targetName,
        const fs::path &projectDir,
        const std::vector<std::string> &values)
    {
      if (values.empty())
        return;

      out << command << "(" << targetName << " PRIVATE\n";

      for (const std::string &value : values)
      {
        const fs::path resolved = absolute_project_path(projectDir, value);
        out << "  " << cmake_quoted_path(resolved) << "\n";
      }

      out << ")\n\n";
    }

    // ---------------------------------------------------------------
    // Section emitters
    // ---------------------------------------------------------------

    static void emit_header(std::ostringstream &out)
    {
      out << "# Auto-generated by Vix from vix.app\n";
      out << "# Do not edit this file directly.\n\n";
      out << "cmake_minimum_required(VERSION 3.24)\n\n";
    }

    static void emit_project(
        std::ostringstream &out,
        const std::string &targetName)
    {
      out << "project(" << targetName << " LANGUAGES CXX)\n\n";
    }

    static void emit_standard(
        std::ostringstream &out,
        const std::string &standard)
    {
      const int number = cpp_standard_number(standard);

      out << "set(CMAKE_CXX_STANDARD " << number << ")\n";
      out << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
      out << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
    }

    static bool is_threads_package(const AppPackage &pkg)
    {
      return lower_copy(pkg.name) == "threads";
    }

    static void emit_packages(
        std::ostringstream &out,
        const std::vector<AppPackage> &packages)
    {
      if (packages.empty())
        return;

      // `packages` only emits find_package() calls. Imported targets
      // produced by these packages (e.g. Threads::Threads, fmt::fmt)
      // must be added explicitly to `links` in the manifest. Keeping
      // discovery and linking separate makes the manifest predictable.
      out << "# Packages declared in vix.app\n";
      out << "# Imported targets must be listed explicitly under `links`.\n";

      for (const AppPackage &pkg : packages)
      {
        if (is_threads_package(pkg))
        {
          // Threads is special: this hint must be set before
          // find_package(Threads) for the pthread flag to be picked.
          out << "set(THREADS_PREFER_PTHREAD_FLAG ON)\n";
        }

        out << "find_package(" << pkg.name;

        if (!pkg.components.empty())
        {
          out << " COMPONENTS";
          for (const std::string &component : pkg.components)
            out << " " << component;
        }

        if (pkg.required)
          out << " REQUIRED";

        out << ")\n";
      }

      out << "\n";
    }

    static void emit_target(
        std::ostringstream &out,
        const AppManifest &manifest,
        const std::string &targetName,
        const fs::path &projectDir)
    {
      out << cmake_target_command(manifest.type)
          << "("
          << targetName
          << cmake_library_kind(manifest.type)
          << "\n";

      for (const std::string &source : manifest.sources)
      {
        const fs::path resolved = absolute_project_path(projectDir, source);
        out << "  " << cmake_quoted_path(resolved) << "\n";
      }

      out << ")\n\n";
    }

    static void emit_includes_and_defines(
        std::ostringstream &out,
        const AppManifest &manifest,
        const std::string &targetName,
        const fs::path &projectDir)
    {
      emit_path_list(
          out,
          "target_include_directories",
          targetName,
          projectDir,
          manifest.includeDirs);

      emit_quoted_list(
          out,
          "target_compile_definitions",
          targetName,
          manifest.defines);
    }

    static void emit_options_and_features(
        std::ostringstream &out,
        const AppManifest &manifest,
        const std::string &targetName)
    {
      emit_raw_list(
          out,
          "target_compile_options",
          targetName,
          manifest.compileOptions);

      emit_raw_list(
          out,
          "target_link_options",
          targetName,
          manifest.linkOptions);

      emit_raw_list(
          out,
          "target_compile_features",
          targetName,
          manifest.compileFeatures);
    }

    static void emit_links(
        std::ostringstream &out,
        const AppManifest &manifest,
        const std::string &targetName)
    {
      // Linking is intentionally separate from `packages`. The
      // `packages` section only emits `find_package(...)`; users are
      // responsible for listing the resulting imported targets (for
      // example `Threads::Threads` or `fmt::fmt`) under `links`. This
      // keeps the manifest's mental model simple and predictable.
      emit_raw_list(
          out,
          "target_link_libraries",
          targetName,
          manifest.links);
    }

    static void emit_output_dir(
        std::ostringstream &out,
        const AppManifest &manifest,
        const std::string &targetName)
    {
      if (manifest.outputDir.empty())
        return;

      // Relative output directories are anchored to ${CMAKE_BINARY_DIR}
      // so the layout does not depend on where CMake is invoked from.
      // Absolute paths are used as-is.
      const fs::path raw(manifest.outputDir);
      const std::string normalized =
          fs::path(manifest.outputDir).lexically_normal().generic_string();

      const std::string anchored =
          raw.is_absolute()
              ? normalized
              : ("${CMAKE_BINARY_DIR}/" + normalized);

      const std::string quoted = cmake_quote(anchored);

      out << "# Output directory declared in vix.app\n";
      out << "set_target_properties(" << targetName << " PROPERTIES\n";
      out << "  RUNTIME_OUTPUT_DIRECTORY " << quoted << "\n";
      out << "  LIBRARY_OUTPUT_DIRECTORY " << quoted << "\n";
      out << "  ARCHIVE_OUTPUT_DIRECTORY " << quoted << "\n";
      out << ")\n\n";
    }

    static std::string resource_basename(const std::string &source)
    {
      const fs::path path(source);
      return path.filename().generic_string();
    }

    static void emit_resources(
        std::ostringstream &out,
        const AppManifest &manifest,
        const std::string &targetName,
        const fs::path &projectDir)
    {
      if (manifest.resources.empty())
        return;

      out << "# Resources declared in vix.app\n";

      for (const AppResource &res : manifest.resources)
      {
        const fs::path absoluteSource =
            absolute_project_path(projectDir, res.source);

        const std::string destName =
            res.destination.empty()
                ? resource_basename(res.source)
                : res.destination;

        const std::string quotedSrc = cmake_quoted_path(absoluteSource);
        const std::string dstPath =
            "$<TARGET_FILE_DIR:" + targetName + ">/" + destName;
        const std::string quotedDst = cmake_quote(dstPath);

        // Pick the right command at configure time using IS_DIRECTORY
        // so the same resource entry works for both files and folders.
        out << "if(IS_DIRECTORY " << quotedSrc << ")\n";
        out << "  add_custom_command(TARGET " << targetName
            << " POST_BUILD\n";
        out << "    COMMAND ${CMAKE_COMMAND} -E copy_directory\n";
        out << "      " << quotedSrc << "\n";
        out << "      " << quotedDst << "\n";
        out << "    COMMENT \"Copying resource directory "
            << destName << "\")\n";
        out << "else()\n";
        out << "  add_custom_command(TARGET " << targetName
            << " POST_BUILD\n";
        out << "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n";
        out << "      " << quotedSrc << "\n";
        out << "      " << quotedDst << "\n";
        out << "    COMMENT \"Copying resource " << destName << "\")\n";
        out << "endif()\n";
      }

      out << "\n";
    }
  } // namespace

  // -----------------------------------------------------------------
  // Public API
  // -----------------------------------------------------------------

  bool AppCMakeGenerateResult::success() const
  {
    return error.empty() &&
           !sourceDir.empty() &&
           !cmakeListsPath.empty();
  }

  std::string generate_app_cmake_lists_content(
      const AppManifest &manifest,
      const fs::path &projectDir)
  {
    const std::string targetName = manifest.name;

    std::ostringstream out;

    emit_header(out);
    emit_project(out, targetName);
    emit_standard(out, manifest.standard);

    // Packages must be resolved before the target so their imported
    // targets are available to target_link_libraries.
    emit_packages(out, manifest.packages);

    emit_target(out, manifest, targetName, projectDir);
    emit_includes_and_defines(out, manifest, targetName, projectDir);
    emit_options_and_features(out, manifest, targetName);
    emit_links(out, manifest, targetName);
    emit_output_dir(out, manifest, targetName);
    emit_resources(out, manifest, targetName, projectDir);

    return out.str();
  }

  AppCMakeGenerateResult generate_app_cmake_project(
      const AppManifest &manifest,
      const fs::path &projectDir)
  {
    AppCMakeGenerateResult result;

    if (!manifest.valid())
    {
      result.error = "Invalid vix.app manifest.";
      return result;
    }

    const fs::path normalizedProjectDir =
        fs::absolute(projectDir).lexically_normal();

    // Validate every declared source up-front so users get a clear
    // error pointing at the offending path instead of a CMake-level
    // failure deep in the configure step.
    for (const std::string &source : manifest.sources)
    {
      const fs::path resolved =
          absolute_project_path(normalizedProjectDir, source);

      std::error_code ec;
      if (!fs::exists(resolved, ec) || !fs::is_regular_file(resolved, ec))
      {
        result.error =
            "vix.app source file not found: " + resolved.generic_string();
        return result;
      }
    }

    result.sourceDir =
        normalizedProjectDir / ".vix" / "generated" / "app";

    result.cmakeListsPath = result.sourceDir / "CMakeLists.txt";

    const std::string content =
        generate_app_cmake_lists_content(
            manifest,
            normalizedProjectDir);

    if (!write_text_file_atomic(result.cmakeListsPath, content))
    {
      result.error =
          "Failed to write generated CMakeLists.txt: " +
          result.cmakeListsPath.string();
      return result;
    }

    return result;
  }

} // namespace vix::cli::app
