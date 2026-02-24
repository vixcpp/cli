/**
 *
 *  @file ScriptCMake.cpp
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
#include <vix/cli/commands/run/detail/ScriptCMake.hpp>

#include <fstream>
#include <string>
#include <algorithm>

namespace vix::commands::RunCommand::detail
{
  struct ScriptFeatures
  {
    bool usesVix = false;
    bool usesOrm = false;
    bool usesDb = false;
    bool usesMysql = false;
  };

  static ScriptFeatures detect_script_features(const fs::path &cppPath)
  {
    ScriptFeatures f;

    std::ifstream ifs(cppPath);
    if (!ifs)
      return f;

    std::string line;
    while (std::getline(ifs, line))
    {
      auto has = [&](const char *s)
      { return line.find(s) != std::string::npos; };

      if (has("vix::") || has("Vix::") || has("#include <vix/") || has("#include \"vix/"))
        f.usesVix = true;

      if (has("#include <vix/orm/") || has("vix::orm") || has("using namespace vix::orm"))
        f.usesOrm = true;

      if (has("#include <vix/db/") || has("vix::db") || has("using namespace vix::db"))
        f.usesDb = true;

      if (has("make_mysql_factory") || has("Engine::MySQL") || has("mysqlcppconn"))
        f.usesMysql = true;

      if (!f.usesMysql && (has("tcp://") || has("3306") || has("MySQL")))
        f.usesMysql = true;
    }

    if (f.usesOrm)
      f.usesDb = true;

    return f;
  }

  ScriptLinkFlags parse_link_flags(const std::vector<std::string> &flags)
  {
    ScriptLinkFlags out;

    for (const auto &f : flags)
    {
      if (f.rfind("-l", 0) == 0 && f.size() > 2)
      {
        out.libs.push_back(f.substr(2));
        continue;
      }
      if (f.rfind("-L", 0) == 0 && f.size() > 2)
      {
        out.libDirs.push_back(f.substr(2));
        continue;
      }

      if (f.rfind("-I", 0) == 0 || f == "-I" || f.rfind("-D", 0) == 0 || f == "-D" ||
          f.rfind("-isystem", 0) == 0 || f == "-isystem")
      {
        continue;
      }

      out.linkOpts.push_back(f);
    }

    return out;
  }

  bool script_uses_vix(const fs::path &cppPath)
  {
    std::ifstream ifs(cppPath);
    if (!ifs)
      return false;

    std::string line;
    while (std::getline(ifs, line))
    {
      if (line.find("vix::") != std::string::npos ||
          line.find("Vix::") != std::string::npos)
      {
        return true;
      }
      if (line.find("#include") == std::string::npos)
        continue;
      if (line.find("vix") != std::string::npos ||
          line.find("Vix") != std::string::npos)
      {
        return true;
      }
    }
    return false;
  }

  fs::path get_scripts_root()
  {
    auto cwd = fs::current_path();
    return cwd / ".vix-scripts";
  }

  struct ScriptCompileFlags
  {
    std::vector<std::string> includeDirs; // -I...
    std::vector<std::string> systemDirs;  // -isystem ...
    std::vector<std::string> defines;     // -DKEY=VAL
    std::vector<std::string> compileOpts; // autres options compile
  };

  static inline bool starts_with(const std::string &s, const char *p)
  {
    return s.rfind(p, 0) == 0;
  }

  static ScriptCompileFlags parse_compile_flags(const std::vector<std::string> &flags)
  {
    ScriptCompileFlags out;

    for (std::size_t i = 0; i < flags.size(); ++i)
    {
      const std::string &f = flags[i];

      // -I/path or -I /path
      if (starts_with(f, "-I") && f.size() > 2)
      {
        out.includeDirs.push_back(f.substr(2));
        continue;
      }
      if (f == "-I" && i + 1 < flags.size())
      {
        out.includeDirs.push_back(flags[++i]);
        continue;
      }

      // -isystem/path or -isystem /path
      if (starts_with(f, "-isystem") && f.size() > 8)
      {
        out.systemDirs.push_back(f.substr(8));
        continue;
      }
      if (f == "-isystem" && i + 1 < flags.size())
      {
        out.systemDirs.push_back(flags[++i]);
        continue;
      }

      // -DKEY or -DKEY=VAL or -D KEY=VAL
      if (starts_with(f, "-D") && f.size() > 2)
      {
        out.defines.push_back(f.substr(2));
        continue;
      }
      if (f == "-D" && i + 1 < flags.size())
      {
        out.defines.push_back(flags[++i]);
        continue;
      }

      // sinon, c est une option compile generique
      // exemple: -O2, -g, -std=c++20, -fPIC, -Wno-...
      if (!f.empty() && f[0] == '-')
        out.compileOpts.push_back(f);
    }

    return out;
  }

  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags)
  {
    std::string s;
    s.reserve(6200);

    auto q = [](const std::string &p)
    {
      std::string out = "\"";
      for (char c : p)
      {
        if (c == '\\')
          out += "\\\\";
        else if (c == '"')
          out += "\\\"";
        else
          out += c;
      }
      out += "\"";
      return out;
    };

    s += "cmake_minimum_required(VERSION 3.20)\n";
    s += "project(" + exeName + " LANGUAGES CXX)\n\n";

    s += "set(CMAKE_VERBOSE_MAKEFILE ON)\n";
    s += "set(CMAKE_RULE_MESSAGES OFF)\n\n";

    // Standard
    s += "set(CMAKE_CXX_STANDARD 20)\n";
    s += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    s += "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

    // Default build type
    s += "if (NOT CMAKE_BUILD_TYPE)\n";
    s += "  set(CMAKE_BUILD_TYPE Debug CACHE STRING \"Build type\" FORCE)\n";
    s += "endif()\n\n";

    auto feat = detect_script_features(cppPath);

    // Options
    s += "option(VIX_ENABLE_SANITIZERS \"Enable sanitizers (dev only)\" OFF)\n";
    s += "set(VIX_SANITIZER_MODE \"asan_ubsan\" CACHE STRING \"Sanitizer mode: asan_ubsan or ubsan\")\n";
    s += "set_property(CACHE VIX_SANITIZER_MODE PROPERTY STRINGS asan_ubsan ubsan)\n";
    s += "option(VIX_ENABLE_LIBCXX_ASSERTS \"Enable libstdc++ debug mode (_GLIBCXX_ASSERTIONS/_GLIBCXX_DEBUG)\" OFF)\n";
    s += "option(VIX_ENABLE_HARDENING \"Enable extra hardening flags (non-MSVC)\" OFF)\n";
    s += std::string("option(VIX_USE_ORM \"Enable Vix ORM (requires vix::orm)\" ") + (feat.usesOrm ? "ON" : "OFF") + ")\n";

    // Executable
    s += "add_executable(" + exeName + " " + q(cppPath.string()) + ")\n\n";
    auto lf = parse_link_flags(scriptFlags);

    const auto cf = parse_compile_flags(scriptFlags);

    if (!cf.includeDirs.empty() || !cf.systemDirs.empty())
    {
      s += "target_include_directories(" + exeName + " PRIVATE\n";
      for (const auto &d : cf.includeDirs)
        s += "  " + q(d) + "\n";
      s += ")\n\n";

      if (!cf.systemDirs.empty())
      {
        s += "target_include_directories(" + exeName + " SYSTEM PRIVATE\n";
        for (const auto &d : cf.systemDirs)
          s += "  " + q(d) + "\n";
        s += ")\n\n";
      }
    }

    if (!cf.defines.empty())
    {
      s += "target_compile_definitions(" + exeName + " PRIVATE\n";
      for (const auto &d : cf.defines)
        s += "  " + d + "\n";
      s += ")\n\n";
    }

    if (!cf.compileOpts.empty())
    {
      s += "target_compile_options(" + exeName + " PRIVATE\n";
      for (const auto &o : cf.compileOpts)
        s += "  " + o + "\n";
      s += ")\n\n";
    }

    auto hasLib = [&](const std::string &name) -> bool
    {
      return std::find(lf.libs.begin(), lf.libs.end(), name) != lf.libs.end();
    };

    if (!lf.libDirs.empty())
    {
      s += "target_link_directories(" + exeName + " PRIVATE\n";
      for (const auto &d : lf.libDirs)
        s += "  " + q(d) + "\n";
      s += ")\n\n";
    }

    if (!lf.libs.empty())
    {
      s += "target_link_libraries(" + exeName + " PRIVATE\n";
      for (const auto &L : lf.libs)
        s += "  " + L + "\n"; // ssl crypto
      s += ")\n\n";
    }

    if (!lf.linkOpts.empty())
    {
      s += "target_link_options(" + exeName + " PRIVATE\n";
      for (const auto &o : lf.linkOpts)
        s += "  " + o + "\n"; // ex: -Wl,--as-needed
      s += ")\n\n";
    }

    // Warnings
    s += "if (MSVC)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE /W4 /permissive- /EHsc)\n";
    s += "  target_compile_definitions(" + exeName + " PRIVATE _CRT_SECURE_NO_WARNINGS)\n";
    s += "else()\n";
    s += "  target_compile_options(" + exeName + " PRIVATE\n";
    s += "    -Wall -Wextra -Wpedantic\n";
    s += "    -Wshadow -Wconversion -Wsign-conversion\n";
    s += "    -Wformat=2 -Wnull-dereference\n";
    s += "  )\n";
    s += "endif()\n\n";

    // Better backtraces
    s += "if (NOT MSVC)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE -fno-omit-frame-pointer)\n";
    s += "  if (UNIX AND NOT APPLE)\n";
    s += "    target_link_options(" + exeName + " PRIVATE -rdynamic)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    // libstdc++ assertions
    s += "if (VIX_ENABLE_LIBCXX_ASSERTS AND NOT MSVC)\n";
    s += "  target_compile_definitions(" + exeName + " PRIVATE _GLIBCXX_ASSERTIONS)\n";
    s += "endif()\n\n";

    // Hardening
    s += "if (VIX_ENABLE_HARDENING AND NOT MSVC)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE -D_FORTIFY_SOURCE=2)\n";
    s += "  target_link_options(" + exeName + " PRIVATE -Wl,-z,relro -Wl,-z,now)\n";
    s += "endif()\n\n";

    // Link libs if standalone
    if (!useVixRuntime)
    {
      s += "if (UNIX AND NOT APPLE)\n";
      s += "  target_link_libraries(" + exeName + " PRIVATE pthread dl)\n";
      s += "endif()\n\n";
    }
    else
    {
      s += "# Prefer lowercase package, fallback to legacy Vix\n";
      s += "find_package(vix QUIET CONFIG)\n";
      s += "if (NOT vix_FOUND)\n";
      s += "  find_package(Vix CONFIG REQUIRED)\n";
      s += "endif()\n\n";

      s += "# Pick main target (umbrella preferred)\n";
      s += "set(VIX_MAIN_TARGET \"\")\n";
      s += "if (TARGET vix::vix)\n";
      s += "  set(VIX_MAIN_TARGET vix::vix)\n";
      s += "elseif (TARGET Vix::vix)\n";
      s += "  set(VIX_MAIN_TARGET Vix::vix)\n";
      s += "elseif (TARGET vix::core)\n";
      s += "  set(VIX_MAIN_TARGET vix::core)\n";
      s += "elseif (TARGET Vix::core)\n";
      s += "  set(VIX_MAIN_TARGET Vix::core)\n";
      s += "else()\n";
      s += "  message(FATAL_ERROR \"No Vix target found (vix::vix/Vix::vix/vix::core/Vix::core)\")\n";
      s += "endif()\n\n";

      s += "target_link_libraries(" + exeName + " PRIVATE ${VIX_MAIN_TARGET})\n\n";

      s += "# Optional ORM (and DB)\n";
      s += "if (VIX_USE_ORM)\n";
      s += "  if (TARGET vix::orm)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE vix::orm)\n";
      s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_USE_ORM=1)\n";
      s += "  elseif (TARGET Vix::orm)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE Vix::orm)\n";
      s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_USE_ORM=1)\n";
      s += "  else()\n";
      s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but ORM target is not available (vix::orm/Vix::orm)\")\n";
      s += "  endif()\n";
      s += "\n";
      s += "  if (TARGET vix::db)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE vix::db)\n";
      s += "  elseif (TARGET Vix::db)\n";
      s += "    target_link_libraries(" + exeName + " PRIVATE Vix::db)\n";
      s += "  else()\n";
      s += "    message(FATAL_ERROR \"VIX_USE_ORM=ON but DB target is not available (vix::db/Vix::db)\")\n";
      s += "  endif()\n";
      s += "endif()\n\n";

      // Auto-link MySQL connector only when needed and not already provided
      if (feat.usesMysql && !hasLib("mysqlcppconn8") && !hasLib("mysqlcppconn"))
      {
        s += "# Auto-link MySQL connector when script uses MySQL\n";
        s += "if (UNIX)\n";
        s += "  find_library(VIX_MYSQLCPPCONN_LIB NAMES mysqlcppconn8 mysqlcppconn)\n";
        s += "  if (VIX_MYSQLCPPCONN_LIB)\n";
        s += "    target_link_libraries(" + exeName + " PRIVATE ${VIX_MYSQLCPPCONN_LIB})\n";
        s += "  else()\n";
        s += "    message(WARNING \"MySQL connector lib not found (mysqlcppconn8/mysqlcppconn). If you get undefined references, pass: -- -lmysqlcppconn8\")\n";
        s += "  endif()\n";
        s += "endif()\n\n";
      }
    }

    // Sanitizers (mode-aware)
    s += "if (VIX_ENABLE_SANITIZERS AND NOT MSVC)\n";
    s += "  if (VIX_SANITIZER_MODE STREQUAL \"ubsan\")\n";
    s += "    message(STATUS \"Sanitizers: UBSan enabled\")\n";
    s += "    target_compile_options(" + exeName + " PRIVATE\n";
    s += "      -O0 -g3\n";
    s += "      -fno-omit-frame-pointer\n";
    s += "      -fsanitize=undefined\n";
    s += "      -fno-sanitize-recover=all\n";
    s += "    )\n";
    s += "    target_link_options(" + exeName + " PRIVATE -fsanitize=undefined)\n";
    s += "  else()\n";
    s += "    message(STATUS \"Sanitizers: ASan+UBSan enabled\")\n";
    s += "    target_compile_options(" + exeName + " PRIVATE\n";
    s += "      -O1 -g3\n";
    s += "      -fno-omit-frame-pointer\n";
    s += "      -fsanitize=address,undefined\n";
    s += "      -fno-sanitize-recover=all\n";
    s += "    )\n";
    s += "    target_link_options(" + exeName + " PRIVATE -fsanitize=address,undefined)\n";
    s += "    target_compile_definitions(" + exeName + " PRIVATE VIX_ASAN_QUIET=1)\n";
    s += "  endif()\n";
    s += "endif()\n\n";

    // Always keep some debug info on Linux
    s += "if (UNIX AND NOT APPLE)\n";
    s += "  target_compile_options(" + exeName + " PRIVATE -g)\n";
    s += "endif()\n";

    return s;
  }
}
