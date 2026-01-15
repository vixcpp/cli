/**
 *
 *  @file RelpHistory.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */
#include <vix/cli/commands/repl/ReplHistory.hpp>

#include <fstream>
#include <iterator>

namespace vix::cli::repl
{
  History::History(std::size_t maxItems)
      : max_(maxItems)
  {
  }

  void History::add(const std::string &line)
  {
    if (line.empty())
      return;

    // avoid duplicates in a row
    if (!items_.empty() && items_.back() == line)
      return;

    items_.push_back(line);

    if (items_.size() > max_)
    {
      const std::size_t overflow = items_.size() - max_;
      items_.erase(
          items_.begin(),
          std::next(
              items_.begin(),
              static_cast<std::vector<std::string>::difference_type>(overflow)));
    }
  }

  void History::clear()
  {
    items_.clear();
  }

  const std::vector<std::string> &History::items() const noexcept
  {
    return items_;
  }

  bool History::loadFromFile(const std::filesystem::path &file, std::string *err)
  {
    (void)err;
    std::ifstream in(file);
    if (!in.is_open())
    {
      // not fatal if file doesn't exist
      return true;
    }

    std::string line;
    while (std::getline(in, line))
    {
      if (!line.empty())
        add(line);
    }

    return true;
  }

  bool History::saveToFile(const std::filesystem::path &file, std::string *err) const
  {
    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open())
    {
      if (err)
        *err = "cannot write history file: " + file.string();
      return false;
    }

    for (const auto &l : items_)
      out << l << "\n";

    return true;
  }
}
