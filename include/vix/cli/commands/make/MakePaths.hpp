/**
 *
 *  @file MakePaths.hpp
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
#ifndef VIX_MAKE_PATHS_HPP
#define VIX_MAKE_PATHS_HPP

#include <filesystem>
#include <string>

namespace vix::cli::make
{
  struct MakeLayout
  {
    std::filesystem::path root;
    std::filesystem::path base;
    std::filesystem::path include_dir;
    std::filesystem::path src_dir;
    std::filesystem::path tests_dir;

    std::string project;
    std::string default_namespace;

    bool in_module = false;
    std::string module_name;
  };

  [[nodiscard]] std::filesystem::path resolve_root(const std::string &dir_opt);

  [[nodiscard]] std::string detect_project_name_from_cmake(
      const std::filesystem::path &root);

  [[nodiscard]] std::string guess_default_namespace(const std::string &project);

  [[nodiscard]] MakeLayout resolve_layout(
      const std::filesystem::path &root,
      const std::string &in_path);
}

#endif
