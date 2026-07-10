#ifndef VIX_CLOUD_COMMAND_HPP
#define VIX_CLOUD_COMMAND_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace vix::commands
{
  struct CloudBuildReport
  {
    std::string status;
    std::string target;
    std::string profile;
    std::string toolchain;
    std::string summary_message;
    std::int64_t duration_ms{0};
    int warnings_count{0};
    int errors_count{0};
  };

  struct CloudCommand
  {
    static int run(const std::vector<std::string> &args);
    static int login(const std::vector<std::string> &args);
    static int logout(const std::vector<std::string> &args);
    static int status(const std::vector<std::string> &args);
    static int init(const std::vector<std::string> &args);
    static int sync(const std::vector<std::string> &args);
    static int upload_lockfile(const std::vector<std::string> &args);
    static bool submit_build_report(const CloudBuildReport &report, std::string &message);
    static int doctor(const std::vector<std::string> &args);
    static int help();
  };
}

#endif
