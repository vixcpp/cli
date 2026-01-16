/**
 *
 *  @file RunManifestMerge.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RUN_MANIFEST_MERGE_HPP
#define VIX_RUN_MANIFEST_MERGE_HPP

#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/manifest/VixManifest.hpp>

namespace vix::cli::manifest
{
  using Options = vix::commands::RunCommand::detail::Options;
  Options merge_options(const Manifest &mf, const Options &cli);
}

#endif
