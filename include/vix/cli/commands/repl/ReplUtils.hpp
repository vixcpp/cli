#ifndef VIX_RELP_UTILS_HPP
#define VIX_RELP_UTILS_HPP

#include <string>
#include <vector>
#include <filesystem>

namespace vix::cli::repl
{
  // Prompt: vix(<dir>)>
  std::string make_prompt(const std::filesystem::path &cwd);

  // Shell-like split with quotes:
  //   run -- --port 8080
  //   new "my app"
  // Supports "..." and '...'.
  std::vector<std::string> split_command_line(const std::string &line);

  // Clear screen (cross-platform)
  void clear_screen();

  // Resolve home dir (for history file)
  std::filesystem::path user_home_dir();

  // Trim helpers
  std::string trim_copy(std::string s);
  bool starts_with(const std::string &s, const std::string &prefix);
}

#endif
