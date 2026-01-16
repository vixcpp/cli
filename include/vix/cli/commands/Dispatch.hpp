/**
 *
 *  @file Dispatch.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_DISPATCH_HPP
#define VIX_DISPATCH_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <optional>

namespace vix::cli::dispatch
{
  using Args = std::vector<std::string>;
  using RunFn = std::function<int(const Args &)>;
  using HelpFn = std::function<int()>;

  struct Entry
  {
    std::string name;
    std::string category; // "Project", "Packaging", "Info", ...
    std::string summary;  // one-liner shown in REPL help list

    RunFn run;
    HelpFn help;
  };

  class Dispatcher
  {
  public:
    Dispatcher();
    bool has(const std::string &cmd) const;
    int run(const std::string &cmd, const Args &args) const;
    int help(const std::string &cmd) const;
    const std::unordered_map<std::string, Entry> &entries() const noexcept;

  private:
    std::unordered_map<std::string, Entry> map_;
  };

  Dispatcher &global();
}

#endif
