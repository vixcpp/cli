/**
 *
 *  @file UnpublishCommand.cpp
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
#include <vix/cli/commands/UnpublishCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    static std::string trim_copy(std::string s)
    {
      auto isws = [](unsigned char c)
      { return std::isspace(c) != 0; };
      while (!s.empty() && isws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
      while (!s.empty() && isws(static_cast<unsigned char>(s.back())))
        s.pop_back();
      return s;
    }

    static std::string lower_copy(std::string s)
    {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c)
                     { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    static std::string iso_utc_now_compact()
    {
      using namespace std::chrono;
      const auto now = system_clock::now();
      const std::time_t t = system_clock::to_time_t(now);

      std::tm tm{};
#if defined(_WIN32)
      gmtime_s(&tm, &t);
#else
      gmtime_r(&t, &tm);
#endif

      char buf[32]{0};
      std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
      return std::string(buf);
    }

    static std::string home_dir()
    {
#ifdef _WIN32
      const char *home = vix::utils::vix_getenv("USERPROFILE");
#else
      const char *home = vix::utils::vix_getenv("HOME");
#endif
      return home ? std::string(home) : std::string();
    }

    static fs::path vix_root()
    {
      const std::string h = home_dir();
      if (h.empty())
        return fs::path(".vix");
      return fs::path(h) / ".vix";
    }

    static fs::path registry_repo_dir()
    {
      // vix registry sync clones into ~/.vix/registry/index
      return vix_root() / "registry" / "index";
    }

    static fs::path registry_index_dir()
    {
      // entries: ~/.vix/registry/index/index
      return registry_repo_dir() / "index";
    }

    static bool is_git_repo(const fs::path &dir)
    {
      std::error_code ec;
      return fs::exists(dir / ".git", ec);
    }

    /* =========================
       Process runner (no system)
    ========================= */

    struct ProcessResult
    {
      int exitCode{127};
      std::string out;
      std::string err;
    };

    static std::string join_for_log(const std::vector<std::string> &args)
    {
      std::ostringstream oss;
      for (size_t i = 0; i < args.size(); ++i)
      {
        if (i)
          oss << ' ';
        const std::string &a = args[i];
        const bool needsQuotes = (a.find(' ') != std::string::npos) || (a.find('"') != std::string::npos);
        if (!needsQuotes)
        {
          oss << a;
          continue;
        }
        oss << '"';
        for (char c : a)
        {
          if (c == '"')
            oss << "\\\"";
          else
            oss << c;
        }
        oss << '"';
      }
      return oss.str();
    }

