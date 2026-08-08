/**
 *
 *  @file BuildOutputParser.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#pragma once

#include <string_view>
#include <vector>

#include <vix/cli/build/BuildEvent.hpp>

namespace vix::cli::build
{
  class BuildOutputParser
  {
  public:
    BuildOutputParser() = default;

    /**
     * Consume one line produced by a build tool.
     *
     * One input line may generate several semantic events.
     *
     * Example:
     *
     *   [12/40] Building CXX object ...
     *
     * may produce:
     *
     *   CompileStarted
     *   CompileProgress
     */
    std::vector<BuildEvent> consume(
        std::string_view line);

    /**
     * Reset parser state before starting another build.
     */
    void reset() noexcept;

  private:
    bool configureFinished_{false};

    bool compileStarted_{false};
    bool compileFinished_{false};

    bool linkStarted_{false};
  };

} // namespace vix::cli::build
