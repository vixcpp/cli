#include <vix/cli/project/native/GitPackageContentSource.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vix::cli::project::native
{
  namespace fs = std::filesystem;
  namespace
  {
    bool valid_commit_token(std::string_view commit)
    {
      return (commit.size() == 40 || commit.size() == 64) &&
        std::all_of(commit.begin(), commit.end(), [](char c)
        { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

#ifdef _WIN32
    std::string quote_windows(const std::string &value)
    {
      std::string out{"\""};
      std::size_t slashes = 0;
      for (const char c : value)
      {
        if (c == '\\') { ++slashes; continue; }
        if (c == '"') { out.append(slashes * 2 + 1, '\\'); out.push_back(c); slashes = 0; continue; }
        out.append(slashes, '\\'); slashes = 0; out.push_back(c);
      }
      out.append(slashes * 2, '\\');
      out.push_back('"');
      return out;
    }
#endif

    bool valid_utf8(std::string_view value)
    {
      std::size_t i = 0;
      while (i < value.size())
      {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c < 0x80) { ++i; continue; }
        std::size_t n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (!n || i + n > value.size()) return false;
        std::uint32_t cp = c & (n == 2 ? 0x1fu : n == 3 ? 0x0fu : 0x07u);
        for (std::size_t j = 1; j < n; ++j)
        {
          const auto d = static_cast<unsigned char>(value[i + j]);
          if ((d & 0xc0u) != 0x80u) return false;
          cp = (cp << 6u) | (d & 0x3fu);
        }
        if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
        i += n;
      }
      return true;
    }

    bool valid_canonical_path(std::string_view path)
    {
      if (path.empty() || path.front() == '/' || path.back() == '/' || path.find('\0') != std::string_view::npos)
        return false;
      std::size_t start = 0;
      while (start <= path.size())
      {
        const auto slash = path.find('/', start);
        const auto end = slash == std::string_view::npos ? path.size() : slash;
        const auto segment = path.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        if (slash == std::string_view::npos) break;
        start = slash + 1;
      }
      return true;
    }

    GitPackageContentSourceResult failed(std::string message)
    { return {PackageContentSourceStatus::transport_failure, {}, std::move(message)}; }

    GitPackageContentSourceResult invalid(std::string message)
    { return {PackageContentSourceStatus::available, {}, std::move(message)}; }

    struct TreeLeaf { std::string mode; std::string type; std::string oid; std::string path; };

    bool parse_tree(std::string_view bytes, std::vector<TreeLeaf> &leaves, std::string &error)
    {
      std::size_t pos = 0;
      while (pos < bytes.size())
      {
        const auto nul = bytes.find('\0', pos);
        if (nul == std::string_view::npos) { error = "ls-tree output is not NUL terminated"; return false; }
        const auto tab = bytes.find('\t', pos);
        if (tab == std::string_view::npos || tab >= nul) { error = "ls-tree record lacks path separator"; return false; }
        const auto header = bytes.substr(pos, tab - pos);
        const auto first = header.find(' ');
        const auto second = first == std::string_view::npos ? first : header.find(' ', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos ||
            header.find(' ', second + 1) != std::string_view::npos)
        { error = "ls-tree record header is malformed"; return false; }
        TreeLeaf leaf{std::string(header.substr(0, first)),
          std::string(header.substr(first + 1, second - first - 1)),
          std::string(header.substr(second + 1)), std::string(bytes.substr(tab + 1, nul - tab - 1))};
        if (!valid_utf8(leaf.path) || !valid_canonical_path(leaf.path))
        { error = "Git tree path is invalid UTF-8 or violates canonical package path rules"; return false; }
        if (leaf.type != "blob" || (leaf.mode != "100644" && leaf.mode != "100755" && leaf.mode != "120000"))
        { error = leaf.mode == "160000" ? "Git submodule/gitlink is unsupported" : "unexpected Git tree leaf mode or object type"; return false; }
        if ((leaf.oid.size() != 40 && leaf.oid.size() != 64) ||
            !std::all_of(leaf.oid.begin(), leaf.oid.end(), [](char c)
            { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        { error = "Git tree object ID is malformed"; return false; }
        leaves.push_back(std::move(leaf));
        pos = nul + 1;
      }
      return true;
    }

    bool parse_batch(std::string_view bytes, const std::vector<TreeLeaf> &leaves,
      CanonicalPackageTree &tree, std::string &error)
    {
      std::size_t pos = 0;
      tree.entries.reserve(leaves.size());
      for (const auto &leaf : leaves)
      {
        const auto lineEnd = bytes.find('\n', pos);
        if (lineEnd == std::string_view::npos) { error = "cat-file batch header is truncated"; return false; }
        const auto header = bytes.substr(pos, lineEnd - pos);
        const auto space1 = header.find(' ');
        const auto space2 = space1 == std::string_view::npos ? space1 : header.find(' ', space1 + 1);
        if (space1 == std::string_view::npos || space2 == std::string_view::npos ||
            header.find(' ', space2 + 1) != std::string_view::npos ||
            header.substr(space1 + 1, space2 - space1 - 1) != "blob" ||
            header.substr(0, space1) != leaf.oid)
        { error = "cat-file batch returned a missing, mismatched, or non-blob object"; return false; }
        std::size_t size = 0;
        const auto sizeText = header.substr(space2 + 1);
        if (sizeText.empty()) { error = "cat-file batch size is empty"; return false; }
        for (const char c : sizeText)
        {
          const auto digit = static_cast<std::size_t>(c - '0');
          if (c < '0' || c > '9' || size > (static_cast<std::size_t>(-1) - digit) / 10u)
          { error = "cat-file batch size is invalid or overflows"; return false; }
          size = size * 10u + digit;
        }
        pos = lineEnd + 1;
        if (size > bytes.size() - pos || bytes.size() - pos - size < 1 || bytes[pos + size] != '\n')
        { error = "cat-file batch blob payload is truncated"; return false; }

        CanonicalPackageEntry entry;
        entry.path = leaf.path;
        if (leaf.mode == "120000")
        {
          entry.kind = CanonicalPackageEntryKind::symlink;
          entry.target.assign(bytes.data() + pos, size);
          if (entry.target.find('\0') != std::string::npos || !valid_utf8(entry.target))
          { error = "Git symlink target is not valid UTF-8"; return false; }
        }
        else
        {
          entry.kind = CanonicalPackageEntryKind::file;
          entry.executable = leaf.mode == "100755";
          entry.content.reserve(size);
          for (std::size_t i = 0; i < size; ++i)
            entry.content.push_back(static_cast<std::byte>(static_cast<unsigned char>(bytes[pos + i])));
        }
        tree.entries.push_back(std::move(entry));
        pos += size + 1;
      }
      if (pos != bytes.size()) { error = "cat-file batch output has trailing bytes"; return false; }
      return true;
    }
  } // namespace

  GitCommandResult NativeGitCommandRunner::run(const GitCommand &command)
  {
    if (command.arguments.empty()) return {};
    std::vector<std::string> arguments;
    arguments.reserve(command.arguments.size() + 2);
    arguments.push_back(command.arguments.front());
    arguments.push_back("-c");
    arguments.push_back("credential.interactive=never");
    arguments.insert(arguments.end(), command.arguments.begin() + 1, command.arguments.end());
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE outRead{}, outWrite{}, errRead{}, errWrite{}, inRead{}, inWrite{};
    if (!CreatePipe(&outRead, &outWrite, &sa, 0) || !CreatePipe(&errRead, &errWrite, &sa, 0) ||
        !CreatePipe(&inRead, &inWrite, &sa, 0)) return {};
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    std::string commandLine;
    for (const auto &arg : arguments)
    { if (!commandLine.empty()) commandLine.push_back(' '); commandLine += quote_windows(arg); }
    std::vector<char> mutableLine(commandLine.begin(), commandLine.end()); mutableLine.push_back('\0');
    STARTUPINFOA startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inRead; startup.hStdOutput = outWrite; startup.hStdError = errWrite;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessA(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(inRead); CloseHandle(outWrite); CloseHandle(errWrite);
    if (!created) { CloseHandle(outRead); CloseHandle(errRead); CloseHandle(inWrite); return {}; }
    GitCommandResult result; result.started = true;
    std::thread writer([&]
    {
      DWORD wrote = 0;
      std::size_t pos = 0;
      while (pos < command.standardInput.size())
      {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(
          command.standardInput.size() - pos, 1u << 20));
        if (!WriteFile(inWrite, command.standardInput.data() + pos, count, &wrote, nullptr) || !wrote)
        {
          break;
        }
        pos += wrote;
      }
      CloseHandle(inWrite);
    });
    auto reader = [](HANDLE handle, std::string &target)
    {
      char buffer[8192];
      DWORD got = 0;
      while (ReadFile(handle, buffer, sizeof(buffer), &got, nullptr) && got)
      {
        target.append(buffer, got);
      }
      CloseHandle(handle);
    };
    std::thread outReader(reader, outRead, std::ref(result.standardOutput));
    std::thread errReader(reader, errRead, std::ref(result.standardError));
    WaitForSingleObject(process.hProcess, INFINITE); DWORD code = 127; GetExitCodeProcess(process.hProcess, &code);
    result.exitCode = static_cast<int>(code); writer.join(); outReader.join(); errReader.join();
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return result;
#else
    int inPipe[2], outPipe[2], errPipe[2];
    if (::pipe(inPipe) || ::pipe(outPipe) || ::pipe(errPipe)) return {};
    const pid_t pid = ::fork();
    if (pid < 0) return {};
    if (pid == 0)
    {
      (void)::dup2(inPipe[0], STDIN_FILENO); (void)::dup2(outPipe[1], STDOUT_FILENO); (void)::dup2(errPipe[1], STDERR_FILENO);
      ::close(inPipe[0]); ::close(inPipe[1]); ::close(outPipe[0]); ::close(outPipe[1]); ::close(errPipe[0]); ::close(errPipe[1]);
      (void)::setenv("GIT_TERMINAL_PROMPT", "0", 1);
      std::vector<char *> argv; argv.reserve(arguments.size() + 1);
      for (const auto &arg : arguments)
      {
        argv.push_back(const_cast<char *>(arg.c_str()));
      }
      argv.push_back(nullptr);
      ::execvp(argv[0], argv.data()); _exit(127);
    }
    ::close(inPipe[0]); ::close(outPipe[1]); ::close(errPipe[1]);
    GitCommandResult result; result.started = true;
    auto writeInput = [&] {
      sigset_t blockedSignals;
      (void)::sigemptyset(&blockedSignals);
      (void)::sigaddset(&blockedSignals, SIGPIPE);
      (void)::pthread_sigmask(SIG_BLOCK, &blockedSignals, nullptr);
      std::size_t pos = 0;
      while (pos < command.standardInput.size())
      {
        const auto amount = ::write(inPipe[1], command.standardInput.data() + pos,
          command.standardInput.size() - pos);
        if (amount < 0 && errno == EINTR)
        {
          continue;
        }
        if (amount <= 0)
        {
          break;
        }
        pos += static_cast<std::size_t>(amount);
      }
      ::close(inPipe[1]); };
    auto readOutput = [](int fd, std::string &target)
    {
      char buffer[8192];
      for (;;)
      {
        const auto amount = ::read(fd, buffer, sizeof(buffer));
        if (amount < 0 && errno == EINTR)
        {
          continue;
        }
        if (amount <= 0)
        {
          break;
        }
        target.append(buffer, static_cast<std::size_t>(amount));
      }
      ::close(fd);
    };
    std::thread writer(writeInput), outReader(readOutput, outPipe[0], std::ref(result.standardOutput)),
      errReader(readOutput, errPipe[0], std::ref(result.standardError));
    int status = 0;
    pid_t waited = -1;
    do { waited = ::waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    writer.join(); outReader.join(); errReader.join();
    if (waited < 0) return {};
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128; return result;
#endif
  }

  GitCliPackageContentSource::GitCliPackageContentSource(fs::path workRoot, GitCommandRunner &runner)
    : workRoot_(std::move(workRoot)), runner_(runner) {}

  GitPackageContentSourceResult GitCliPackageContentSource::acquire(const GitPackageContentRequest &request)
  {
    if (!request.repositoryRoot || !valid_commit_token(request.fullCommit) ||
        request.normalizedRepository.empty() || request.normalizedRepository.find_first_of("\r\n") != std::string::npos ||
        request.normalizedRepository.find('\0') != std::string::npos)
      return invalid("Git content request does not contain an exact full locked commit and repository root");
    std::error_code ec;
    fs::create_directories(workRoot_, ec);
    if (ec) return failed("cannot create injected Git acquisition work root: " + ec.message());
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path workspace = workRoot_ / ("vix-git-content-" + std::to_string(stamp));
    for (unsigned attempt = 0; attempt < 32 && !fs::create_directory(workspace, ec); ++attempt)
    { ec.clear(); workspace = workRoot_ / ("vix-git-content-" + std::to_string(stamp) + "-" + std::to_string(attempt + 1)); }
    if (ec || !fs::is_directory(workspace, ec)) return failed("cannot create isolated Git acquisition workspace");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{workspace};
    const auto gitDir = (workspace / "objects.git").string();
    auto run = [&](std::vector<std::string> args, std::string input = {})
    { return runner_.run({std::move(args), std::move(input)}); };
    auto init = run({"git", "init", "--bare", "--quiet", gitDir});
    if (!init.started || init.exitCode != 0) return failed("could not initialize isolated Git object database");
    const std::string commit = request.fullCommit;
    auto fetch = run({"git", "--git-dir", gitDir, "fetch", "--no-tags", "--depth=1", "--",
      request.normalizedRepository, commit});
    if (!fetch.started) return failed("Git executable unavailable during exact-commit fetch");
    auto type = run({"git", "--git-dir", gitDir, "cat-file", "-t", commit});
    if (!type.started || type.exitCode != 0)
    {
      fetch = run({"git", "--git-dir", gitDir, "fetch", "--no-tags", "--",
        request.normalizedRepository, "+refs/*:refs/vix-acquisition/*"});
      if (!fetch.started || fetch.exitCode != 0) return failed("Git transport could not obtain repository objects");
      type = run({"git", "--git-dir", gitDir, "cat-file", "-t", commit});
    }
    if (!type.started || type.exitCode != 0) return failed("exact locked Git commit is unavailable after object acquisition");
    while (!type.standardOutput.empty() && (type.standardOutput.back() == '\n' || type.standardOutput.back() == '\r')) type.standardOutput.pop_back();
    if (type.standardOutput != "commit") return invalid("locked Git object is not itself a commit; tag peeling is forbidden");
    auto listing = run({"git", "--git-dir", gitDir, "ls-tree", "-r", "-z", "--full-tree", commit});
    if (!listing.started || listing.exitCode != 0) return failed("cannot enumerate exact locked commit tree");
    std::vector<TreeLeaf> leaves; std::string error;
    if (!parse_tree(listing.standardOutput, leaves, error)) return invalid("invalid exact-commit Git tree: " + error);
    std::string input;
    for (const auto &leaf : leaves) { input += leaf.oid; input.push_back('\n'); }
    if (leaves.empty())
      return {PackageContentSourceStatus::available, std::make_shared<CanonicalPackageContentCandidate>(CanonicalPackageTree{}), {}};
    auto blobs = run({"git", "--git-dir", gitDir, "cat-file", "--batch"}, std::move(input));
    if (!blobs.started || blobs.exitCode != 0) return failed("cannot read Git blob batch");
    CanonicalPackageTree candidate;
    if (!parse_batch(blobs.standardOutput, leaves, candidate, error)) return invalid("invalid Git cat-file batch protocol: " + error);
    return {PackageContentSourceStatus::available,
      std::make_shared<CanonicalPackageContentCandidate>(std::move(candidate)), {}};
  }
} // namespace vix::cli::project::native
