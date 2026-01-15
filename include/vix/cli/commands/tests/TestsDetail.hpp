#ifndef VIX_TESTS_DETAIL_HPP
#define VIX_TESTS_DETAIL_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::TestsCommand::detail
{
  namespace fs = std::filesystem;

  struct Options
  {
    bool watch = false;
    bool list = false;
    bool failFast = false;
    bool runAfter = false; // --run (tests + runtime)

    fs::path projectDir;                // resolved path (or cwd)
    std::vector<std::string> forwarded; // args forwarded to `vix check`
  };

  Options parse(const std::vector<std::string> &args);
}

#endif
