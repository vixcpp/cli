#ifndef VIX_CLI_SCRIPT_CMAKE_HPP
#define VIX_CLI_SCRIPT_CMAKE_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace vix::commands::RunCommand::detail
{
  namespace fs = std::filesystem;

  struct ScriptLinkFlags
  {
    std::vector<std::string> libs;
    std::vector<std::string> libDirs;
    std::vector<std::string> linkOpts;
  };

  ScriptLinkFlags parse_link_flags(const std::vector<std::string> &flags);

  bool script_uses_vix(const fs::path &cppPath);

  fs::path get_scripts_root();

  std::string make_script_cmakelists(
      const std::string &exeName,
      const fs::path &cppPath,
      bool useVixRuntime,
      const std::vector<std::string> &scriptFlags);
}

#endif
