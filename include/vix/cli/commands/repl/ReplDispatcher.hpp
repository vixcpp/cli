/**
 *
 *  @file ReplDispatcher.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RELP_DISPATCHER_HPP
#define VIX_RELP_DISPATCHER_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace vix::cli::repl
{
  using DispatchFn = std::function<int(const std::vector<std::string> &)>;

  class Dispatcher
  {
  public:
    Dispatcher();

    bool has(const std::string &cmd) const;
    int dispatch(const std::string &cmd, const std::vector<std::string> &args) const;

  private:
    std::unordered_map<std::string, DispatchFn> map_;
  };
}

#endif
