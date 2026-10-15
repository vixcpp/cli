#ifndef VIX_DEV_COMMAND_HPP
#define VIX_DEV_COMMAND_HPP

#include <string>
#include <utility>
#include <vector>

namespace vix::commands::DevCommand
{
  namespace detail
  {
    inline std::vector<std::string> watched_run_arguments(
        const std::vector<std::string> &arguments)
    {
      std::vector<std::string> forwarded = arguments;
      forwarded.insert(forwarded.begin(), "--watch");
      return forwarded;
    }

    template <typename Run>
    int dispatch_watched_run(const std::vector<std::string> &arguments, Run &&run)
    {
      const auto forwarded = watched_run_arguments(arguments);
      return std::forward<Run>(run)(forwarded);
    }
  }

  int run(const std::vector<std::string> &arguments);
  int help();
}

#endif
