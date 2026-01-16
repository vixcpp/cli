/**
 *
 *  @file OwnershipRules.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_OWNERSHIP_RULES_HPP
#define VIX_OWNERSHIP_RULES_HPP

#include <memory>

namespace vix::cli::errors
{
  class IErrorRule;

  std::unique_ptr<IErrorRule> makeUniquePtrCopyRule();
  std::unique_ptr<IErrorRule> makeSharedPtrRawPtrMisuseRule();
  std::unique_ptr<IErrorRule> makeDeleteMismatchRule(); // delete vs delete[]
  std::unique_ptr<IErrorRule> makeDanglingStringViewRule();
  std::unique_ptr<IErrorRule> makeReturnLocalRefRule();
  std::unique_ptr<IErrorRule> makeUseOfUninitializedRule();
} // namespace vix::cli::errors

#endif
