/**
 *
 *  @file Fs.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira.  All rights reserved.
 *  https://github.com/vixcpp/vix
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 */
#include <vix/cli/util/Fs.hpp>
#include <vix/utils/Env.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vix::cli::util
{
  bool file_exists(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && !ec;
  }

  bool dir_exists(const fs::path &p)
  {
    std::error_code ec{};
    return fs::exists(p, ec) && fs::is_directory(p, ec) && !ec;
  }

  bool ensure_dir(const fs::path &p, std::string &err)
  {
    if (dir_exists(p))
      return true;

    std::error_code ec{};
    fs::create_directories(p, ec);
    if (ec)
    {
      err = ec.message();
      return false;
    }
    return true;
  }

  std::string read_text_file_or_empty(const fs::path &p)
  {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs)
      return {};

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
  }

  bool write_text_file_atomic(const fs::path &p, const std::string &content)
  {
    std::error_code ec{};
    if (!p.parent_path().empty())
      fs::create_directories(p.parent_path(), ec);

    const fs::path tmp = p.string() + ".tmp";
    {
      std::ofstream ofs(tmp, std::ios::binary);
      if (!ofs)
        return false;

      ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
      ofs.flush();
      if (!ofs)
        return false;
    }

    fs::rename(tmp, p, ec);
    if (ec)
    {
      fs::remove(tmp, ec);
      return false;
    }
    return true;
  }

  std::optional<fs::path> find_project_root(fs::path start)
  {
    std::error_code ec{};
    start = fs::weakly_canonical(start, ec);
    if (ec)
      return std::nullopt;

    for (fs::path p = start; !p.empty(); p = p.parent_path())
    {
      if (file_exists(p / "CMakeLists.txt"))
        return p;

      if (p == p.root_path())
        break;
    }
    return std::nullopt;
  }

  void collect_files_recursive(
      const fs::path &root,
      const std::string &ext,
      std::vector<fs::path> &out)
  {
    std::error_code ec{};
    if (!dir_exists(root))
      return;

    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
      if (ec)
        break;

      if (!it->is_regular_file(ec))
        continue;

      const fs::path p = it->path();
      if (ext.empty() || p.extension() == ext)
        out.push_back(p);
    }
  }

  void print_log_tail_fast(const fs::path &logPath, std::size_t maxLines)
  {
    std::ifstream ifs(logPath, std::ios::binary);
    if (!ifs)
      return;

    ifs.seekg(0, std::ios::end);
    const std::streamoff size = ifs.tellg();
    if (size <= 0)
      return;

    const std::streamoff chunkSize = 64 * 1024; // 64 KB
    std::string buffer;
    buffer.reserve(static_cast<std::size_t>(std::min<std::streamoff>(size, chunkSize)));

    std::streamoff pos = size;
    std::size_t linesFound = 0;
    std::string acc;

    while (pos > 0 && linesFound <= maxLines)
    {
      const std::streamoff readSize = std::min<std::streamoff>(chunkSize, pos);
      pos -= readSize;

      ifs.seekg(pos, std::ios::beg);
      buffer.assign(static_cast<std::size_t>(readSize), '\0');
      ifs.read(&buffer[0], readSize);
      if (!ifs)
        break;

      acc.insert(0, buffer);

      linesFound = 0;
      for (char c : acc)
        if (c == '\n')
          ++linesFound;

      if (linesFound > maxLines)
        break;
    }

    std::vector<std::string> lines;
    {
      std::istringstream is(acc);
      std::string line;
      while (std::getline(is, line))
        lines.push_back(line);
    }

    const std::size_t start = (lines.size() > maxLines) ? (lines.size() - maxLines) : 0;

    std::cerr << "\n--- " << logPath.string() << " (last "
              << (lines.size() - start) << " lines) ---\n";
    for (std::size_t i = start; i < lines.size(); ++i)
      std::cerr << lines[i] << "\n";
    std::cerr << "--- end ---\n\n";
  }

#ifdef _WIN32

  bool executable_on_path(const std::string &exeName)
  {
    char buf[MAX_PATH];
    DWORD n = ::SearchPathA(nullptr, exeName.c_str(), nullptr,
                            static_cast<DWORD>(sizeof(buf)), buf, nullptr);
    return n != 0 && n < sizeof(buf);
  }

#else

  static bool is_executable_file(const fs::path &p)
  {
    struct stat sb{};
    if (::stat(p.c_str(), &sb) != 0)
      return false;

    if (!S_ISREG(sb.st_mode))
      return false;

    return (sb.st_mode & S_IXUSR) || (sb.st_mode & S_IXGRP) || (sb.st_mode & S_IXOTH);
  }

  bool executable_on_path(const std::string &exeName)
  {
    const char *pathEnv = vix::utils::vix_getenv("PATH");
    if (!pathEnv)
      return false;

    std::string_view pathStr(pathEnv);
    std::size_t start = 0;

    while (start <= pathStr.size())
    {
      std::size_t end = pathStr.find(':', start);
      if (end == std::string_view::npos)
        end = pathStr.size();

      std::string_view dir = pathStr.substr(start, end - start);
      if (!dir.empty())
      {
        fs::path candidate = fs::path(std::string(dir)) / exeName;
        if (is_executable_file(candidate))
          return true;
      }

      start = end + 1;
    }

    return false;
  }

#endif

} // namespace vix::cli::util
