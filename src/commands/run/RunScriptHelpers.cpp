/**
 *
 *  @file RunScriptHelpers.cpp
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
#include <vix/cli/commands/run/RunScriptHelpers.hpp>
#include <vix/cli/commands/run/RunDetail.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>
#include <optional>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace vix::cli::style;

namespace vix::commands::RunCommand::detail
{
  namespace
  {
    struct WatchSpinner
    {
      std::atomic<bool> running{false};
      std::thread worker;

      void start(std::string label)
      {
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
          return;

        worker = std::thread(
            [this, label = std::move(label)]()
            {
            static const char *frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
            constexpr std::size_t frameCount = sizeof(frames) / sizeof(frames[0]);

            std::size_t i = 0;
            while (running.load(std::memory_order_relaxed))
            {
                std::cout << "\r┃   " << frames[i] << " " << label << "   " << std::flush;
                i = (i + 1) % frameCount;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            } });
      }

      void stop()
      {
        bool expected = true;
        if (!running.compare_exchange_strong(expected, false))
          return;

        if (worker.joinable())
          worker.join();

        std::cout << "\r" << std::string(120, ' ') << "\r" << std::flush;
      }

      ~WatchSpinner()
      {
        stop();
      }
    };

    WatchSpinner g_watch_spinner;
  }

  void watch_spinner_start(std::string label)
  {
    g_watch_spinner.start(std::move(label));
  }

  void watch_spinner_stop()
  {
    g_watch_spinner.stop();
  }

  void watch_spinner_pause_for_output()
  {
    watch_spinner_stop();
  }

  namespace
  {
    struct WatchSpinnerScope
    {
      explicit WatchSpinnerScope(std::string label)
      {
        watch_spinner_start(std::move(label));
      }
      ~WatchSpinnerScope()
      {
        watch_spinner_stop();
      }
    };
  }

  void print_watch_restart_banner(const fs::path &path, std::string_view label)
  {
#ifdef _WIN32
    std::system("cls");
#else
    std::cout << "\x1b[2J\x1b[H" << std::flush;
#endif

    info(std::string("Watcher Restarting! File change detected: \"") + path.string() + "\"");

    std::cout << "\n"
              << std::flush;

    if (label.empty())
      label = "Rebuilding & restarting...";

    watch_spinner_start(std::string(label));
  }

  std::string sanitizer_mode_string(bool /*enableSanitizers*/, bool enableUbsanOnly)
  {
    return enableUbsanOnly ? "ubsan" : "asan_ubsan";
  }

  bool want_sanitizers(bool enableSanitizers, bool enableUbsanOnly)
  {
    return enableSanitizers || enableUbsanOnly;
  }

  std::string make_script_config_signature(
      bool useVixRuntime,
      bool enableSanitizers,
      bool enableUbsanOnly,
      const std::vector<std::string> &scriptFlags,
      bool withSqlite,
      bool withMySql)
  {
    std::string sig;
    sig.reserve(160);

    sig += "useVix=";
    sig += useVixRuntime ? "1" : "0";

    sig += ";san=";
    sig += want_sanitizers(enableSanitizers, enableUbsanOnly) ? "1" : "0";

    sig += ";mode=";
    sig += sanitizer_mode_string(enableSanitizers, enableUbsanOnly);

    sig += ";sqlite=";
    sig += withSqlite ? "1" : "0";

    sig += ";mysql=";
    sig += withMySql ? "1" : "0";

    sig += ";flags=";
    for (const auto &f : scriptFlags)
    {
      sig += f;
      sig += ",";
    }

    return sig;
  }
  std::string join_quoted_args_local(const std::vector<std::string> &a)
  {
    std::string s;
    for (const auto &x : a)
    {
      if (x.empty())
        continue;
      s += " ";
      s += quote(x);
    }
    return s;
  }

  std::string wrap_with_cwd_if_needed(const Options &opt, const std::string &cmd)
  {
    if (opt.cwd.empty())
      return cmd;

    const std::string cwd = normalize_cwd_if_needed(opt.cwd);

#ifdef _WIN32
    return "cmd /C \"cd /D " + quote(cwd) + " && " + cmd + "\"";
#else
    return "cd " + quote(cwd) + " && " + cmd;
#endif
  }

