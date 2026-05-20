#pragma once

/**
 * @file NewTypes.hpp
 * @author Gaspard Kirira
 *
 * Copyright 2025, Gaspard Kirira.  All rights reserved.
 * https://github.com/vixcpp/vix
 * Use of this source code is governed by a MIT license
 * that can be found in the License file.
 *
 * Shared types for the `vix new` command.
 */

namespace vix::commands::new_cmd
{
  enum class TemplateKind
  {
    App,
    Lib,
    Vue
  };

  struct FeaturesSelection
  {
    bool orm{false};         ///< VIX_USE_ORM=ON
    bool sanitizers{false};  ///< VIX_ENABLE_SANITIZERS=ON
    bool static_rt{false};   ///< VIX_LINK_STATIC=ON
    bool full_static{false}; ///< VIX_LINK_FULL_STATIC=ON
  };

  enum class OverwriteChoice
  {
    Overwrite,
    Cancel
  };

  enum class InPlaceDirChoice
  {
    Proceed,
    Cancel
  };

} // namespace vix::commands::new_cmd
