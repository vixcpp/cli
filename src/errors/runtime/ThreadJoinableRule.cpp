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
      bool isJThread = false;
    };

    std::vector<ThreadDecl> find_thread_declarations(
        const std::vector<std::string> &lines)
    {
      std::vector<ThreadDecl> decls;

      const std::regex re(
          R"(\bstd::(jthread|thread)\s+([A-Za-z_]\w*))");

      for (std::size_t i = 0; i < lines.size(); ++i)
      {
        const std::string code = strip_line_comment(lines[i]);
        std::smatch match;

        if (!std::regex_search(code, match, re))
          continue;

        if (match.size() < 3)
          continue;

        ThreadDecl decl;
        decl.isJThread = match[1].str() == "jthread";
        decl.name = match[2].str();
        decl.line = static_cast<int>(i + 1);
        decl.column = static_cast<int>(match.position(2) + 1);

        decls.push_back(decl);
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

      for (std::size_t i = static_cast<std::size_t>(startLine - 1);
           i < lines.size();
           ++i)
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
      if (sourceFile.empty())
        return std::nullopt;

      const auto linesOpt = read_file_lines(sourceFile);
      if (!linesOpt)
        return std::nullopt;

      const auto &lines = *linesOpt;
      const auto decls = find_thread_declarations(lines);

      if (decls.empty())
        return std::nullopt;

      for (const auto &decl : decls)
      {
        if (decl.isJThread)
          continue;

        if (!has_join_or_detach_after(lines, decl.name, decl.line))
          return decl;
      }

      return std::nullopt;
    }

    std::string make_thread_hint(const ThreadDecl &decl)
    {
      return "call " + decl.name + ".join() or " + decl.name + ".detach() before leaving the scope";
    }

    std::vector<std::string> source_patterns_for_joinable_thread()
    {
      return {
          "std::thread",
          "thread(",
          "std::jthread",
          "jthread(",
          ".join(",
          ".join()",
          ".detach(",
          ".detach()",
          "joinable(",
          ".joinable(",
          ".joinable()",
      };
    }

    bool looks_like_joinable_thread_log(const std::string &log)
    {
      const bool terminateWithoutException =
          icontains(log, "terminate called without an active exception") ||
          icontains(log, "terminate called without active exception");

      const bool abortSignal =
          icontains(log, "aborted") ||
          icontains(log, "sigabrt") ||
          icontains(log, "core dumped") ||
          icontains(log, "signal 6");

      const bool threadSignal =
          icontains(log, "std::thread") ||
          icontains(log, "thread::~thread") ||
          icontains(log, "~thread") ||
          icontains(log, "joinable");

      return terminateWithoutException &&
             (abortSignal || threadSignal);
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
      return looks_like_joinable_thread_log(log);
    }

    bool handle(
        const std::string &log,
        const std::filesystem::path &sourceFile) const override
    {
      const std::string message = "joinable std::thread destroyed";

      std::cerr << RED
                << "runtime error: "
                << message
                << RESET << "\n";

      const auto suspect = find_suspected_unjoined_thread(sourceFile);

      if (suspect)
      {
        const auto err = make_runtime_location(
            sourceFile,
            suspect->line,
            suspect->column,
            "thread '" + suspect->name + "' may leave scope without join() or detach()");

        print_runtime_codeframe(err);

        print_runtime_hints_and_at(
            {
                make_thread_hint(*suspect),
                "prefer std::jthread for RAII thread shutdown when possible",
            },
            sourceFile.string() + ":" + std::to_string(suspect->line));

        print_runtime_log_excerpt(log, 18);

        return true;
      }

      RuntimeLocation location =
          find_best_runtime_location(log, sourceFile);

      if (!location.valid())
      {
        location =
            find_best_runtime_location_or_source_hint(
                log,
                sourceFile,
                source_patterns_for_joinable_thread());
      }

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
              "join or detach every started std::thread before it leaves scope",
              "prefer std::jthread for RAII thread shutdown when possible",
              "do not ignore the runtime log: std::thread destruction calls std::terminate when the thread is still joinable",
          },
          make_at_text(location, sourceFile));

      print_runtime_log_excerpt(log, 18);

      return true;
    }
  };

  std::unique_ptr<IRuntimeErrorRule> makeThreadJoinableRule()
  {
    return std::make_unique<ThreadJoinableRule>();
  }
} // namespace vix::cli::errors::runtime
