/**
 *
 *  @file FilesystemRuntimeRule.cpp
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
#include <vix/cli/errors/runtime/IRuntimeErrorRule.hpp>
#include <vix/cli/errors/runtime/RuntimeRuleUtils.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    enum class FilesystemKind
    {
      FileNotFound,
      NotADirectory,
      IsADirectory,
      FileAlreadyExists,
      TooManySymlinks,
      InvalidPath,
      GenericFilesystemError,
    };

    FilesystemKind classify_issue(const std::string &log)
    {
      if (icontains(log, "No such file or directory"))
        return FilesystemKind::FileNotFound;

      if (icontains(log, "not a directory"))
        return FilesystemKind::NotADirectory;

      if (icontains(log, "is a directory"))
        return FilesystemKind::IsADirectory;

      if (icontains(log, "file exists"))
        return FilesystemKind::FileAlreadyExists;

      if (icontains(log, "too many symbolic links"))
        return FilesystemKind::TooManySymlinks;

      if (icontains(log, "invalid path"))
        return FilesystemKind::InvalidPath;

      return FilesystemKind::GenericFilesystemError;
    }

    std::string choose_message(const std::string &log)
    {
      switch (classify_issue(log))
      {
      case FilesystemKind::FileNotFound:
        return "file not found";

      case FilesystemKind::NotADirectory:
        return "path is not a directory";

      case FilesystemKind::FileAlreadyExists:
        return "path already exists";

      case FilesystemKind::IsADirectory:
      case FilesystemKind::TooManySymlinks:
      case FilesystemKind::InvalidPath:
      case FilesystemKind::GenericFilesystemError:
      default:
        return "filesystem error";
      }
    }

    std::string choose_hint(const std::string &log)
    {
      (void)log;
      return "check the path, working directory, permissions, and parent directories";
    }

    std::vector<std::string> source_patterns_for_filesystem()
    {
      return {
          "std::filesystem",
          "create_directories",
          "create_directory",
          "current_path(",
          "exists(",
          "remove(",
          "rename(",
          "copy(",
          "ifstream",
          "ofstream",
      };
    }

    bool looks_like_filesystem_log(const std::string &log)
    {
      // Skip cases handled by more specific rules
      if (icontains(log, "Permission denied") ||
          icontains(log, "EACCES") ||
          icontains(log, "EPERM"))
      {
        return false;
      }

      return icontains(log, "std::filesystem::filesystem_error") ||
             icontains(log, "filesystem error") ||
             icontains(log, "No such file or directory") ||
             icontains(log, "not a directory") ||
             icontains(log, "is a directory") ||
             icontains(log, "file exists") ||
             icontains(log, "too many symbolic links") ||
             icontains(log, "invalid path");
    }
  } // namespace

  class FilesystemRuntimeRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;
      return looks_like_filesystem_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = choose_message(log);

      RuntimeLocation location = find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location = find_best_runtime_location_or_source_hint(
            log,
            sourceFile,
            source_patterns_for_filesystem());
      }

      std::cerr << RED
                << "runtime error: "
                << message
                << RESET << "\n";

      if (location.valid())
      {
        const auto err = make_runtime_location(
            location.file,
            location.line,
            location.column,
            message);

        print_runtime_codeframe(err);
      }

      print_runtime_hints_and_at(
          {
              choose_hint(log),
              "the runtime log below contains the exact path reported by the OS",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 20);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeFilesystemRuntimeRule()
  {
    return std::make_unique<FilesystemRuntimeRule>();
  }
} // namespace vix::cli::errors::runtime
