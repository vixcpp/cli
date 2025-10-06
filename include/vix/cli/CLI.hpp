#ifndef VIX_CLI_HPP
#define VIX_CLI_HPP

#include <string>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <vector>

namespace Vix
{
    class CLI
    {
    public:
        CLI();
        int run(int argc, char **argv);

    private:
        using CommandHandler = std::function<int(const std::vector<std::string> &)>;
        std::unordered_map<std::string, CommandHandler> commands_;

        int help(const std::vector<std::string> &args);
        int version(const std::vector<std::string> &args);
    };
}

#endif