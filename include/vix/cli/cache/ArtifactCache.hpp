/**
 *
 *  @file ArtifactCache.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 *  Compatibility adapter for the engine artifact cache
 *
 */

#ifndef VIX_CLI_ARTIFACT_CACHE_HPP
#define VIX_CLI_ARTIFACT_CACHE_HPP

#include <vix/engine/ArtifactCache.hpp>

namespace vix::cli::cache
{
  using vix::engine::Artifact;
  using vix::engine::ArtifactCache;
  using vix::engine::ArtifactCachePaths;
  using vix::engine::ArtifactIndexEntry;
  using vix::engine::BuildState;
  using vix::engine::ProjectInput;

} // namespace vix::cli::cache

#endif
