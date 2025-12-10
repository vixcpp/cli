#pragma once

#include <string>
#include <vector>

namespace vix::commands::DevCommand
{
    /// Entry point for `vix dev [args...]`.
    ///
    /// High-level behaviour (v1):
    ///   - Forwards all arguments to `vix run`, and ensures that a
    ///     `--watch` / reload-friendly mode is enabled.
    ///   - This makes `vix dev` a convenient alias for:
    ///         vix run <args...> --watch
    ///
    /// Future versions may implement a richer "dev server" experience:
    ///   - file watching on source directories
    ///   - smart rebuild / restart cycles
    ///   - friendly URL + server banner
    int run(const std::vector<std::string> &args);

    /// Help text for `vix dev`.
    ///
    /// Printed when the user calls:
    ///   vix dev --help
    /// or:
    ///   vix help dev
    int help();
} // namespace vix::commands::DevCommand
