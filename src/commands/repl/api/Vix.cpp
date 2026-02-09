/**
 *
 *  @file Vix.cpp
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
#include <vix/cli/commands/repl/api/Vix.hpp>
#include <vix/cli/commands/repl/ReplHistory.hpp>
#include <vix/utils/Env.hpp>

#include <cstdlib>
#include <system_error>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

namespace vix::cli::repl::api
{
  Vix::Vix(vix::cli::repl::History *hist)
      : history_(hist)
  {
  }

  int Vix::pid() const
  {
#ifndef _WIN32
    return (int)::getpid();
#else
    return (int)_getpid();
#endif
  }

  void Vix::exit(int code)
  {
    exitRequested_ = true;
    exitCode_ = code;
  }

  bool Vix::exit_requested() const { return exitRequested_; }
  int Vix::exit_code() const { return exitCode_; }

  std::filesystem::path Vix::cwd() const
  {
    return std::filesystem::current_path();
  }

  VixResult Vix::cd(const std::string &path)
  {
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    if (ec)
      return {1, "cd: " + ec.message()};
    return {0, ""};
  }

  VixResult Vix::mkdir(const std::string &path, bool recursive)
  {
    std::error_code ec;
    bool ok = false;
    if (recursive)
      ok = std::filesystem::create_directories(path, ec);
    else
      ok = std::filesystem::create_directory(path, ec);

    if (ec)
      return {1, "mkdir: " + ec.message()};

    (void)ok;
    return {0, ""};
  }

  std::optional<std::string> Vix::env(const std::string &key) const
  {
    const char *v = vix::utils::vix_getenv(key.c_str());
    if (!v || !*v)
      return std::nullopt;
    return std::string(v);
  }

  const std::vector<std::string> &Vix::args() const { return args_; }
  void Vix::set_args(std::vector<std::string> a) { args_ = std::move(a); }

  VixResult Vix::history()
  {
    if (!history_)
      return {1, "history not available"};
    return {0, ""};
  }

  VixResult Vix::history_clear()
  {
    if (!history_)
      return {1, "history not available"};
    history_->clear();
    return {0, ""};
  }
}