#if defined(_WIN32)

    static std::string win_last_error()
    {
      const DWORD err = GetLastError();
      if (!err)
        return {};
      LPSTR buf = nullptr;
      const DWORD n = FormatMessageA(
          FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr,
          err,
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPSTR)&buf,
          0,
          nullptr);
      std::string s = (n && buf) ? std::string(buf, buf + n) : std::string();
      if (buf)
        LocalFree(buf);
      return trim_copy(s);
    }

    static std::string win_read_all(HANDLE h)
    {
      std::string out;
      char buf[4096];
      DWORD read = 0;
      while (true)
      {
        const BOOL ok = ReadFile(h, buf, (DWORD)sizeof(buf), &read, nullptr);
        if (!ok || read == 0)
          break;
        out.append(buf, buf + read);
      }
      return out;
    }

    static ProcessResult run_process_capture(const std::vector<std::string> &args, const std::optional<fs::path> &cwd = std::nullopt)
    {
      ProcessResult r;

      if (args.empty())
      {
        r.exitCode = 127;
        r.err = "empty command";
        return r;
      }

      SECURITY_ATTRIBUTES sa{};
      sa.nLength = sizeof(sa);
      sa.bInheritHandle = TRUE;

      HANDLE outRead = nullptr, outWrite = nullptr;
      HANDLE errRead = nullptr, errWrite = nullptr;

      if (!CreatePipe(&outRead, &outWrite, &sa, 0))
      {
        r.exitCode = 127;
        r.err = "CreatePipe(stdout) failed: " + win_last_error();
        return r;
      }
      if (!SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0))
      {
        r.exitCode = 127;
        r.err = "SetHandleInformation(stdout) failed: " + win_last_error();
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return r;
      }

      if (!CreatePipe(&errRead, &errWrite, &sa, 0))
      {
        r.exitCode = 127;
        r.err = "CreatePipe(stderr) failed: " + win_last_error();
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return r;
      }
      if (!SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0))
      {
        r.exitCode = 127;
        r.err = "SetHandleInformation(stderr) failed: " + win_last_error();
        CloseHandle(outRead);
        CloseHandle(outWrite);
        CloseHandle(errRead);
        CloseHandle(errWrite);
        return r;
      }

      STARTUPINFOA si{};
      si.cb = sizeof(si);
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = outWrite;
      si.hStdError = errWrite;
      si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

      PROCESS_INFORMATION pi{};
      std::string cmd = join_for_log(args);

      std::vector<char> cmdBuf(cmd.begin(), cmd.end());
      cmdBuf.push_back('\0');

      std::string cwdStr;
      LPCSTR cwdPtr = nullptr;
      if (cwd)
      {
        cwdStr = cwd->string();
        cwdPtr = cwdStr.c_str();
      }

      const BOOL ok = CreateProcessA(
          nullptr,
          cmdBuf.data(),
          nullptr,
          nullptr,
          TRUE,
          0,
          nullptr,
          cwdPtr,
          &si,
          &pi);

      CloseHandle(outWrite);
      CloseHandle(errWrite);

      if (!ok)
      {
        r.exitCode = 127;
        r.err = "CreateProcess failed: " + win_last_error();
        CloseHandle(outRead);
        CloseHandle(errRead);
        return r;
      }

      WaitForSingleObject(pi.hProcess, INFINITE);

      DWORD ec = 127;
      GetExitCodeProcess(pi.hProcess, &ec);
      r.exitCode = (int)ec;

      r.out = win_read_all(outRead);
      r.err = win_read_all(errRead);

      CloseHandle(outRead);
      CloseHandle(errRead);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);

      r.out = trim_copy(r.out);
      r.err = trim_copy(r.err);
      return r;
    }

#else

    static int set_cloexec(int fd)
    {
      const int flags = fcntl(fd, F_GETFD);
      if (flags < 0)
        return -1;
      return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }

    static std::string read_fd_all(int fd)
    {
      std::string out;
      char buf[4096];
      while (true)
      {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0)
        {
          out.append(buf, buf + n);
          continue;
        }
        if (n == 0)
          break;
        if (errno == EINTR)
          continue;
        break;
      }
      return out;
    }

    static ProcessResult run_process_capture(const std::vector<std::string> &args, const std::optional<fs::path> &cwd = std::nullopt)
    {
      ProcessResult r;

      if (args.empty())
      {
        r.exitCode = 127;
        r.err = "empty command";
        return r;
      }

      int outPipe[2]{-1, -1};
      int errPipe[2]{-1, -1};

      if (pipe(outPipe) != 0)
      {
        r.exitCode = 127;
        r.err = "pipe(stdout) failed";
        return r;
      }
      if (pipe(errPipe) != 0)
      {
        r.exitCode = 127;
        r.err = "pipe(stderr) failed";
        close(outPipe[0]);
        close(outPipe[1]);
        return r;
      }

      set_cloexec(outPipe[0]);
      set_cloexec(outPipe[1]);
      set_cloexec(errPipe[0]);
      set_cloexec(errPipe[1]);

      pid_t pid = fork();
      if (pid < 0)
      {
        r.exitCode = 127;
        r.err = "fork failed";
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);
        return r;
      }

      if (pid == 0)
      {
        if (cwd)
          (void)chdir(cwd->c_str());

        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);

        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &a : args)
          argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
      }

      close(outPipe[1]);
      close(errPipe[1]);

      r.out = read_fd_all(outPipe[0]);
      r.err = read_fd_all(errPipe[0]);

      close(outPipe[0]);
      close(errPipe[0]);

      int status = 0;
      while (waitpid(pid, &status, 0) < 0)
      {
        if (errno == EINTR)
          continue;
        break;
      }

      if (WIFEXITED(status))
        r.exitCode = WEXITSTATUS(status);
      else if (WIFSIGNALED(status))
        r.exitCode = 128 + WTERMSIG(status);
      else
        r.exitCode = 127;

      r.out = trim_copy(r.out);
      r.err = trim_copy(r.err);
      return r;
    }

