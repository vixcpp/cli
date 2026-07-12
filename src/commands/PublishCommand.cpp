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
#include <vix/cli/commands/CloudCommand.hpp>
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
#include <regex>
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
      bool jsonOut{false};
      bool verbose{false};
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

    static std::string stable_hash_hex(const std::string &input)
    {
      std::uint64_t hash = 1469598103934665603ull;
      for (char ch : input)
      {
        const auto c = static_cast<unsigned char>(ch);
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
      }

      std::ostringstream out;
      out << std::hex << std::setw(16) << std::setfill('0') << hash;
      return out.str();
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

    static std::string registry_repo_url()
    {
      return "https://github.com/vixcpp/registry.git";
    }

    [[maybe_unused]] static std::string iso_utc_now()
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
            "local tag exists, but it has not been pushed to origin: " + tag +
            ". Run: git push origin " + tag);
      }

      return detected;
    }

    [[maybe_unused]] static json read_vix_json_object(const fs::path &repoRoot)
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

    [[maybe_unused]] static std::string https_repo_from_remote(const std::string &remoteUrl)
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

    static std::string normalize_repository_url(std::string url)
    {
      url = trim_copy(url);
      if (url.empty())
        return {};

      while (!url.empty() && url.back() == '/')
        url.pop_back();

      if (url.rfind("git@", 0) == 0)
      {
        const auto at = url.find('@');
        const auto colon = url.find(':', at == std::string::npos ? 0 : at);
        if (at != std::string::npos && colon != std::string::npos)
        {
          const std::string host = url.substr(at + 1, colon - at - 1);
          const std::string path = url.substr(colon + 1);
          url = "https://" + host + "/" + path;
        }
      }
      else if (url.rfind("ssh://git@", 0) == 0)
      {
        std::string rest = url.substr(std::string("ssh://git@").size());
        const auto slash = rest.find('/');
        if (slash != std::string::npos)
          url = "https://" + rest.substr(0, slash) + "/" + rest.substr(slash + 1);
      }

      if (url.size() >= 4 && url.rfind(".git") == url.size() - 4)
        url.erase(url.size() - 4);

      const auto scheme = url.find("://");
      if (scheme != std::string::npos)
      {
        const auto hostStart = scheme + 3;
        const auto slash = url.find('/', hostStart);
        if (slash != std::string::npos)
        {
          std::string schemePart = lower_copy(url.substr(0, scheme));
          std::string host = lower_copy(url.substr(hostStart, slash - hostStart));
          std::string path = url.substr(slash + 1);
          if (host == "github.com")
            path = lower_copy(path);
          url = schemePart + "://" + host + "/" + path;
        }
      }

      return url;
    }

    static std::optional<std::string> git_origin_url()
    {
      const auto r = run_process_capture({"git", "remote", "get-url", "origin"});
      if (r.exitCode != 0 || trim_copy(r.out).empty())
        return std::nullopt;
      return trim_copy(r.out);
    }

    static std::optional<std::string> git_remote_commit_for_tag(const std::string &tag)
    {
      const auto r = run_process_capture({"git", "ls-remote", "--tags", "origin", "refs/tags/" + tag, "refs/tags/" + tag + "^{}"});
      if (r.exitCode != 0 || trim_copy(r.out).empty())
        return std::nullopt;

      std::istringstream iss(r.out);
      std::string line;
      std::string first;
      while (std::getline(iss, line))
      {
        line = trim_copy(line);
        if (line.empty())
          continue;
        const auto tab = line.find_first_of(" \t");
        if (tab == std::string::npos)
          continue;
        const std::string sha = line.substr(0, tab);
        const std::string ref = trim_copy(line.substr(tab + 1));
        if (first.empty())
          first = sha;
        if (ref == "refs/tags/" + tag + "^{}")
          return sha;
      }
      if (!first.empty())
        return first;
      return std::nullopt;
    }

    static bool is_valid_package_atom(const std::string &value)
    {
      if (value.empty())
        return false;

      for (char c : value)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '-' || c == '_' || c == '.'))
          return false;
      }

      return value.find("..") == std::string::npos &&
             value.front() != '.' && value.back() != '.';
    }

    static bool is_safe_relative_path(const std::string &value)
    {
      if (value.empty())
        return false;
      fs::path p(value);
      if (p.is_absolute())
        return false;
      for (const auto &part : p)
      {
        if (part == "..")
          return false;
      }
      return true;
    }

    static std::vector<std::string> manifest_include_roots(const json &manifest)
    {
      std::vector<std::string> roots;
      if (manifest.contains("include") && manifest["include"].is_string())
        roots.push_back(manifest["include"].get<std::string>());
      if (manifest.contains("includes") && manifest["includes"].is_array())
      {
        for (const auto &item : manifest["includes"])
          if (item.is_string())
            roots.push_back(item.get<std::string>());
      }
      if (roots.empty())
        roots.push_back("include");
      std::sort(roots.begin(), roots.end());
      roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
      return roots;
    }

    struct PublicHeader
    {
      std::string includeRoot;
      std::string path;
      fs::path absolute;
    };

    static std::vector<PublicHeader> scan_public_headers_or_throw(const fs::path &repoRoot, const json &manifest)
    {
      std::vector<PublicHeader> headers;
      const fs::path rootAbs = fs::weakly_canonical(repoRoot);
      for (const std::string &root : manifest_include_roots(manifest))
      {
        if (!is_safe_relative_path(root))
          throw std::runtime_error("invalid include path in vix.json: " + root);

        const fs::path includeRoot = repoRoot / root;
        std::error_code ec;
        if (!fs::exists(includeRoot, ec) || ec)
          continue;
        const fs::path includeRootAbs = fs::weakly_canonical(includeRoot, ec);
        if (ec || includeRootAbs.string().rfind(rootAbs.string(), 0) != 0)
          throw std::runtime_error("include path escapes repository: " + root);

        for (auto it = fs::recursive_directory_iterator(includeRootAbs, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); ++it)
        {
          if (it->is_symlink(ec))
          {
            const fs::path target = fs::weakly_canonical(it->path(), ec);
            if (ec || target.string().rfind(rootAbs.string(), 0) != 0)
              throw std::runtime_error("public include symlink escapes repository: " + it->path().string());
          }
          if (!it->is_regular_file(ec) || ec)
            continue;
          const std::string ext = lower_copy(it->path().extension().string());
          if (ext != ".h" && ext != ".hpp" && ext != ".hh" && ext != ".hxx" && ext != ".ipp")
            continue;
          const std::string name = it->path().filename().string();
          if (name.rfind(".", 0) == 0 || name.find("~") != std::string::npos)
            continue;
          const fs::path rel = fs::relative(it->path(), includeRootAbs, ec);
          if (ec)
            continue;
          headers.push_back({root, rel.generic_string(), it->path()});
        }
      }
      std::sort(headers.begin(), headers.end(), [](const PublicHeader &a, const PublicHeader &b)
                { return std::tie(a.includeRoot, a.path) < std::tie(b.includeRoot, b.path); });
      return headers;
    }

    [[maybe_unused]] static std::string read_text_file_or_empty_local(const fs::path &p)
    {
      std::ifstream in(p, std::ios::binary);
      if (!in)
        return {};
      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    static json generate_api_document(const std::string &pkgId,
                                      const std::string &version,
                                      const std::string &commit,
                                      const std::vector<PublicHeader> &headers)
    {
      json api = json::object();
      api["format"] = "vix-api-1";
      api["package"] = pkgId;
      api["version"] = version;
      api["commit"] = commit;
      api["headers"] = json::array();

      const std::regex functionLike(R"(^\s*(?:template\s*<[^;]+>\s*)?(?:inline\s+|constexpr\s+|static\s+|virtual\s+|friend\s+)*([A-Za-z_][A-Za-z0-9_:<>~,&*\s]+)\s+([A-Za-z_][A-Za-z0-9_:~]*)\s*\(([^;{}]*)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:->\s*[^;{]+)?[;{])");
      const std::regex classLike(R"(^\s*(class|struct|enum\s+class|enum)\s+([A-Za-z_][A-Za-z0-9_]*)\b)");
      const std::regex nsLike(R"(^\s*namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{?)");

      for (const auto &header : headers)
      {
        json h = json::object();
        h["path"] = header.path;
        h["includeRoot"] = header.includeRoot;
        h["symbols"] = json::array();

        std::ifstream in(header.absolute);
        std::string line;
        int lineNo = 0;
        std::vector<std::string> namespaces;
        std::string pendingDoc;
        while (std::getline(in, line))
        {
          ++lineNo;
          std::string t = trim_copy(line);
          if (t.rfind("///", 0) == 0)
          {
            if (!pendingDoc.empty())
              pendingDoc += "\n";
            pendingDoc += trim_copy(t.substr(3));
            continue;
          }
          std::smatch m;
          if (std::regex_search(line, m, nsLike))
          {
            namespaces.push_back(m[1].str());
            pendingDoc.clear();
            continue;
          }
          if (std::regex_search(line, m, classLike))
          {
            const std::string name = m[2].str();
            std::string qualified = name;
            if (!namespaces.empty())
              qualified = namespaces.back() + "::" + name;
            h["symbols"].push_back({{"kind", m[1].str()}, {"qualifiedName", qualified}, {"signature", t}, {"documentation", pendingDoc}, {"line", lineNo}});
            pendingDoc.clear();
            continue;
          }
          if (std::regex_search(line, m, functionLike))
          {
            const std::string name = m[2].str();
            std::string qualified = name;
            if (!namespaces.empty() && name.find("::") == std::string::npos)
              qualified = namespaces.back() + "::" + name;
            h["symbols"].push_back({{"kind", "function"}, {"qualifiedName", qualified}, {"signature", t}, {"documentation", pendingDoc}, {"line", lineNo}});
            pendingDoc.clear();
            continue;
          }
          if (!t.empty() && t.rfind("//", 0) != 0)
            pendingDoc.clear();
        }

        api["headers"].push_back(h);
      }
      return api;
    }

    static std::optional<std::pair<std::string, fs::path>> find_registry_entry_by_repository(const fs::path &regIndex,
                                                                                            const std::string &normalizedRepo)
    {
      if (normalizedRepo.empty() || !fs::exists(regIndex))
        return std::nullopt;
      std::error_code ec;
      for (auto it = fs::directory_iterator(regIndex, ec); !ec && it != fs::directory_iterator(); ++it)
      {
        if (!it->is_regular_file() || it->path().extension() != ".json")
          continue;
        try
        {
          json entry = read_json_or_throw(it->path());
          std::string repo;
          if (entry.contains("repo") && entry["repo"].is_object() && entry["repo"].contains("url") && entry["repo"]["url"].is_string())
            repo = entry["repo"]["url"].get<std::string>();
          else if (entry.contains("repository") && entry["repository"].is_string())
            repo = entry["repository"].get<std::string>();
          if (normalize_repository_url(repo) != normalizedRepo)
            continue;
          std::string ns = entry.value("namespace", std::string{});
          std::string name = entry.value("name", std::string{});
          if (!ns.empty() && !name.empty())
            return std::make_pair(ns + "/" + name, it->path());
        }
        catch (...)
        {
        }
      }
      return std::nullopt;
    }

    static void validate_publish_manifest_or_throw(const json &manifest,
                                                   const std::string &resolvedVersion,
                                                   const std::string &originRepo)
    {
      auto require_string = [&](const char *key) -> std::string
      {
        if (!manifest.contains(key) || !manifest[key].is_string() || trim_copy(manifest[key].get<std::string>()).empty())
          throw std::runtime_error(std::string("invalid vix.json: missing string field `") + key + "`");
        return trim_copy(manifest[key].get<std::string>());
      };

      const std::string ns = lower_copy(require_string("namespace"));
      const std::string name = lower_copy(require_string("name"));
      const std::string version = require_string("version");
      const std::string type = require_string("type");
      const std::string license = require_string("license");
      const std::string description = require_string("description");
      const std::string repo = require_string("repository");

      if (!is_valid_package_atom(ns))
        throw std::runtime_error("invalid vix.json: `namespace` contains unsupported characters");
      if (!is_valid_package_atom(name))
        throw std::runtime_error("invalid vix.json: `name` contains unsupported characters");
      if (version != resolvedVersion)
        throw std::runtime_error("Version mismatch\n\n  vix.json\n  " + version + "\n\n  Git tag\n  v" + resolvedVersion);
      if (type != "header-only" && type != "library" && type != "header-and-source" && type != "executable")
        throw std::runtime_error("invalid vix.json: unsupported package type `" + type + "`");
      if (description.size() < 8)
        throw std::runtime_error("invalid vix.json: description is too short");
      if (license.empty())
        throw std::runtime_error("invalid vix.json: license is required");
      if (normalize_repository_url(repo) != originRepo)
        throw std::runtime_error("Repository mismatch\n\n  vix.json\n  " + repo + "\n\n  Git origin\n  " + originRepo);
      if (manifest.contains("keywords") && !manifest["keywords"].is_array())
        throw std::runtime_error("invalid vix.json: keywords must be an array");
      if (!manifest.contains("authors") || !manifest["authors"].is_array() || manifest["authors"].empty())
        throw std::runtime_error("invalid vix.json: authors must be a non-empty array");
      for (const auto &author : manifest["authors"])
      {
        if (!author.is_object() || !author.contains("name") || !author["name"].is_string() || trim_copy(author["name"].get<std::string>()).empty())
          throw std::runtime_error("invalid vix.json: each author needs a name");
      }
      for (const std::string &root : manifest_include_roots(manifest))
      {
        if (!is_safe_relative_path(root))
          throw std::runtime_error("invalid vix.json: unsafe include path `" + root + "`");
      }
    }

    static fs::path create_tag_checkout_or_throw(const std::string &commit)
    {
      std::error_code ec;
      fs::path base = fs::temp_directory_path(ec);
      if (ec)
        base = fs::current_path();
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      fs::path checkout = base / ("vix-publish-tag-" + std::to_string(static_cast<long long>(stamp)));
      const auto r = run_process_capture({"git", "worktree", "add", "--detach", "--quiet", checkout.string(), commit});
      if (r.exitCode != 0)
        throw std::runtime_error("failed to checkout tagged source: " + trim_copy(r.err));
      return checkout;
    }

    struct TaggedCheckoutGuard
    {
      fs::path path;
      ~TaggedCheckoutGuard()
      {
        if (!path.empty())
        {
          run_process_capture({"git", "worktree", "remove", "--force", path.string()});
          std::error_code ec;
          fs::remove_all(path, ec);
        }
      }
    };

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

    [[maybe_unused]] static std::string git_user_name()
    {
      const auto r = run_process_capture({"git", "config", "user.name"});
      if (r.exitCode == 0 && !r.out.empty())
        return trim_copy(r.out);
      return {};
    }

    [[maybe_unused]] static std::optional<std::pair<std::string, std::string>> infer_from_git_remote()
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

    static int ensure_registry_repo_for_publish(const fs::path &regRepo)
    {
      const fs::path regIndex = regRepo / "index";

      if (fs::exists(regRepo) && is_git_repo(regRepo) && fs::exists(regIndex))
      {
        return 0;
      }

      if (fs::exists(regRepo) && !is_git_repo(regRepo))
      {
        vix::cli::util::err_line(
            std::cerr,
            "registry path exists but is not a git repository: " + regRepo.string());

        vix::cli::util::warn_line(
            std::cerr,
            "Remove this directory or run: vix registry sync");

        return 1;
      }

      fs::create_directories(regRepo.parent_path());

      vix::cli::util::step("registry not found, cloning registry index...");

      const auto r = run_process_retry_debug(
          {"git", "clone", "-q", "--depth", "1", registry_repo_url(), regRepo.string()});

      if (r.exitCode != 0)
      {
        vix::cli::util::err_line(std::cerr, "failed to clone registry index");

        if (!r.err.empty())
          vix::cli::util::warn_line(std::cerr, r.err);

        return r.exitCode;
      }

      if (!fs::exists(regIndex))
      {
        vix::cli::util::err_line(
            std::cerr,
            "invalid registry clone: missing index directory");

        return 1;
      }

      return 0;
    }

    static int normalize_registry_worktree_for_publish(const fs::path &regRepo)
    {
      {
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "fetch", "-q", "origin", "--prune"});

        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(
              std::cerr,
              "failed to fetch registry origin");

          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);

          return r.exitCode;
        }
      }

      {
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "checkout", "-q", "-B", "main", "origin/main"});

        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(
              std::cerr,
              "failed to checkout registry main branch");

          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);

          return r.exitCode;
        }
      }

      {
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "reset", "-q", "--hard", "origin/main"});

        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(
              std::cerr,
              "failed to reset registry to origin/main");

          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);

          return r.exitCode;
        }
      }

      {
        const auto r = run_process_retry_debug(
            {"git", "-C", regRepo.string(), "clean", "-q", "-fd", "--", "index"});

        if (r.exitCode != 0)
        {
          vix::cli::util::err_line(
              std::cerr,
              "failed to clean untracked registry files");

          if (!r.err.empty())
            vix::cli::util::warn_line(std::cerr, r.err);

          return r.exitCode;
        }
      }

      return 0;
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


    static bool has_cloud_flag(const std::vector<std::string> &args)
    {
      return std::find(args.begin(), args.end(), "--cloud") != args.end();
    }

    static std::vector<std::string> without_cloud_flag(const std::vector<std::string> &args)
    {
      std::vector<std::string> out;
      out.reserve(args.size());
      for (const auto &arg : args)
      {
        if (arg != "--cloud")
          out.push_back(arg);
      }
      return out;
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

        if (a == "--json")
        {
          opt.jsonOut = true;
          continue;
        }

        if (a == "--verbose" || a == "-v")
        {
          opt.verbose = true;
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
      if (opt.verbose && !opt.jsonOut)
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

      const auto commitOpt = git_commit_for_tag(tag);
      if (!commitOpt)
      {
        vix::cli::util::err_line(std::cerr, "failed to resolve commit for tag: " + tag);
        return 1;
      }
      const std::string commit = *commitOpt;

      const auto remoteCommitOpt = git_remote_commit_for_tag(tag);
      if (!remoteCommitOpt)
      {
        vix::cli::util::err_line(std::cerr, "Tag " + tag + " is not on origin");
        vix::cli::util::warn_line(std::cerr, "Run: git push origin " + tag);
        return 1;
      }

      if (*remoteCommitOpt != commit)
      {
        vix::cli::util::err_line(std::cerr, "Tag local and origin commits differ");
        if (opt.verbose)
        {
          vix::cli::util::kv(std::cerr, "local", commit);
          vix::cli::util::kv(std::cerr, "origin", *remoteCommitOpt);
        }
        return 1;
      }

      const auto originRaw = git_origin_url();
      if (!originRaw)
      {
        vix::cli::util::err_line(std::cerr, "git origin is not configured");
        return 1;
      }

      const std::string originRepo = normalize_repository_url(*originRaw);
      if (originRepo.empty())
      {
        vix::cli::util::err_line(std::cerr, "git origin URL is invalid");
        return 1;
      }

      fs::path taggedCheckout;
      try
      {
        taggedCheckout = create_tag_checkout_or_throw(commit);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, ex.what());
        return 1;
      }

      TaggedCheckoutGuard taggedCheckoutGuard{taggedCheckout};

      json vixManifest = json::object();
      std::optional<std::string> ns;
      std::optional<std::string> name;
      std::vector<PublicHeader> publicHeaders;
      json apiDocument = json::object();

      try
      {
        const fs::path manifestPath = taggedCheckout / "vix.json";
        if (!file_exists_nonempty(manifestPath))
          throw std::runtime_error("invalid vix.json: missing vix.json in tagged source");

        vixManifest = read_json_or_throw(manifestPath);
        if (!vixManifest.is_object())
          throw std::runtime_error("invalid vix.json: root must be an object");

        validate_publish_manifest_or_throw(vixManifest, resolvedVersion, originRepo);

        ns = lower_copy(trim_copy(vixManifest["namespace"].get<std::string>()));
        name = lower_copy(trim_copy(vixManifest["name"].get<std::string>()));

        publicHeaders = scan_public_headers_or_throw(taggedCheckout, vixManifest);
        const std::string type = vixManifest.value("type", std::string{});
        if ((type == "header-only" || type == "library" || type == "header-and-source") && publicHeaders.empty())
          throw std::runtime_error("No public headers found");

        apiDocument = generate_api_document(*ns + "/" + *name, resolvedVersion, commit, publicHeaders);
      }
      catch (const std::exception &ex)
      {
        vix::cli::util::err_line(std::cerr, ex.what());
        return 1;
      }

      if (opt.verbose && !opt.jsonOut)
      {
        vix::cli::util::kv(std::cout, "repo", repoRoot.string());
        vix::cli::util::kv(std::cout, "tag checkout", taggedCheckout.string());
        vix::cli::util::kv(std::cout, "version", resolvedVersion);
        vix::cli::util::kv(std::cout, "tag", tag);
        vix::cli::util::kv(std::cout, "commit", commit);
        vix::cli::util::kv(std::cout, "headers", std::to_string(publicHeaders.size()));
      }

      const std::string pkgId = *ns + "/" + *name;
      if (opt.verbose && !opt.jsonOut)
        vix::cli::util::kv(std::cout, "id", pkgId);

      const fs::path regRepo = registry_repo_dir();
      const fs::path regIndex = registry_index_dir();

      if (opt.verbose && !opt.jsonOut)
        vix::cli::util::kv(std::cout, "registry", regRepo.string());

      {
        const int rc = ensure_registry_repo_for_publish(regRepo);
        if (rc != 0)
        {
          vix::cli::util::err_line(
              std::cerr,
              "failed to prepare registry index for publishing");

          return rc;
        }
      }

      {
        const int rc = normalize_registry_worktree_for_publish(regRepo);
        if (rc != 0)
        {
          vix::cli::util::err_line(
              std::cerr,
              "failed to prepare local registry repo");

          vix::cli::util::tip_line(
              std::cerr,
              "Check network access or run: vix registry sync");

          return rc;
        }
      }

      const fs::path entryPath = regIndex / registry_file_name(*ns, *name);
      if (opt.verbose && !opt.jsonOut)
        vix::cli::util::kv(std::cout, "entry", entryPath.string());

      if (const auto registeredByRepo = find_registry_entry_by_repository(regIndex, originRepo))
      {
        if (registeredByRepo->first != pkgId)
        {
          vix::cli::util::err_line(std::cerr, "Package identity changed");
          std::cerr << "\n  Registered\n  " << registeredByRepo->first
                    << "\n\n  Manifest\n  " << pkgId
                    << "\n\n  Repository\n  " << originRepo
                    << "\n\nUse an explicit rename workflow to change a published package identity.\n";
          return 1;
        }
      }

      const bool entryExists = fs::exists(entryPath);

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
          std::string registeredCommit;
          const auto &existingVersion = entry["versions"][resolvedVersion];
          if (existingVersion.is_object() && existingVersion.contains("commit") && existingVersion["commit"].is_string())
            registeredCommit = existingVersion["commit"].get<std::string>();

          if (registeredCommit == commit)
          {
            if (opt.jsonOut)
            {
              json out = {{"status", "published"}, {"package", pkgId}, {"version", resolvedVersion}, {"tag", tag}, {"commit", commit}};
              std::cout << out.dump(2) << "\n";
            }
            else
            {
              vix::cli::util::ok_line(std::cout, pkgId + "@" + resolvedVersion + " is already published");
            }
            return 0;
          }

          vix::cli::util::err_line(std::cerr, "Published version is immutable");
          std::cerr << "\n  Package\n  " << pkgId << "@" << resolvedVersion
                    << "\n\n  Registry commit\n  " << registeredCommit
                    << "\n\n  Current tag commit\n  " << commit << "\n";
          return 1;
        }
      }
      else
      {
        const std::string httpsUrl = originRepo;
        const std::string defaultBranch = guess_default_branch();

        const std::string desc = vixManifest["description"].get<std::string>();
        const std::string displayName =
            (vixManifest.contains("displayName") && vixManifest["displayName"].is_string())
                ? vixManifest["displayName"].get<std::string>()
                : *name;
        const std::string license = vixManifest["license"].get<std::string>();
        const std::string documentation =
            (vixManifest.contains("documentation") && vixManifest["documentation"].is_string())
                ? vixManifest["documentation"].get<std::string>()
                : (httpsUrl + "#readme");

        json keywords = json::array();
        if (vixManifest.contains("keywords") && vixManifest["keywords"].is_array())
          keywords = vixManifest["keywords"];

        json exports = json::object();
        if (vixManifest.contains("exports") && vixManifest["exports"].is_object())
          exports = vixManifest["exports"];
        if (!exports.contains("headers") || !exports["headers"].is_array())
          exports["headers"] = json::array();
        if (!exports.contains("modules") || !exports["modules"].is_array())
          exports["modules"] = json::array();
        if (!exports.contains("namespaces") || !exports["namespaces"].is_array())
          exports["namespaces"] = json::array();
        for (const auto &header : publicHeaders)
        {
          if (std::find(exports["headers"].begin(), exports["headers"].end(), header.path) == exports["headers"].end())
            exports["headers"].push_back(header.path);
        }

        json constraints = json::object();
        if (vixManifest.contains("constraints") && vixManifest["constraints"].is_object())
          constraints = vixManifest["constraints"];
        if (vixManifest.contains("standard") && vixManifest["standard"].is_string())
          constraints["minCppStandard"] = vixManifest["standard"].get<std::string>();
        if (!constraints.contains("platforms"))
          constraints["platforms"] = json::array({"linux", "macos", "windows"});

        json deps = json::object();
        if (vixManifest.contains("dependencies") && vixManifest["dependencies"].is_object())
          deps = vixManifest["dependencies"];
        if (vixManifest.contains("deps") && vixManifest["deps"].is_array())
          deps["registry"] = vixManifest["deps"];
        if (!deps.contains("git"))
          deps["git"] = json::array();
        if (!deps.contains("registry"))
          deps["registry"] = json::array();
        if (!deps.contains("system"))
          deps["system"] = json::array();

        json maintainers = json::array();
        if (vixManifest.contains("maintainers") && vixManifest["maintainers"].is_array())
          maintainers = vixManifest["maintainers"];
        else if (vixManifest.contains("authors") && vixManifest["authors"].is_array())
          maintainers = vixManifest["authors"];

        json quality = json::object();
        quality["ci"] = json::array();
        quality["hasDocs"] = !documentation.empty();
        quality["hasExamples"] = fs::exists(taggedCheckout / "examples");
        quality["hasTests"] = fs::exists(taggedCheckout / "tests") ||
                              fs::exists(taggedCheckout / "test") ||
                              fs::exists(taggedCheckout / "unittests");

        json api = json::object();
        api["format"] = "vix-api-1";
        api["generatedBy"] = "vix-cli";
        api["path"] = "vix.api.json";
        api["hash"] = stable_hash_hex(apiDocument.dump());
        api["document"] = apiDocument;

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
        entry["type"] = vixManifest["type"].get<std::string>();
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
      entry["api"]["document"] = apiDocument;
      entry["api"]["hash"] = stable_hash_hex(apiDocument.dump());

      if (!entry.contains("versions") || !entry["versions"].is_object())
        entry["versions"] = json::object();

      entry["versions"][resolvedVersion] = json::object({
          {"tag", tag},
          {"commit", commit},
          {"manifest", json::object({{"type", vixManifest["type"]}, {"include", manifest_include_roots(vixManifest)}})},
          {"api", json::object({{"format", "vix-api-1"}, {"path", "vix.api.json"}, {"hash", entry["api"]["hash"]}})},
      });

      if (opt.dryRun)
      {
        if (opt.jsonOut)
        {
          json out = {{"status", "ready"}, {"package", pkgId}, {"version", resolvedVersion}, {"tag", tag}, {"commit", commit}, {"headers", publicHeaders.size()}, {"api", apiDocument}};
          std::cout << out.dump(2) << "\n";
        }
        else
        {
          vix::cli::util::ok_line(std::cout, pkgId + "@" + resolvedVersion + " is ready to publish");
        }
        return 0;
      }

      const std::string branch = branch_name(*ns, *name, resolvedVersion);
      if (opt.verbose && !opt.jsonOut)
        vix::cli::util::kv(std::cout, "branch", branch);

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
          if (opt.verbose && !opt.jsonOut)
          {
            vix::cli::util::warn_line(std::cout,
                                      "gh pr create failed, continuing without failing publish.");
            if (!r.err.empty())
              vix::cli::util::warn_line(std::cout, r.err);
          }
        }
      }

      if (prCreated)
      {
        if (opt.jsonOut)
        {
          json out = {{"status", "published"}, {"package", pkgId}, {"version", resolvedVersion}, {"tag", tag}, {"commit", commit}, {"branch", branch}};
          std::cout << out.dump(2) << "\n";
        }
        else
        {
          vix::cli::util::ok_line(std::cout, "published " + pkgId + "@" + resolvedVersion);
        }
        return 0;
      }

      if (opt.jsonOut)
      {
        json out = {{"status", "submitted"}, {"package", pkgId}, {"version", resolvedVersion}, {"tag", tag}, {"commit", commit}, {"branch", branch}};
        std::cout << out.dump(2) << "\n";
      }
      else
      {
        vix::cli::util::ok_line(std::cout, "submitted " + pkgId + "@" + resolvedVersion);
        std::cout << "  Branch: " << branch << "\n";
      }
      return 0;
    }
  } // namespace

  int PublishCommand::run(const std::vector<std::string> &args)
  {
    if (has_cloud_flag(args))
      return CloudCommand::publish(without_cloud_flag(args));

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
        << "  vix publish [version] [--notes \"...\"] [--dry-run] [--json] [--verbose]\n"
        << "  vix publish --cloud [options]\n\n"

        << "Description:\n"
        << "  vix publish publishes a tagged version to the public Vix registry.\n"
        << "  vix publish --cloud publishes to Softadastra Cloud private registry.\n"
        << "  If public version is omitted, Vix uses the latest local SemVer tag and verifies it exists on origin.\n"
        << "  Published versions are immutable and package identity is tied to the git repository.\n\n"

        << "Public registry options:\n"
        << "  --notes \"...\"     Attach release notes\n"
        << "  --dry-run          Validate without pushing changes\n"
        << "  --json             Emit machine-readable JSON\n"
        << "  --verbose, -v      Show git, registry and validation details\n"
        << "  --cleanup          Remove older local publish branches in the registry clone\n\n"
        << "Cloud registry options:\n"
        << "  --cloud            Publish to Softadastra Cloud instead of the public registry\n"
        << "  --package <name>   Package name, for example vix/http\n"
        << "  --version <value>  Package version\n"
        << "  --visibility <private|public>\n"
        << "  --description <text>\n"
        << "  --repository-url <url>\n"
        << "  --archive <path>   Use an existing package.tar.gz\n"
        << "  --manifest <path>  Use manifest JSON file\n"
        << "  --dry-run          Show what would be published\n"
        << "  --json             Emit machine-readable JSON\n\n"

        << "Requirements:\n"
        << "  - Run inside a git repository\n"
        << "  - A tag v<version> must exist locally and on origin\n"
        << "  - vix.json version must match the tag\n"
        << "  - vix.json repository must match git origin\n"
        << "  - Public headers must exist under the declared include root\n\n"

        << "Examples:\n"
        << "  vix publish\n"
        << "  vix publish 0.2.0\n"
        << "  vix publish --notes \"Add helpers\"\n"
        << "  vix publish --dry-run\n"
        << "  vix publish --dry-run --json\n"
        << "  vix publish --cloud --package vix/testpkg --version 1.0.0\n"
        << "  vix cloud publish --dry-run\n";

    return 0;
  }

} // namespace vix::commands
