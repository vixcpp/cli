#ifndef ORM_COMMAND_PHP
#define ORM_COMMAND_HPP

#include <string>
#include <vector>

namespace vix::commands::OrmCommand
{
    int run(const std::vector<std::string> &args);
    int help();
}

#endif
