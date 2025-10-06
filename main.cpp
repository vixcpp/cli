#include "vix/cli/CLI.hpp"

int main(int argc, char **argv)
{
    Vix::CLI cli;
    return cli.run(argc, argv);
}