#pragma once
#include "vix/cli/commands/run/RunDetail.hpp"
#include "vix/cli/manifest/VixManifest.hpp"

namespace vix::cli::manifest
{
    using Options = vix::commands::RunCommand::detail::Options;
    Options merge_options(const Manifest &mf, const Options &cli);
}
