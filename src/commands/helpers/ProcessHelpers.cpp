/**
 *
 *  @file ProcessHelpers.cpp
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
#include <vix/cli/commands/helpers/ProcessHelpers.hpp>

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace vix::cli::commands::helpers
{
  std::string quote(const std::string &s)
  {
#if defined(_WIN32)
    std::string out = "\"";
    out.reserve(s.size() + 2);
    for (char c : s)
    {
      if (c == '"')
        out += "\"\"";
      else
        out += c;
    }
    out += "\"";
    return out;
#else
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s)
    {
      if (c == '\'')
        out += "'\\''";
      else
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
#endif
  }

  bool has_cmake_cache(const std::filesystem::path &buildDir)
  {
    std::error_code ec;
    return std::filesystem::exists(buildDir / "CMakeCache.txt", ec);
  }

  std::string run_and_capture_with_code(const std::string &cmd, int &outCode)
  {
    outCode = 0;

#if defined(_WIN32)
    std::string full = cmd + " 2>&1";
    FILE *pipe = _popen(full.c_str(), "r");
#else
    std::string full = cmd + " 2>&1";
    FILE *pipe = ::popen(full.c_str(), "r");
#endif
    if (!pipe)
    {
      outCode = 127;
      return {};
    }

    std::ostringstream oss;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
      oss << buffer;

#if defined(_WIN32)
    int rc = _pclose(pipe);
    outCode = rc;
#else
    int rc = ::pclose(pipe);
    outCode = rc;
#endif
    return oss.str();
  }

} // namespace vix::cli::commands::helpers