#ifndef _WIN32
  void apply_sanitizer_env_if_needed(bool enableSanitizers, bool enableUbsanOnly)
  {
    if (!want_sanitizers(enableSanitizers, enableUbsanOnly))
      return;

    ::setenv("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1:color=never", 1);

    if (!enableUbsanOnly)
    {
      ::setenv(
          "ASAN_OPTIONS",
          "abort_on_error=1:"
          "detect_leaks=1:"
          "symbolize=1:"
          "allocator_may_return_null=1:"
          "fast_unwind_on_malloc=0:"
          "strict_init_order=1:"
          "check_initialization_order=1:"
          "color=never:"
          "quiet=1",
          1);
    }
  }
#endif

  std::optional<fs::path> find_vix_include_dir()
  {
    const char *home = vix::utils::vix_getenv(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
    );

    std::error_code ec;

    if (home && *home)
    {
      const fs::path inc = fs::path(home) / ".vix" / "include";
      if (fs::exists(inc / "vix.hpp", ec) && !ec)
        return inc;
    }

    for (const char *prefix : {"/usr/local/include", "/usr/include"})
    {
      const fs::path inc = fs::path(prefix);
      ec.clear();
      if (fs::exists(inc / "vix.hpp", ec) && !ec)
        return inc;
    }

    return std::nullopt;
  }

  std::optional<fs::path> find_vix_lib()
  {
    std::error_code ec;

    for (const char *prefix : {"/usr/local/lib", "/usr/lib"})
    {
      for (const char *name : {"libvix.a", "libvix.so", "libvix.dylib"})
      {
        const fs::path p = fs::path(prefix) / name;
        ec.clear();
        if (fs::exists(p, ec) && !ec)
          return p;
      }
    }

    return std::nullopt;
  }

  std::vector<fs::path> find_vix_all_module_libs()
  {
    std::vector<fs::path> result;
    std::error_code ec;

    static const char *modules[] = {
        "libvix_core.a",
        "libvix_net.a",
        "libvix_websocket.a",
        "libvix_middleware.a",
        "libvix_p2p.a",
        "libvix_p2p_http.a",
        "libvix_async.a",
        "libvix_sync.a",
        "libvix_cache.a",
        "libvix_orm.a",
        "libvix_db.a",
        "libvix_crypto.a",
        "libvix_process.a",
        "libvix_io.a",
        "libvix_fs.a",
        "libvix_os.a",
        "libvix_env.a",
        "libvix_log.a",
        "libvix_utils.a",
        "libvix_path.a",
        "libvix_error.a",
        nullptr};

    for (const char *prefix : {"/usr/local/lib", "/usr/lib"})
    {
      bool found = false;
      std::vector<fs::path> batch;

      for (int i = 0; modules[i] != nullptr; ++i)
      {
        const fs::path p = fs::path(prefix) / modules[i];
        ec.clear();
        if (fs::exists(p, ec) && !ec)
        {
          batch.push_back(p);
          found = true;
        }
      }

      if (found)
        return batch;
    }

    return result;
  }

  std::optional<fs::path> find_vix_pch()
  {
    const char *home = vix::utils::vix_getenv(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
    );

    std::error_code ec;

    if (home && *home)
    {
      const fs::path pchDir = fs::path(home) / ".vix" / "cache" / "pch";
      for (const char *name : {"vix.hpp.gch", "vix.hpp.pch"})
      {
        const fs::path p = pchDir / name;
        ec.clear();
        if (fs::exists(p, ec) && !ec)
          return p;
      }
    }

    for (const char *prefix : {"/usr/local/include", "/usr/include"})
    {
      for (const char *name : {"vix.hpp.gch", "vix.hpp.pch"})
      {
        const fs::path p = fs::path(prefix) / name;
        ec.clear();
        if (fs::exists(p, ec) && !ec)
          return p;
      }
    }

    return std::nullopt;
  }
} // namespace vix::commands::RunCommand::detail
