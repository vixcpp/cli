/**
 *
 *  @file ThreadJoinableRule.cpp
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
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <vix/cli/Style.hpp>

using namespace vix::cli::style;

namespace vix::cli::errors::runtime
{
  namespace
  {
    struct ThreadDecl
    {
      std::string name;
      int line = 1;
      int column = 1;
    };

    std::vector<ThreadDecl> find_thread_declarations(const std::vector<std::string> &lines)
    {
      std::vector<ThreadDecl> decls;

      const std::regex re(R"(\bstd::thread\s+([A-Za-z_]\w*))");

      for (std::size_t i = 0; i < lines.size(); ++i)
      {
        const std::string code = strip_line_comment(lines[i]);
        std::smatch m;

        if (std::regex_search(code, m, re))
        {
          ThreadDecl d;
          d.name = m[1].str();
          d.line = static_cast<int>(i + 1);
          d.column = static_cast<int>(m.position(1) + 1);
          decls.push_back(d);
        }
      }

      return decls;
    }

    bool has_join_or_detach_after(
        const std::vector<std::string> &lines,
        const std::string &threadName,
        int startLine)
    {
      const std::regex re(
          "\\b" + threadName + R"(\s*\.\s*(join|detach)\s*\()");

      for (std::size_t i = static_cast<std::size_t>(startLine - 1); i < lines.size(); ++i)
      {
        const std::string code = strip_line_comment(lines[i]);

        if (std::regex_search(code, re))
          return true;
      }

      return false;
    }

    std::optional<ThreadDecl> find_suspected_unjoined_thread(
        const std::filesystem::path &sourceFile)
    {
      const auto linesOpt = read_file_lines(sourceFile);
      if (!linesOpt)
        return std::nullopt;

      const auto &lines = *linesOpt;
      const auto decls = find_thread_declarations(lines);

      if (decls.empty())
        return std::nullopt;

      for (const auto &decl : decls)
      {
        if (!has_join_or_detach_after(lines, decl.name, decl.line))
          return decl;
      }

      return std::nullopt;
    }
  } // namespace

  class ThreadJoinableRule final : public IRuntimeErrorRule
  {
  public:
    bool match(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)sourceFile;

      const bool hasTerminateWithoutException =
          icontains(log, "terminate called without an active exception");

      const bool hasAbort =
          icontains(log, "aborted") ||
          icontains(log, "sigabrt") ||
          icontains(log, "core dumped");

      return hasTerminateWithoutException && hasAbort;
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      (void)log;

      std::cerr << RED
                << "runtime error: joinable std::thread destroyed"
                << RESET << "\n";

      const auto suspect = sourceFile.empty()
                               ? std::nullopt
                               : find_suspected_unjoined_thread(sourceFile);

      if (suspect && !sourceFile.empty())
      {
        const auto err = make_runtime_location(
            sourceFile,
            suspect->line,
            suspect->column,
            "thread '" + suspect->name + "' may leave scope without join() or detach()");

        print_runtime_codeframe(err);

        print_runtime_hints_and_at(
            {
                "thread '" + suspect->name + "' likely reaches its destructor without join() or detach()",
                "call " + suspect->name + ".join() or " + suspect->name + ".detach() before leaving the scope",
            },
            sourceFile.filename().string() + ":" +
                std::to_string(suspect->line) + ":" +
                std::to_string(suspect->column));

        return true;
      }

      print_runtime_hints_and_at(
          {
              "a std::thread likely reached its destructor without join() or detach()",
              "join or detach every started thread before leaving the scope",
          },
          !sourceFile.empty() ? ("source: " + sourceFile.filename().string()) : "");

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeThreadJoinableRule()
  {
    return std::make_unique<ThreadJoinableRule>();
  }
} // namespace vix::cli::errors::runtime
