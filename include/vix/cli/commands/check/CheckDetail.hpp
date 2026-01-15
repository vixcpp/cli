#ifndef VIX_CHECK_DETAIL_HPP
#define VIX_CHECK_DETAIL_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::CheckCommand::detail
{
  namespace fs = std::filesystem;

  struct Options
  {
    // common
    std::string dir;
    std::string preset = "dev-ninja";
    std::string buildPreset; // optional (override)
    int jobs = 0;
    bool quiet = false;
    bool verbose = false;
    std::string logLevel;

    // script mode
    bool singleCpp = false;
    fs::path cppFile;
    bool enableSanitizers = false; // --san
    bool enableUbsanOnly = false;  // --ubsan

    // project checks
    bool tests = false;                 // --tests
    std::string ctestPreset;            // --ctest-preset
    std::vector<std::string> ctestArgs; // repeatable: --ctest-arg <...>

    // runtime check (project mode + optional)
    bool runAfterBuild = false; // --run
    int runTimeoutSec = 0;      // --run-timeout <sec> (0 = no timeout)
  };

  Options parse(const std::vector<std::string> &args);

  // script mode
  int check_single_cpp(const Options &opt);
  // project mode
  int check_project(const Options &opt, const fs::path &projectDir);

} // namespace vix::commands::CheckCommand::detail

#endif
