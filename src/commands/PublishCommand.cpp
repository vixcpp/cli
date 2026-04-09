/**
 * @file PublishCommand.cpp
 * @brief Implements `vix publish` command (registry publication via git + optional GitHub PR).
 * @author Gaspard Kirira
 *
 * @copyright Copyright (c) 2025 Gaspard Kirira
 * @license MIT
 *
 * Project: Vix.cpp
 * Repository: https://github.com/vixcpp/vix
 *
 * This source code is governed by the MIT license found in the LICENSE file.
 */
#include <vix/cli/commands/PublishCommand.hpp>
#include <vix/cli/util/Ui.hpp>
#include <vix/cli/util/Semver.hpp>
#include <vix/cli/Style.hpp>
#include <vix/utils/Env.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
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
using json = nlohmann::json;

namespace vix::commands
{
  using namespace vix::cli::style;

  namespace
  {
    struct PublishOptions
    {
      std::string version;
      std::string notes;
      bool dryRun{false};
      bool cleanup{false};
    };

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
      // entries folder inside the registry repo: ~/.vix/registry/index/index
      return registry_repo_dir() / "index";
    }

    static std::string iso_utc_now()
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

      std::ostringstream oss;
      oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
      return oss.str();
    }

    static bool file_exists_nonempty(const fs::path &p)
    {
      std::error_code ec;
      return fs::exists(p, ec) && fs::is_regular_file(p, ec);
    }

    static json read_json_or_throw(const fs::path &p)
    {
      std::ifstream in(p);
      if (!in)
        throw std::runtime_error("cannot open: " + p.string());
      json j;
      in >> j;
      return j;
    }

    static void write_json_or_throw(const fs::path &p, const json &j)
    {
      std::ofstream out(p);
      if (!out)
        throw std::runtime_error("cannot write: " + p.string());
      out << j.dump(2) << "\n";
    }

#if defined(_WIN32)
    static std::string join_for_log(
        const std::vector<std::string> &args)
    {
      std::ostringstream out;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        if (i > 0)
        {
          out << ' ';
        }

        const std::string &arg = args[i];
        const bool needsQuotes =
            arg.find(' ') != std::string::npos ||
            arg.find('\t') != std::string::npos ||
            arg.find('"') != std::string::npos;

        if (!needsQuotes)
        {
          out << arg;
          continue;
        }

        out << '"';
        for (char c : arg)
        {
          if (c == '"')
          {
            out << "\\\"";
          }
          else
          {
            out << c;
          }
        }
        out << '"';
      }

      return out.str();
    }
