#include <vix/cli/CLI.hpp>
#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        vix::CLI cli;
        return cli.run(argc, argv);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "[FATAL] Uncaught exception: " << ex.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "[FATAL] Unknown error occurred." << std::endl;
        return 1;
    }
}