#endif

    static ProcessResult run_process_retry(const std::vector<std::string> &args, const std::optional<fs::path> &cwd = std::nullopt, int attempts = 2)
    {
      ProcessResult last;
      for (int i = 0; i < attempts; ++i)
      {
        last = run_process_capture(args, cwd);
        if (last.exitCode == 0)
          return last;

        if (i + 1 < attempts)
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      return last;
    }

    static bool command_exists_on_path(const std::string &exe)
    {
      const auto r = run_process_capture({exe, "--version"});
      return r.exitCode == 0;
    }

    static bool gh_is_authed()
    {
      const auto r = run_process_capture({"gh", "auth", "status", "-h", "github.com"});
      return r.exitCode == 0;
    }

    /* =========================
       Unpublish logic
    ========================= */

    struct Options
    {
      std::string id;  // namespace/name
      bool yes{false}; // skip prompt
    };

    static std::optional<std::pair<std::string, std::string>> split_pkg_id(const std::string &id)
    {
      const auto pos = id.find('/');
      if (pos == std::string::npos)
        return std::nullopt;

      std::string ns = lower_copy(trim_copy(id.substr(0, pos)));
      std::string name = lower_copy(trim_copy(id.substr(pos + 1)));

      if (ns.empty() || name.empty())
        return std::nullopt;

      return std::make_pair(ns, name);
    }

    static std::string registry_file_name(const std::string &ns, const std::string &name)
    {
      return ns + "." + name + ".json";
    }

    static std::string branch_name(const std::string &ns, const std::string &name)
    {
      return "unpublish-" + ns + "-" + name + "-" + iso_utc_now_compact();
    }

    static bool confirm_delete(const std::string &pkgId, bool yes)
    {
      if (yes)
        return true;

      vix::cli::util::warn_line(std::cout, "This will remove the package from the Vix registry.");
      vix::cli::util::warn_line(std::cout, "Changes are auto-merged after validation.");
      vix::cli::util::warn_line(std::cout, "Package: " + pkgId);
      vix::cli::util::warn_line(std::cout, "Type DELETE to confirm: ");

      std::string line;
      std::getline(std::cin, line);
      line = trim_copy(line);
      return line == "DELETE";
    }

    static Options parse_args_or_throw(const std::vector<std::string> &args)
    {
      Options opt;
      if (args.empty())
        throw std::runtime_error("missing package id. Try: vix unpublish namespace/name");

      std::vector<std::string> pos;
      for (size_t i = 0; i < args.size(); ++i)
      {
        const auto &a = args[i];
        if (a == "-y" || a == "--yes")
        {
          opt.yes = true;
          continue;
        }
        if (a == "-h" || a == "--help")
          throw std::runtime_error("help");
        if (!a.empty() && a[0] == '-')
          throw std::runtime_error("unknown flag: " + a);
        pos.push_back(a);
      }

      if (pos.empty())
        throw std::runtime_error("missing package id");

      opt.id = pos[0];
      opt.id = trim_copy(opt.id);
      if (opt.id.empty())
        throw std::runtime_error("package id cannot be empty");

      return opt;
    }

    static int unpublish_impl(const Options &opt)
    {
      vix::cli::util::section(std::cout, "Unpublish");

      const auto parts = split_pkg_id(opt.id);
      if (!parts)
      {
        vix::cli::util::err_line(std::cerr, "invalid id format, expected namespace/name");
        return 1;
      }

      const std::string ns = parts->first;
      const std::string name = parts->second;
      const std::string pkgId = ns + "/" + name;

      vix::cli::util::kv(std::cout, "id", pkgId);

      const fs::path regRepo = registry_repo_dir();
      const fs::path regIndex = registry_index_dir();

      vix::cli::util::kv(std::cout, "registry", regRepo.string());

      if (!fs::exists(regRepo) || !is_git_repo(regRepo) || !fs::exists(regIndex))
      {
        vix::cli::util::err_line(std::cerr, "registry is not available locally");
        vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
        return 1;
      }

      const fs::path entryPath = regIndex / registry_file_name(ns, name);
      vix::cli::util::kv(std::cout, "entry", entryPath.string());

      if (!fs::exists(entryPath))
      {
        vix::cli::util::err_line(std::cerr, "registry entry not found locally");
        vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
        return 1;
      }

      if (!confirm_delete(pkgId, opt.yes))
      {
        vix::cli::util::warn_line(std::cout, "cancelled");
        return 0;
      }

      const std::string branch = branch_name(ns, name);
      vix::cli::util::kv(std::cout, "branch", branch);

      {
        const auto r = run_process_retry({"git", "-C", regRepo.string(), "pull", "-q", "--ff-only"});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to update local registry repo");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      {
        const auto r = run_process_retry({"git", "-C", regRepo.string(), "checkout", "-B", branch, "-q"});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to create branch");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      {
        std::error_code ec;
        fs::remove(entryPath, ec);
        if (ec)
        {
          vix::cli::util::err_line(std::cerr, "failed to delete entry file");
          return 1;
        }
      }

      {
        const auto r = run_process_retry({"git", "-C", regRepo.string(), "add", "-A"});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to stage changes");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      {
        const std::string msg = "registry: unpublish " + pkgId;
        const auto r = run_process_retry({"git", "-C", regRepo.string(), "commit", "-q", "-m", msg});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to commit");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      {
        const auto r = run_process_retry({"git", "-C", regRepo.string(), "push", "-u", "origin", branch});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to push branch");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      // Best effort PR creation
      if (!command_exists_on_path("gh"))
      {
        vix::cli::util::warn_line(std::cout, "gh not found, skipping PR creation");
        vix::cli::util::warn_line(std::cout, "Create PR manually: vixcpp/registry <- " + branch);
        return 0;
      }

      if (!gh_is_authed())
      {
        vix::cli::util::warn_line(std::cout, "gh is installed but not authenticated");
        vix::cli::util::warn_line(std::cout, "Run: gh auth login");
        vix::cli::util::warn_line(std::cout, "Then create PR: vixcpp/registry <- " + branch);
        return 0;
      }

      {
        const std::string title = "registry: unpublish " + pkgId;
        const std::string body =
            "Removes " + pkgId + " from the Vix registry.\n\n"
                                 "Deleted file:\n"
                                 "- index/" +
            registry_file_name(ns, name) + "\n";

        const auto r = run_process_retry({"gh", "pr", "create",
                                          "--repo", "vixcpp/registry",
                                          "--base", "main",
                                          "--head", branch,
                                          "--title", title,
                                          "--body", body});

        if (r.exitCode == 0)
        {
          vix::cli::util::ok_line(std::cout, "PR created");
          return 0;
        }

        vix::cli::util::warn_line(std::cout, "gh pr create failed, continuing");
        if (!r.err.empty())
          vix::cli::util::warn_line(std::cout, r.err);

        vix::cli::util::warn_line(std::cout, "Create PR manually: vixcpp/registry <- " + branch);
        return 0;
      }
    }
  } // namespace

  int UnpublishCommand::run(const std::vector<std::string> &args)
  {
    try
    {
      const Options opt = parse_args_or_throw(args);
      return unpublish_impl(opt);
    }
    catch (const std::exception &ex)
    {
      if (std::string(ex.what()) == "help")
        return help();

      vix::cli::util::err_line(std::cerr, std::string("unpublish failed: ") + ex.what());
      return 1;
    }
  }

  int UnpublishCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix unpublish <namespace/name> [-y|--yes]\n\n"

        << "Description:\n"
        << "  Remove a package from the Vix registry (auto-merged after validation).\n\n"

        << "Options:\n"
        << "  -y, --yes      Skip confirmation prompt\n\n"

        << "Requirements:\n"
        << "  - Local registry must be synced (vix registry sync)\n\n"

        << "Confirmation:\n"
        << "  - You must type DELETE to confirm\n\n"

        << "Examples:\n"
        << "  vix unpublish gaspardkirira/strings\n"
        << "  vix unpublish gaspardkirira/strings --yes\n";

    return 0;
  }

} // namespace vix::commands
