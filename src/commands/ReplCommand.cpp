#include <vix/cli/commands/ReplCommand.hpp>
#include <vix/cli/commands/repl/ReplDetail.hpp>
#include <vix/cli/commands/repl/ReplFlow.hpp>
#include <iostream>

namespace vix::commands::ReplCommand
{
    int run(const std::vector<std::string> &args)
    {
        (void)args;
        return repl_flow_run();
    }

    int help()
    {
        // Flow owns display; but keep help here for CLI consistency
        // You can also forward to flow help if you want.
        std::cout
            << "Usage:\n"
            << "  vix repl\n\n"
            << "Description:\n"
            << "  Start an interactive Vix REPL (CLI shell + calculator).\n\n"
            << "Examples:\n"
            << "  vix repl\n"
            << "  vix repl   # then: = 1+2*3\n";
        return 0;
    }
}