#endif

    struct ProcessResult
    {
      int exitCode{127};
      std::string out;
      std::string err;
    };

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

      // CreateProcess requires a writable buffer
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
        // child
        if (cwd)
        {
          if (chdir(cwd->c_str()) != 0)
            _exit(127);
        }

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
      // parent
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

    static ProcessResult run_process_retry_debug(
        const std::vector<std::string> &args,
        const std::optional<fs::path> &cwd = std::nullopt,
        int attempts = 2)
    {
      ProcessResult last;
      for (int i = 0; i < attempts; ++i)
      {
        last = run_process_capture(args, cwd);
        if (last.exitCode == 0)
          return last;

        // small retry for transient filesystem/network issues
        if (i + 1 < attempts)
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      return last;
    }

    static std::optional<std::string> git_top_level()
    {
      const auto r = run_process_capture({"git", "rev-parse", "--show-toplevel"});
      if (r.exitCode != 0 || r.out.empty())
        return std::nullopt;
      return r.out;
    }

    static bool git_is_clean()
    {
      const auto r = run_process_capture({"git", "status", "--porcelain"});
      if (r.exitCode != 0)
        return false;
      return r.out.empty();
    }

    static bool git_tag_exists(const std::string &tag)
    {
      {
        const auto r = run_process_capture({"git", "rev-parse", "-q", "--verify", tag + "^{tag}"});
        if (r.exitCode == 0)
          return true;
      }
      {
        const auto r = run_process_capture({"git", "rev-parse", "-q", "--verify", "refs/tags/" + tag});
        return r.exitCode == 0;
      }
    }

    static std::optional<std::string> git_commit_for_tag(const std::string &tag)
    {
      const auto r = run_process_capture({"git", "rev-list", "-n", "1", tag});
      if (r.exitCode != 0 || r.out.empty())
        return std::nullopt;
      return r.out;
    }

    static bool git_remote_tag_exists(const std::string &tag)
    {
      {
        const auto r = run_process_capture({"git", "ls-remote", "--tags", "origin", "refs/tags/" + tag});
        if (r.exitCode == 0 && !r.out.empty())
          return true;
      }
      {
        const auto r = run_process_capture({"git", "ls-remote", "--tags", "origin", "refs/tags/" + tag + "^{}"});
        return (r.exitCode == 0 && !r.out.empty());
      }
    }

    static std::optional<std::string> read_vix_json_namespace(const fs::path &repoRoot)
    {
      const fs::path p = repoRoot / "vix.json";
      if (!file_exists_nonempty(p))
        return std::nullopt;

      try
      {
        const json j = read_json_or_throw(p);
        if (j.is_object() && j.contains("namespace") && j["namespace"].is_string())
          return lower_copy(j["namespace"].get<std::string>());
      }
      catch (...)
      {
      }
      return std::nullopt;
    }

    static bool looks_like_semver_tag(const std::string &tag)
    {
      std::string s = trim_copy(tag);
      if (s.empty())
        return false;

      if (s[0] == 'v' || s[0] == 'V')
        s.erase(s.begin());

      if (s.empty())
        return false;

      const auto firstDot = s.find('.');
      if (firstDot == std::string::npos)
        return false;

      const auto secondDot = s.find('.', firstDot + 1);
      if (secondDot == std::string::npos)
        return false;

      return vix::cli::util::semver::compare(s, s) == 0;
    }

    static std::string normalize_version_from_tag(std::string tag)
    {
      tag = trim_copy(tag);
      if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        tag.erase(tag.begin());
      return trim_copy(tag);
    }

    static std::optional<std::string> git_latest_semver_tag()
    {
      const auto r = run_process_capture({"git", "tag", "--list"});
      if (r.exitCode != 0 || r.out.empty())
        return std::nullopt;

      std::istringstream iss(r.out);
      std::string line;
      std::vector<std::string> versions;

      while (std::getline(iss, line))
      {
        line = trim_copy(line);
        if (!looks_like_semver_tag(line))
          continue;

        versions.push_back(normalize_version_from_tag(line));
      }

      if (versions.empty())
        return std::nullopt;

      return vix::cli::util::semver::findLatest(versions);
    }

    static std::optional<std::string> resolve_publish_version_or_throw(
        const PublishOptions &opt)
    {
      const std::string explicitVersion = trim_copy(opt.version);

      if (!explicitVersion.empty())
      {
        const std::string tag = "v" + explicitVersion;

        if (!git_tag_exists(tag))
        {
          throw std::runtime_error(
              "tag not found locally: " + tag +
              ". Create it first or run `vix publish` without a version to use the latest git tag");
        }

        if (!git_remote_tag_exists(tag))
        {
          throw std::runtime_error(
              "tag not found on remote origin: " + tag +
              ". Push it first with: git push origin " + tag);
        }

        return explicitVersion;
      }

      const auto detected = git_latest_semver_tag();
      if (!detected.has_value())
      {
        throw std::runtime_error(
            "no publishable git tag found. Create and push a tag like v0.2.0");
      }

      const std::string tag = "v" + *detected;

      if (!git_remote_tag_exists(tag))
      {
        throw std::runtime_error(
            "latest local tag exists but is not on origin: " + tag +
            ". Push it first with: git push origin " + tag);
      }

      return detected;
    }

    static std::optional<std::string> read_vix_json_name(const fs::path &repoRoot)
    {
      const fs::path p = repoRoot / "vix.json";
      if (!file_exists_nonempty(p))
        return std::nullopt;

      try
      {
        const json j = read_json_or_throw(p);
        if (j.is_object() && j.contains("name") && j["name"].is_string())
          return lower_copy(j["name"].get<std::string>());
      }
      catch (...)
      {
      }
      return std::nullopt;
    }

    static json read_vix_json_object(const fs::path &repoRoot)
    {
      const fs::path p = repoRoot / "vix.json";
      if (!file_exists_nonempty(p))
        return json::object();
      try
      {
        const json j = read_json_or_throw(p);
        if (j.is_object())
          return j;
      }
      catch (...)
      {
      }
      return json::object();
    }

    static std::string https_repo_from_remote(const std::string &remoteUrl)
    {
      std::string httpsUrl = trim_copy(remoteUrl);
      if (httpsUrl.empty())
        return {};

      if (httpsUrl.find("git@") == 0)
      {
        const auto pos = httpsUrl.find(':');
        if (pos != std::string::npos)
        {
          const std::string path = httpsUrl.substr(pos + 1);
          httpsUrl = "https://github.com/" + path;
        }
      }

      if (httpsUrl.size() >= 4 && httpsUrl.rfind(".git") == httpsUrl.size() - 4)
        httpsUrl.erase(httpsUrl.size() - 4);

      return httpsUrl;
    }

    static std::string guess_default_branch()
    {
      // Best-effort: if this fails, fallback to main
      const auto r = run_process_capture({"git", "symbolic-ref", "refs/remotes/origin/HEAD"});
      if (r.exitCode != 0 || r.out.empty())
        return "main";

      // expected: refs/remotes/origin/main
      const auto slash = r.out.rfind('/');
      if (slash == std::string::npos || slash + 1 >= r.out.size())
        return "main";

      return trim_copy(r.out.substr(slash + 1));
    }

    static std::string git_user_name()
    {
      const auto r = run_process_capture({"git", "config", "user.name"});
      if (r.exitCode == 0 && !r.out.empty())
        return trim_copy(r.out);
      return {};
    }

    static std::optional<std::pair<std::string, std::string>> infer_from_git_remote()
    {
      const auto r = run_process_capture({"git", "remote", "get-url", "origin"});
      if (r.exitCode != 0 || r.out.empty())
        return std::nullopt;

      std::string u = r.out;

      auto strip_suffix = [&](const std::string &suf)
      {
        if (u.size() >= suf.size() && u.rfind(suf) == u.size() - suf.size())
          u.erase(u.size() - suf.size());
      };
      strip_suffix(".git");

      std::string path;

      const auto posAt = u.find('@');
      const auto posColon = u.find(':');
      if (posAt != std::string::npos && posColon != std::string::npos && posColon > posAt)
      {
        path = u.substr(posColon + 1);
      }
      else
      {
        const auto pos = u.find("://");
        if (pos != std::string::npos)
        {
          std::string rest = u.substr(pos + 3);
          const auto slash = rest.find('/');
          if (slash != std::string::npos)
            path = rest.substr(slash + 1);
        }
      }

      if (path.empty())
        return std::nullopt;

      const auto slash = path.find('/');
      if (slash == std::string::npos)
        return std::nullopt;

      std::string ns = path.substr(0, slash);
      std::string name = path.substr(slash + 1);

      if (ns.empty() || name.empty())
        return std::nullopt;

      return std::make_pair(lower_copy(ns), lower_copy(name));
    }

    static std::string registry_file_name(const std::string &ns, const std::string &name)
    {
      return ns + "." + name + ".json";
    }

    static std::string branch_name(const std::string &ns, const std::string &name, const std::string &version)
    {
      std::string b = "publish-" + ns + "-" + name + "-" + version;
      for (char &c : b)
      {
        if (c == '/')
          c = '-';
      }
      return b;
    }

    static bool is_git_repo(const fs::path &dir)
    {
      const auto r = run_process_capture(
          {"git", "rev-parse", "--is-inside-work-tree"},
          dir);

      return r.exitCode == 0 && trim_copy(r.out) == "true";
    }

    static bool command_exists_on_path(const std::string &exe)
    {
      // No shell: just try to execute "<exe> --version"
      const auto r = run_process_capture({exe, "--version"});
      return r.exitCode == 0;
    }

    static bool gh_is_authed()
    {
      const auto r = run_process_capture({"gh", "auth", "status", "-h", "github.com"});
      return r.exitCode == 0;
    }

    static PublishOptions parse_args_or_throw(const std::vector<std::string> &args)
    {
      PublishOptions opt;

      bool versionSet = false;

      for (size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "--dry-run")
        {
          opt.dryRun = true;
          continue;
        }

        if (a == "--cleanup")
        {
          opt.cleanup = true;
          continue;
        }

        if (a == "--notes")
        {
          if (i + 1 >= args.size())
            throw std::runtime_error("--notes requires a value");

          opt.notes = args[i + 1];
          ++i;
          continue;
        }

        const std::string prefix = "--notes=";
        if (a.rfind(prefix, 0) == 0)
        {
          opt.notes = a.substr(prefix.size());
          continue;
        }

        if (!a.empty() && a[0] == '-')
        {
          throw std::runtime_error("unknown flag: " + a);
        }

        if (versionSet)
        {
          throw std::runtime_error("too many positional arguments");
        }

        opt.version = trim_copy(a);
        versionSet = true;
      }

      return opt;
    }

    static int publish_impl(const PublishOptions &opt)
    {
      vix::cli::util::section(std::cout, "Publish");

      const auto repoRootStr = git_top_level();
      if (!repoRootStr)
      {
        vix::cli::util::err_line(std::cerr, "not inside a git repository");
        vix::cli::util::warn_line(std::cerr, "Run this inside your library repo.");
        return 1;
      }

      const fs::path repoRoot = *repoRootStr;

      if (!git_is_clean())
      {
        vix::cli::util::err_line(std::cerr, "working tree is not clean");
        vix::cli::util::warn_line(std::cerr, "Commit your changes before publishing.");
        return 1;
      }

      const auto resolvedVersionOpt = resolve_publish_version_or_throw(opt);
      if (!resolvedVersionOpt.has_value())
      {
        vix::cli::util::err_line(std::cerr, "could not resolve publish version");
        return 1;
      }

      const std::string resolvedVersion = *resolvedVersionOpt;
      const std::string tag = "v" + resolvedVersion;

      vix::cli::util::kv(std::cout, "repo", repoRoot.string());
      vix::cli::util::kv(std::cout, "version", resolvedVersion);
      vix::cli::util::kv(std::cout, "tag", tag);

      const auto commitOpt = git_commit_for_tag(tag);
      if (!commitOpt)
      {
        vix::cli::util::err_line(std::cerr, "failed to resolve commit for tag: " + tag);
        return 1;
      }

      const std::string commit = *commitOpt;
      vix::cli::util::kv(std::cout, "commit", commit);

      auto is_valid_package_atom = [](const std::string &value) -> bool
      {
        if (value.empty())
          return false;

        for (char c : value)
        {
          const unsigned char uc = static_cast<unsigned char>(c);
          if (!(std::isalnum(uc) || c == '-' || c == '_' || c == '.'))
            return false;
        }

        return true;
      };

      json vixManifest = json::object();
      std::optional<std::string> ns;
      std::optional<std::string> name;

      const fs::path manifestPath = repoRoot / "vix.json";
      const bool hasManifest = file_exists_nonempty(manifestPath);

      if (hasManifest)
      {
        try
        {
          vixManifest = read_json_or_throw(manifestPath);
        }
        catch (const std::exception &ex)
        {
          vix::cli::util::err_line(std::cerr, std::string("invalid vix.json: ") + ex.what());
          return 1;
        }

        if (!vixManifest.is_object())
        {
          vix::cli::util::err_line(std::cerr, "invalid vix.json: root must be an object");
          return 1;
        }

        if (!vixManifest.contains("namespace") || !vixManifest["namespace"].is_string())
        {
          vix::cli::util::err_line(std::cerr, "invalid vix.json: missing string field `namespace`");
          return 1;
        }

        if (!vixManifest.contains("name") || !vixManifest["name"].is_string())
        {
          vix::cli::util::err_line(std::cerr, "invalid vix.json: missing string field `name`");
          return 1;
        }

        const std::string manifestNs =
            lower_copy(trim_copy(vixManifest["namespace"].get<std::string>()));
        const std::string manifestName =
            lower_copy(trim_copy(vixManifest["name"].get<std::string>()));

        if (!is_valid_package_atom(manifestNs))
        {
          vix::cli::util::err_line(std::cerr,
                                   "invalid vix.json: `namespace` contains unsupported characters");
          return 1;
        }

        if (!is_valid_package_atom(manifestName))
        {
          vix::cli::util::err_line(std::cerr,
                                   "invalid vix.json: `name` contains unsupported characters");
          return 1;
        }

        ns = manifestNs;
        name = manifestName;
      }

      if (!ns || !name)
      {
        const auto inferred = infer_from_git_remote();
        if (inferred)
        {
          ns = inferred->first;
          name = inferred->second;
        }
      }

      if (!ns || !name || ns->empty() || name->empty())
      {
        vix::cli::util::err_line(std::cerr, "cannot infer package namespace/name");
        vix::cli::util::warn_line(
            std::cerr,
            "Fix: add { \"namespace\": \"...\", \"name\": \"...\" } in vix.json or ensure git remote origin is set.");
        return 1;
      }

      const std::string pkgId = *ns + "/" + *name;
      vix::cli::util::kv(std::cout, "id", pkgId);

      const fs::path regRepo = registry_repo_dir();
      const fs::path regIndex = registry_index_dir();

      vix::cli::util::kv(std::cout, "registry", regRepo.string());

      if (!fs::exists(regRepo) || !is_git_repo(regRepo) || !fs::exists(regIndex))
      {
        vix::cli::util::err_line(std::cerr, "registry is not available locally: " + regRepo.string());
        vix::cli::util::warn_line(std::cerr, "Run: vix registry sync");
        return 1;
      }

      const fs::path entryPath = regIndex / registry_file_name(*ns, *name);
      vix::cli::util::kv(std::cout, "entry", entryPath.string());

      const bool entryExists = fs::exists(entryPath);

      if (!git_remote_tag_exists(tag))
      {
        vix::cli::util::err_line(std::cerr, "tag not found on remote origin: " + tag);
        vix::cli::util::warn_line(std::cerr, "Fix: git push origin " + tag);
        vix::cli::util::warn_line(std::cerr, "Or:  git push --tags");
        return 1;
      }

      json entry = json::object();

      if (entryExists)
      {
        try
        {
          entry = read_json_or_throw(entryPath);
        }
        catch (const std::exception &ex)
        {
          vix::cli::util::err_line(std::cerr,
                                   std::string("failed to read registry entry: ") + ex.what());
          return 1;
        }

        if (!entry.is_object())
        {
          vix::cli::util::err_line(std::cerr, "invalid registry entry format");
          return 1;
        }

        if (!entry.contains("versions") || !entry["versions"].is_object())
        {
          vix::cli::util::err_line(std::cerr,
                                   "invalid registry entry: missing versions object");
          return 1;
        }

        if (entry["versions"].contains(resolvedVersion))
        {
          vix::cli::util::err_line(std::cerr,
                                   "version already registered: " + pkgId + "@" + resolvedVersion);
          vix::cli::util::warn_line(std::cerr,
                                    "This tag/version is already present in the registry.");
          return 1;
        }
      }
      else
      {
        std::string remoteUrl;
        {
          const auto r = run_process_capture({"git", "remote", "get-url", "origin"});
          remoteUrl = trim_copy(r.out);
        }

        const std::string httpsUrl = https_repo_from_remote(remoteUrl);
        const std::string defaultBranch = guess_default_branch();

        if (vixManifest.empty())
          vixManifest = read_vix_json_object(repoRoot);

        const std::string desc =
            (vixManifest.contains("description") && vixManifest["description"].is_string())
                ? vixManifest["description"].get<std::string>()
                : std::string();

        const std::string displayName =
            (vixManifest.contains("displayName") && vixManifest["displayName"].is_string())
                ? vixManifest["displayName"].get<std::string>()
                : *name;

        const std::string license =
            (vixManifest.contains("license") && vixManifest["license"].is_string())
                ? vixManifest["license"].get<std::string>()
                : std::string("MIT");

        const std::string documentation =
            (vixManifest.contains("documentation") && vixManifest["documentation"].is_string())
                ? vixManifest["documentation"].get<std::string>()
                : (httpsUrl.empty() ? std::string() : (httpsUrl + "#readme"));

        json keywords = json::array();
        if (vixManifest.contains("keywords") && vixManifest["keywords"].is_array())
          keywords = vixManifest["keywords"];

        json exports = json::object();
        if (vixManifest.contains("exports") && vixManifest["exports"].is_object())
        {
          exports = vixManifest["exports"];
        }
        else
        {
          exports["headers"] = json::array();
          exports["modules"] = json::array();
          exports["namespaces"] = json::array();

          const fs::path includeDir = repoRoot / "include";
          if (fs::exists(includeDir))
          {
            const fs::path candidate = includeDir / *name / (*name + ".hpp");
            if (fs::exists(candidate))
            {
              const std::string rel = (fs::path(*name) / (*name + ".hpp")).generic_string();
              exports["headers"].push_back(rel);
              exports["modules"].push_back(rel);
            }
          }
        }

        json constraints = json::object();
        if (vixManifest.contains("constraints") && vixManifest["constraints"].is_object())
        {
          constraints = vixManifest["constraints"];
        }
        else
        {
          constraints["minCppStandard"] = "c++17";
          constraints["platforms"] = json::array({"linux", "macos", "windows"});
        }

        json deps = json::object();
        if (vixManifest.contains("dependencies") && vixManifest["dependencies"].is_object())
        {
          deps = vixManifest["dependencies"];
        }
        else
        {
          deps["git"] = json::array();
          deps["registry"] = json::array();
          deps["system"] = json::array();
        }

        json maintainers = json::array();
        if (vixManifest.contains("maintainers") && vixManifest["maintainers"].is_array())
        {
          maintainers = vixManifest["maintainers"];
        }
        else
        {
          json m = json::object();
          m["github"] = *ns;
          const std::string uname = git_user_name();
          m["name"] = uname.empty() ? *ns : uname;
          maintainers.push_back(m);
        }

        json quality = json::object();
        quality["ci"] = json::array();
        quality["hasDocs"] = !documentation.empty();
        quality["hasExamples"] = fs::exists(repoRoot / "examples");
        quality["hasTests"] = fs::exists(repoRoot / "tests") ||
                              fs::exists(repoRoot / "test") ||
                              fs::exists(repoRoot / "unittests");

        json api = json::object();
        api["format"] = "vix-api-1";
        api["generatedBy"] = "vix-cli";
        api["path"] = "vix.api.json";
        api["updatedAt"] = iso_utc_now();

        entry = json::object();
        entry["api"] = api;

        entry["name"] = *name;
        entry["namespace"] = *ns;
        entry["displayName"] = displayName;
        entry["description"] = desc;
        entry["documentation"] = documentation;

        entry["keywords"] = keywords;
        entry["license"] = license;

        entry["exports"] = exports;
        entry["constraints"] = constraints;
        entry["dependencies"] = deps;

        entry["maintainers"] = maintainers;

        entry["manifestPath"] = "vix.json";
        entry["homepage"] = httpsUrl;
        entry["repo"] = json::object({{"url", httpsUrl}, {"defaultBranch", defaultBranch}});
        entry["type"] = "header-only";

        entry["quality"] = quality;

        entry["versions"] = json::object();
      }

      if (!entry.is_object())
      {
        vix::cli::util::err_line(std::cerr, "invalid registry entry format");
        return 1;
      }

      if (!entry.contains("api") || !entry["api"].is_object())
        entry["api"] = json::object();

      entry["api"]["format"] = "vix-api-1";
      entry["api"]["generatedBy"] = "vix-cli";
      entry["api"]["path"] = "vix.api.json";
      entry["api"]["updatedAt"] = iso_utc_now();

      if (!entry.contains("versions") || !entry["versions"].is_object())
        entry["versions"] = json::object();

      entry["versions"][resolvedVersion] = json::object({
          {"tag", tag},
          {"commit", commit},
      });

      if (opt.dryRun)
      {
        vix::cli::util::ok_line(std::cout, "dry-run: would update: " + entryPath.string());
        std::cout << "\n"
                  << entry.dump(2) << "\n";
        return 0;
      }

      std::error_code ec;
      fs::create_directories(entryPath.parent_path(), ec);

      try
      {
        write_json_or_throw(entryPath, entry);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr,
                                 std::string("failed to write registry entry: ") + ex.what());
        return 1;
      }

      const std::string branch = branch_name(*ns, *name, resolvedVersion);
      vix::cli::util::kv(std::cout, "branch", branch);

      {
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "pull", "-q", "--ff-only"});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr,
                                   "failed to update local registry repo (pull --ff-only)");

          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);

          vix::cli::util::tip_line(std::cerr,
                                   "Run 'vix registry sync' to reset the local registry.");

          return r.exitCode;
        }
      }

      {
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "checkout", "-B", branch, "-q"});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to create branch: " + branch);
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      {
        const std::string relEntry =
            (fs::path("index") / registry_file_name(*ns, *name)).generic_string();
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "add", relEntry});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to add registry entry");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      {
        const std::string msg = "registry: " + pkgId + " v" + resolvedVersion;
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "commit", "-q", "-m", msg});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to commit registry update");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      {
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "push", "-u", "origin", branch});
        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(std::cerr, "failed to push branch to origin");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);
          return r.exitCode;
        }
      }

      if (opt.cleanup)
      {
        const auto r = run_process_capture(
            {"git", "-C", regRepo.string(), "branch", "--format=%(refname:short)"});
        if (r.exitCode == 0 && !r.out.empty())
        {
          std::istringstream iss(r.out);
          std::string line;
          const std::string prefix = "publish-" + *ns + "-" + *name + "-";

          while (std::getline(iss, line))
          {
            line = trim_copy(line);
            if (line.empty() || line == branch)
              continue;

            if (line.rfind(prefix, 0) != 0)
              continue;

            run_process_capture({"git", "-C", regRepo.string(), "branch", "-D", line});
          }
        }
      }

      bool prCreated = false;

      if (command_exists_on_path("gh") && gh_is_authed())
      {
        const std::string title = "registry: add " + pkgId + " v" + resolvedVersion;

        std::ostringstream body;
        body << "Publish package `" << pkgId << "` version `" << resolvedVersion << "`.\n\n";
        body << "- tag: `" << tag << "`\n";
        body << "- commit: `" << commit << "`\n";

        if (!trim_copy(opt.notes).empty())
          body << "\nNotes:\n"
               << opt.notes << "\n";

        const auto r = run_process_retry_debug({
            "gh",
            "pr",
            "create",
            "--repo",
            "vixcpp/registry",
            "--base",
            "main",
            "--head",
            branch,
            "--title",
            title,
            "--body",
            body.str(),
        });

        if (r.exitCode == 0)
          prCreated = true;
        else
        {
          vix::cli::util::warn_line(std::cout,
                                    "gh pr create failed, continuing without failing publish.");
          if (!r.err.empty())
            vix::cli::util::warn_line(std::cout, r.err);
        }
      }

      if (prCreated)
      {
        vix::cli::util::ok_line(std::cout, "PR created for: " + pkgId + " v" + resolvedVersion);
        vix::cli::util::ok_line(std::cout,
                                "registry will reflect this version after the PR merges");
        return 0;
      }

      vix::cli::util::ok_line(std::cout, "branch pushed: " + branch);
      vix::cli::util::warn_line(std::cout,
                                "Create a PR on GitHub: vixcpp/registry <- " + branch);
      vix::cli::util::warn_line(std::cout,
                                "Tip: install/auth gh to auto-create PR next time.");
      return 0;
    }
  } // namespace

  int PublishCommand::run(const std::vector<std::string> &args)
  {
    try
    {
      const PublishOptions opt = parse_args_or_throw(args);
      return publish_impl(opt);
    }
    catch (const std::exception &ex)
    {
      vix::cli::util::err_line(std::cerr, std::string("publish failed: ") + ex.what());
      return 1;
    }
  }

  int PublishCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix publish [version] [--notes \"...\"] [--dry-run]\n\n"

        << "Description:\n"
        << "  Publish a tagged version of the current package to the Vix registry.\n"
        << "  If version is omitted, Vix uses the latest local SemVer tag and verifies it exists on origin.\n\n"

        << "Options:\n"
        << "  --notes \"...\"     Attach release notes\n"
        << "  --dry-run          Validate without pushing changes\n"
        << "  --cleanup          Remove older local publish branches in the registry clone\n\n"

        << "Requirements:\n"
        << "  - Run inside a git repository\n"
        << "  - A tag v<version> must exist locally and on origin\n\n"

        << "Examples:\n"
        << "  vix publish\n"
        << "  vix publish 0.2.0\n"
        << "  vix publish --notes \"Add helpers\"\n"
        << "  vix publish --dry-run\n";

    return 0;
  }
} // namespace vix::commands
