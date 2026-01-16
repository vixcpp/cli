/**
 *
 *  @file ReplHistory.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#ifndef VIX_RELP_HISTORY_HPP
#define VIX_RELP_HISTORY_HPP

#include <string>
#include <vector>
#include <filesystem>
#include <cstddef>

namespace vix::cli::repl
{
  class History
  {
  public:
    explicit History(std::size_t maxItems);

    void add(const std::string &line);
    void clear();

    const std::vector<std::string> &items() const noexcept;

    bool loadFromFile(const std::filesystem::path &file, std::string *err = nullptr);
    bool saveToFile(const std::filesystem::path &file, std::string *err = nullptr) const;

  private:
    std::size_t max_;
    std::vector<std::string> items_;
  };
}

#endif
