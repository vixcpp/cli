#ifndef VIX_CLOUD_COMMAND_HPP
#define VIX_CLOUD_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands
{
  struct CloudCommand
  {
    static int run(const std::vector<std::string> &args);
    static int login(const std::vector<std::string> &args);
    static int logout(const std::vector<std::string> &args);
    static int status(const std::vector<std::string> &args);
    static int init(const std::vector<std::string> &args);
    static int sync(const std::vector<std::string> &args);
    static int doctor(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
