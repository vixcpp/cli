/**
 *
 *  @file CMakeBuild.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/cmake/CMakeBuild.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

#include <vix/cli/Style.hpp>
#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Strings.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vix::cli::build
{
  namespace util = vix::cli::util;
  namespace style = vix::cli::style;

  bool is_cmake_configure_summary_line(std::string_view line)
  {
    return (line.rfind("-- Configuring done", 0) == 0) ||
           (line.rfind("-- Generating done", 0) == 0) ||
           (line.find("Build files have been written to:") != std::string_view::npos);
  }

  bool is_configure_cmd(const std::vector<std::string> &argv)
  {
    bool hasS = false, hasB = false;
    for (const auto &a : argv)
    {
      if (a == "-S")
        hasS = true;
      if (a == "-B")
        hasB = true;
    }
    return !argv.empty() && argv[0] == "cmake" && hasS && hasB;
  }

  int default_jobs()
  {
    unsigned int hc = std::thread::hardware_concurrency();
    if (hc == 0)
      return 4;
    if (hc > 64)
      hc = 64;
    return static_cast<int>(hc);
  }

  std::vector<std::string> cmake_configure_argv(
      const process::Plan &plan,
      const process::Options &opt)
  {
    std::vector<std::string> argv;
    argv.reserve(32);

    argv.push_back("cmake");
    argv.push_back(opt.cmakeVerbose ? "--log-level=VERBOSE" : "--log-level=WARNING");

    argv.push_back("-S");
    argv.push_back(plan.projectDir.string());
    argv.push_back("-B");
    argv.push_back(plan.buildDir.string());
    argv.push_back("-G");
    argv.push_back(plan.preset.generator);

    for (const auto &kv : plan.cmakeVars)
      argv.push_back("-D" + kv.first + "=" + kv.second);

    for (const auto &a : opt.cmakeArgs)
      argv.push_back(a);

    return argv;
  }

  std::vector<std::string> cmake_build_argv(const process::Plan &plan,
                                            const process::Options &opt)
  {
    std::vector<std::string> argv;
    argv.reserve(16);

    argv.push_back("cmake");
    argv.push_back("--build");
    argv.push_back(plan.buildDir.string());

    int jobs = opt.jobs;
    if (jobs <= 0)
      jobs = default_jobs();

    if (!opt.buildTarget.empty())
    {
      argv.push_back("--target");
      argv.push_back(opt.buildTarget);
    }

    argv.push_back("--");
    argv.push_back("-j");
    argv.push_back(std::to_string(jobs));

    return argv;
  }

  std::vector<std::string> ninja_dry_run_argv(
      const process::Plan &plan,
      const process::Options &opt)
  {
    (void)opt;
    return {"ninja", "-C", plan.buildDir.string(), "-n"};
  }

  std::vector<std::pair<std::string, std::string>>
  ninja_env(const process::Options &opt, const process::Plan &plan)
  {
    std::vector<std::pair<std::string, std::string>> env;

    if (!opt.status)
      return env;

    if (plan.preset.generator == "Ninja")
      env.emplace_back("NINJA_STATUS", "[%f/%t %p%%] ");

    return env;
  }

#ifndef _WIN32

  process::ExecResult run_process_capture(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      std::string &outText)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
      r.exitCode = 127;
      return r;
    }

    pid_t pid = ::fork();
    if (pid == 0)
    {
      ::dup2(pipefd[1], STDOUT_FILENO);
      ::dup2(pipefd[1], STDERR_FILENO);
      ::close(pipefd[0]);
      ::close(pipefd[1]);

      for (const auto &kv : extraEnv)
        ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

      std::vector<char *> cargv;
      cargv.reserve(argv.size() + 1);
      for (const auto &s : argv)
        cargv.push_back(const_cast<char *>(s.c_str()));
      cargv.push_back(nullptr);

      ::execvp(cargv[0], cargv.data());
      _exit(127);
    }

    ::close(pipefd[1]);

    std::string buf(8 * 1024, '\0');
    while (true)
    {
      ssize_t n = ::read(pipefd[0], &buf[0], buf.size());
      if (n > 0)
        outText.append(buf.data(), static_cast<std::size_t>(n));
      else
        break;
    }

    ::close(pipefd[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0)
    {
      r.exitCode = 127;
      return r;
    }

    r.exitCode = process::normalize_exit_code(status);
    return r;
  }

  process::ExecResult run_process_live_to_log(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      const fs::path &logPath,
      bool quiet,
      bool cmakeVerbose,
      bool progressOnly)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    const bool filterCMakeSummary = is_configure_cmd(argv) && !cmakeVerbose;

    int pipefd[2];
    if (::pipe(pipefd) != 0)
    {
      r.exitCode = 127;
      return r;
    }

    int logfd = ::open(logPath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (logfd < 0)
    {
      ::close(pipefd[0]);
      ::close(pipefd[1]);
      r.exitCode = 127;
      return r;
    }

    pid_t pid = ::fork();
    if (pid == 0)
    {
      ::dup2(pipefd[1], STDOUT_FILENO);
      ::dup2(pipefd[1], STDERR_FILENO);

      ::close(pipefd[0]);
      ::close(pipefd[1]);

      for (const auto &kv : extraEnv)
        ::setenv(kv.first.c_str(), kv.second.c_str(), 1);

      std::vector<char *> cargv;
      cargv.reserve(argv.size() + 1);
      for (const auto &s : argv)
        cargv.push_back(const_cast<char *>(s.c_str()));
      cargv.push_back(nullptr);

      ::execvp(cargv[0], cargv.data());
      _exit(127);
    }

    ::close(pipefd[1]);

    std::string firstLine;
    bool gotFirstLine = false;

    std::string consoleBuf;
    consoleBuf.reserve(8192);

    auto is_progress_line = [](const std::string &line) -> bool
    {
      if (line.empty())
        return false;
      if (line[0] != '[')
        return false;
      const auto rb = line.find(']');
      if (rb == std::string::npos)
        return false;
      const auto slash = line.find('/', 1);
      const auto pct = line.find('%', 1);
      return (slash != std::string::npos && pct != std::string::npos && pct < rb);
    };

    auto should_echo_line = [&](const std::string &line) -> bool
    {
      if (quiet)
        return false;

      if (filterCMakeSummary)
      {
        return !is_cmake_configure_summary_line(line);
      }

      if (!progressOnly)
        return true;

      if (is_progress_line(line))
        return true;

      if (line.rfind("FAILED:", 0) == 0)
        return true;

      if (line.rfind("ninja:", 0) == 0)
        return true;

      return false;
    };

    std::string buf(16 * 1024, '\0');

    while (true)
    {
      ssize_t n = ::read(pipefd[0], &buf[0], buf.size());
      if (n <= 0)
        break;

      r.producedOutput = true;

      (void)::write(logfd, buf.data(), static_cast<std::size_t>(n));

      if (!gotFirstLine)
      {
        for (ssize_t i = 0; i < n; ++i)
        {
          char c = buf[static_cast<std::size_t>(i)];
          if (c == '\n')
          {
            gotFirstLine = true;
            break;
          }
          if (firstLine.size() < 200)
            firstLine.push_back(c);
        }
      }

      if (quiet)
        continue;

      consoleBuf.append(buf.data(), static_cast<std::size_t>(n));

      std::size_t start = 0;
      while (true)
      {
        std::size_t nl = consoleBuf.find('\n', start);
        if (nl == std::string::npos)
          break;

        std::string line = consoleBuf.substr(start, nl - start);

        if (should_echo_line(line))
        {
          line.push_back('\n');
          (void)::write(STDOUT_FILENO, line.data(), line.size());
        }

        start = nl + 1;
      }

      if (start > 0)
        consoleBuf.erase(0, start);
    }

    ::close(pipefd[0]);
    ::close(logfd);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0)
    {
      r.exitCode = 127;
      return r;
    }

    r.exitCode = process::normalize_exit_code(status);
    r.capturedFirstLine = util::trim(firstLine);
    return r;
  }

#else // _WIN32

  process::ExecResult run_process_capture(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      std::string &outText)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    for (const auto &kv : extraEnv)
    {
      std::string e = kv.first + "=" + kv.second;
      (void)_putenv(e.c_str());
    }

    fs::path tmp = fs::temp_directory_path() / "vix_build_capture.tmp";

    std::ostringstream oss;
    for (std::size_t i = 0; i < argv.size(); ++i)
    {
      if (i)
        oss << " ";
      oss << "\"" << argv[i] << "\"";
    }

    std::string cmd = oss.str() + " > \"" + tmp.string() + "\" 2>&1";
    int raw = std::system(cmd.c_str());
    r.exitCode = process::normalize_exit_code(raw);

    outText = util::read_text_file_or_empty(tmp);
    std::error_code ec{};
    fs::remove(tmp, ec);
    return r;
  }

  process::ExecResult run_process_live_to_log(
      const std::vector<std::string> &argv,
      const std::vector<std::pair<std::string, std::string>> &extraEnv,
      const fs::path &logPath,
      bool quiet,
      bool /*cmakeVerbose*/)
  {
    process::ExecResult r;
    r.displayCommand = util::join_display_cmd(argv);

    for (const auto &kv : extraEnv)
    {
      std::string e = kv.first + "=" + kv.second;
      (void)_putenv(e.c_str());
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < argv.size(); ++i)
    {
      if (i)
        oss << " ";
      oss << "\"" << argv[i] << "\"";
    }

    std::string full = oss.str() + " > \"" + logPath.string() + "\" 2>&1";
    int raw = std::system(full.c_str());
    r.exitCode = process::normalize_exit_code(raw);

    if (!quiet)
      std::cerr << util::read_text_file_or_empty(logPath);

    return r;
  }

#endif // _WIN32

  bool ninja_is_up_to_date(const process::Options &opt, const process::Plan &plan)
  {
    if (!opt.dryUpToDate)
      return false;
    if (plan.preset.generator != "Ninja")
      return false;

    std::string out;
    process::ExecResult r = run_process_capture(ninja_dry_run_argv(plan, opt),
                                                ninja_env(opt, plan), out);
    if (r.exitCode != 0)
      return false;

    out = util::trim(out);
    return out.empty();
  }

  void print_preset_summary(const process::Options &opt, const process::Plan &plan)
  {
    if (opt.quiet)
      return;

    if (plan.launcher)
      style::step(std::string("compiler cache: ") + *plan.launcher);
    if (plan.fastLinkerFlag)
      style::step(std::string("fast linker: ") + *plan.fastLinkerFlag);

    for (const auto &kv : plan.cmakeVars)
      style::step(kv.first + "=" + kv.second);

    std::cout << "\n";
  }

} // namespace vix::cli::build
