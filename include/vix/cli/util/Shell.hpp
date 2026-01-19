#ifndef VIX_CLI_UTIL_SHELL_HPP
#define VIX_CLI_UTIL_SHELL_HPP

#include <cstdlib>
#include <string>

namespace vix::cli::util
{
  inline bool debug_enabled()
  {
    const char *v = std::getenv("VIX_DEBUG");
    if (!v)
      return false;

    const std::string s(v);
    return (s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "YES");
  }

  inline int run_cmd(const std::string &cmd, bool quiet)
  {
#ifdef _WIN32
    (void)quiet;
    const int rc = std::system(cmd.c_str());
    return rc == 0 ? 0 : 1;
#else
    std::string full = cmd;

    if (quiet && !debug_enabled())
      full += " >/dev/null 2>&1";

    const int rc = std::system(full.c_str());
    return rc == 0 ? 0 : 1;
#endif
  }

  inline int run_cmd_retry_debug(const std::string &cmd)
  {
    if (run_cmd(cmd, true) == 0)
      return 0;

    if (debug_enabled())
      return 1;

    return run_cmd(cmd, false);
  }
} // namespace vix::cli::utils

#endif // VIX_CLI_UTIL_SHELL_HPP
