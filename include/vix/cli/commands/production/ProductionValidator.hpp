/**
 *
 *  @file ProductionValidator.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_PRODUCTION_VALIDATOR_HPP
#define VIX_PRODUCTION_VALIDATOR_HPP

namespace vix::commands::production::validator
{
  /**
   * @brief Validate the production configuration from vix.json.
   *
   * This validates the production configuration by using the real production
   * modules already available in Vix:
   *
   * - vix deploy --dry-run
   * - vix proxy nginx check
   * - vix health
   *
   * @return Process exit code.
   */
  int validate();

  /**
   * @brief Show the current production status.
   *
   * This checks the deployed production state by using:
   *
   * - vix service status
   * - vix health
   * - vix proxy nginx check
   * - vix logs errors --repeated -n 80
   *
   * @return Process exit code.
   */
  int status();
}

#endif
