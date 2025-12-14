#pragma once
#include <string>
#include <vector>

namespace vix::commands
{
    class OrmCommand
    {
    public:
        static int run(const std::vector<std::string> &args);
        static int help();
    };
}
