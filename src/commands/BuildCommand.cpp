/**
 *
 *  @file BuildCommand.cpp
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
#include <vix/cli/ErrorHandler.hpp>
#include <vix/cli/Style.hpp>
#include <vix/cli/cache/ArtifactCache.hpp>
#include <vix/cli/commands/BuildCommand.hpp>
#include <vix/cli/build/BuildGraph.hpp>
#include <vix/cli/build/BuildScheduler.hpp>
#include <vix/cli/build/ObjectCache.hpp>
#include <vix/cli/build/BuildGraphExecutor.hpp>
#include <vix/cli/build/BuildStyle.hpp>
#include <vix/cli/build/BuildContext.hpp>
#include <vix/cli/app/AppManifest.hpp>
#include <vix/cli/app/AppCMakeGenerator.hpp>
#include <vix/cli/app/AppProjectResolver.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <vix/cli/cmake/CMakeBuild.hpp>
#include <vix/cli/cmake/GlobalPackages.hpp>
#include <vix/cli/cmake/Toolchain.hpp>
#include <vix/cli/util/Args.hpp>
#include <vix/cli/util/Console.hpp>
#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Hash.hpp>
#include <vix/cli/util/Strings.hpp>
#include <vix/cli/util/Ui.hpp>

#include <vix/cli/commands/run/detail/ScriptProbe.hpp>
#include <vix/cli/commands/run/detail/DirectScriptRunner.hpp>
#include <vix/cli/commands/run/detail/ScriptCMake.hpp>
#include <vix/cli/commands/run/RunDetail.hpp>

namespace fs = std::filesystem;
using namespace vix::cli::style;
namespace process = vix::cli::process;
namespace util = vix::cli::util;
namespace build = vix::cli::build;
namespace artifact_cache = vix::cli::cache;
namespace run_detail = vix::commands::RunCommand::detail;
namespace app = vix::cli::app;

namespace vix::commands::BuildCommand
{
  namespace
  {
    static constexpr std::uint64_t LOCAL_FNV_OFFSET = 1469598103934665603ull;

    static std::string platform_executable_name(const std::string &name);

    static void write_last_binary(const fs::path &path);

    static std::optional<fs::path> resolve_main_executable(
        const fs::path &buildDir,
        const fs::path &projectDir,
        const std::string &buildTarget,
        const std::string &defaultTargetName);

    struct DeferredConsole
    {
      bool enabled{false};
      std::ostringstream buf;

      explicit DeferredConsole(bool on) : enabled(on) {}

      void print(const std::string &s)
      {
        if (enabled)
          buf << s;
        else
          std::cout << s;
      }

      void flush_to_stdout()
      {
        if (!enabled)
          return;
        std::cout << buf.str();
        buf.str("");
        buf.clear();
      }

      void discard()
      {
        buf.str("");
        buf.clear();
      }
    };

    static bool write_if_different(const fs::path &path, const std::string &content)
    {
      if (util::file_exists(path))
      {
        const std::string current = util::read_text_file_or_empty(path);
        if (current == content)
          return true;
      }

      return util::write_text_file_atomic(path, content);
    }

    static bool graph_executor_enabled()
    {
      const char *value = std::getenv("VIX_GRAPH_EXECUTOR");

      if (!value || !*value)
        return true;

      std::string s(value);

      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return !(s == "0" ||
               s == "false" ||
               s == "no" ||
               s == "off");
    }

    static bool is_all_build_target(const std::string &target)
    {
      return target.empty() || target == "all";
    }

    static bool can_use_target_graph_executor(
        const process::Options &opt,
        const std::size_t importedCompileCommands,
        const std::size_t importedNinjaTasks)
    {
      if (!graph_executor_enabled())
        return false;

      if (!opt.useCache)
        return false;

      if (opt.clean)
        return false;

      if (is_all_build_target(opt.buildTarget))
        return false;

      if (!opt.targetTriple.empty())
        return false;

      if (!opt.cmakeArgs.empty())
        return false;

      if (opt.withSqlite || opt.withMySql)
        return false;

      if (opt.linkStatic)
        return false;

      if (importedCompileCommands == 0)
        return false;

      if (importedNinjaTasks == 0)
        return false;

      return true;
    }

    static std::size_t count_built_targets_from_log(const std::string &log)
    {
      std::size_t count = 0;
      std::istringstream iss(log);
      std::string line;

      while (std::getline(iss, line))
      {
        line = util::trim(line);

        if (line.empty())
          continue;

        if (line.find(" Linking CXX executable ") != std::string::npos ||
            line.find(" Linking C executable ") != std::string::npos ||
            line.find(" Linking CXX static library ") != std::string::npos ||
            line.find(" Linking C static library ") != std::string::npos ||
            line.find(" Linking CXX shared library ") != std::string::npos ||
            line.find(" Linking C shared library ") != std::string::npos ||
            line.find(" Built target ") != std::string::npos)
        {
          ++count;
        }
      }

      return count;
    }

    static std::string sanitize_cache_component(std::string s)
    {
      for (char &c : s)
      {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '.' || c == '_' || c == '-' || c == '+'))
          c = '_';
      }

      if (s.empty())
        return "unknown";

      return s;
    }

    static std::string detect_native_target_triple()
    {
#if defined(__x86_64__) && defined(__linux__)
      return "x86_64-linux-gnu";
#elif defined(__aarch64__) && defined(__linux__)
      return "aarch64-linux-gnu";
#elif defined(__arm__) && defined(__linux__)
      return "arm-linux-gnueabihf";
#elif defined(__riscv) && (__riscv_xlen == 64) && defined(__linux__)
      return "riscv64-linux-gnu";
#elif defined(_WIN32) && defined(_M_X64)
      return "x86_64-windows-msvc";
#elif defined(_WIN32) && defined(_M_ARM64)
      return "aarch64-windows-msvc";
#elif defined(__APPLE__) && defined(__aarch64__)
      return "aarch64-apple-darwin";
#elif defined(__APPLE__) && defined(__x86_64__)
      return "x86_64-apple-darwin";
#else
      return "unknown-target";
#endif
    }

    static std::string detect_compiler_identity()
    {
#ifdef __clang__
      return sanitize_cache_component(
          "clang++-" + std::to_string(__clang_major__) + "." +
          std::to_string(__clang_minor__) + "." +
          std::to_string(__clang_patchlevel__));
#elif defined(__GNUC__)
      return sanitize_cache_component(
          "g++-" + std::to_string(__GNUC__) + "." +
          std::to_string(__GNUC_MINOR__) + "." +
          std::to_string(__GNUC_PATCHLEVEL__));
#elif defined(_MSC_VER)
      return sanitize_cache_component("msvc-" + std::to_string(_MSC_VER));
#else
      return "unknown-compiler";
#endif
    }

    static std::string make_artifact_fingerprint(
        const process::Plan &plan,
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      std::ostringstream oss;

      oss << "project=" << plan.projectDir.string() << "\n";
      oss << "preset=" << plan.preset.name << "\n";
      oss << "buildType=" << plan.preset.buildType << "\n";
      oss << "buildTarget=" << opt.buildTarget << "\n";
      oss << "defaultTargetName=" << plan.defaultTargetName << "\n";
      oss << "projectFingerprint=" << plan.projectFingerprint << "\n";
      oss << "targetTriple=" << opt.targetTriple << "\n";
      oss << "sysroot=" << opt.sysroot << "\n";
      oss << "static=" << (opt.linkStatic ? "1" : "0") << "\n";
      oss << "linker=" << static_cast<int>(opt.linker) << "\n";
      oss << "launcher=" << static_cast<int>(opt.launcher) << "\n";

      if (plan.fastLinkerFlag)
        oss << "fastLinkerFlag=" << *plan.fastLinkerFlag << "\n";

      if (plan.launcher)
        oss << "launcherTool=" << *plan.launcher << "\n";

      oss << "vars:\n";
      oss << util::signature_join(plan.cmakeVars);

      oss << "rawCMakeArgs:\n";
      for (const auto &arg : opt.cmakeArgs)
        oss << arg << "\n";

      if (!toolchainContent.empty())
      {
        oss << "toolchain:\n";
        oss << toolchainContent;

        if (toolchainContent.back() != '\n')
          oss << "\n";
      }

      const std::string payload = oss.str();
      const std::uint64_t h = util::fnv1a64_str(payload, LOCAL_FNV_OFFSET);

      return util::hex64(h);
    }

    static std::string make_object_cache_build_fingerprint(
        const process::Plan &plan,
        const process::Options &opt)
    {
      std::ostringstream oss;

      oss << "signature=" << plan.signature << "\n";
      oss << "preset=" << plan.preset.name << "\n";
      oss << "buildType=" << plan.preset.buildType << "\n";
      oss << "targetTriple="
          << (opt.targetTriple.empty() ? detect_native_target_triple() : opt.targetTriple)
          << "\n";
      oss << "compiler=" << detect_compiler_identity() << "\n";
      oss << "linker=" << static_cast<int>(opt.linker) << "\n";
      oss << "launcher=" << static_cast<int>(opt.launcher) << "\n";

      if (plan.launcher)
        oss << "launcherTool=" << *plan.launcher << "\n";

      if (plan.fastLinkerFlag)
        oss << "fastLinkerFlag=" << *plan.fastLinkerFlag << "\n";

      oss << "cmakeVars:\n";
      oss << util::signature_join(plan.cmakeVars);

      oss << "rawCMakeArgs:\n";
      for (const auto &arg : opt.cmakeArgs)
        oss << arg << "\n";

      const std::string payload = oss.str();
      const std::uint64_t h = util::fnv1a64_str(payload, LOCAL_FNV_OFFSET);

      return util::hex64(h);
    }

    static artifact_cache::Artifact make_project_artifact(
        const process::Plan &plan,
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      artifact_cache::Artifact a;
      a.package = sanitize_cache_component(
          !plan.defaultTargetName.empty()
              ? plan.defaultTargetName
              : plan.projectDir.filename().string());
      a.version = "local";
      a.target = sanitize_cache_component(
          opt.targetTriple.empty() ? detect_native_target_triple() : opt.targetTriple);
      a.compiler = detect_compiler_identity();
      a.buildType = sanitize_cache_component(plan.preset.buildType);
      a.fingerprint = make_artifact_fingerprint(plan, opt, toolchainContent);

      const fs::path root = artifact_cache::ArtifactCache::artifact_path(a);
      a.root = root;
      a.include = root / "include";
      a.lib = root / "lib";

      return a;
    }

    static bool persist_project_artifact(const artifact_cache::Artifact &a)
    {
      if (!artifact_cache::ArtifactCache::ensure_layout(a))
        return false;

      return artifact_cache::ArtifactCache::write_manifest(a);
    }

    static bool can_use_target_artifact_cache(const process::Options &opt)
    {
      if (!opt.useCache)
        return false;

      if (opt.explain)
        return false;

      if (opt.clean)
        return false;

      if (opt.exportBin || !opt.outPath.empty())
        return false;

      if (is_all_build_target(opt.buildTarget))
        return false;

      if (!opt.cmakeArgs.empty())
        return false;

      if (!opt.targetTriple.empty())
        return false;

      if (opt.withSqlite || opt.withMySql)
        return false;

      if (opt.linkStatic)
        return false;

      return true;
    }

    static fs::path artifact_target_binary_path(
        const artifact_cache::Artifact &artifact,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const std::string target =
          build::default_build_target_name(opt, plan);

      return artifact.root / "bin" / platform_executable_name(target);
    }

    static bool copy_executable_file(
        const fs::path &source,
        const fs::path &destination)
    {
      std::error_code ec;

      if (!fs::exists(source, ec) || ec)
        return false;

      if (!fs::is_regular_file(source, ec) || ec)
        return false;

      const fs::path parent = destination.parent_path();

      if (!parent.empty())
      {
        fs::create_directories(parent, ec);

        if (ec)
          return false;
      }

      fs::copy_file(
          source,
          destination,
          fs::copy_options::overwrite_existing,
          ec);

      if (ec)
        return false;

#ifndef _WIN32
      const auto perms = fs::status(source, ec).permissions();

      if (!ec)
      {
        fs::permissions(
            destination,
            perms,
            fs::perm_options::replace,
            ec);
      }
#endif

      return true;
    }

    static bool restore_project_target_artifact(
        const artifact_cache::Artifact &artifact,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const fs::path cachedBinary =
          artifact_target_binary_path(artifact, opt, plan);

      const fs::path destination =
          build::default_project_executable_path(opt, plan);

      if (!copy_executable_file(cachedBinary, destination))
        return false;

      if (!persist_project_artifact(artifact))
        return false;

      write_last_binary(destination);

      return true;
    }

    static bool store_project_target_artifact(
        const artifact_cache::Artifact &artifact,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const auto exeOpt = resolve_main_executable(
          plan.buildDir,
          plan.userProjectDir,
          opt.buildTarget,
          plan.defaultTargetName);

      if (!exeOpt)
        return false;

      if (!artifact_cache::ArtifactCache::ensure_layout(artifact))
        return false;

      const fs::path cachedBinary =
          artifact_target_binary_path(artifact, opt, plan);

      if (!copy_executable_file(*exeOpt, cachedBinary))
        return false;

      return artifact_cache::ArtifactCache::write_manifest(artifact);
    }

    static process::Options parse_args_or_exit(
        const std::vector<std::string> &args,
        int &exitCode)
    {
      process::Options o;
      exitCode = 0;

      for (std::size_t i = 0; i < args.size(); ++i)
      {
        const std::string &a = args[i];

        if (a == "--")
        {
          for (std::size_t j = i + 1; j < args.size(); ++j)
            o.cmakeArgs.push_back(args[j]);
          break;
        }

        if (a == "--help" || a == "-h")
        {
          exitCode = -2;
          return o;
        }
        else if (a == "--verbose" || a == "-v")
        {
          o.verbose = true;
          o.cmakeVerbose = true;
        }
        else if (a == "--explain")
        {
          o.explain = true;
        }
        else if (a == "--warnings")
        {
          o.warnings = true;
        }
        else if (a == "--warning-check")
        {
          o.warningCheck = true;
        }
        else if (a == "--page")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --page <n>");
            exitCode = 2;
            return o;
          }

          try
          {
            const int page = std::stoi(std::string(*v));
            if (page <= 0)
              throw std::runtime_error("invalid page");

            o.warningsPage = static_cast<std::size_t>(page);
            o.warningsPageSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --page: " + std::string(*v));
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--page=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--page=").size());

          try
          {
            const int page = std::stoi(v);
            if (page <= 0)
              throw std::runtime_error("invalid page");

            o.warningsPage = static_cast<std::size_t>(page);
            o.warningsPageSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --page: " + v);
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--limit")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --limit <n>");
            exitCode = 2;
            return o;
          }

          try
          {
            const int limit = std::stoi(std::string(*v));
            if (limit <= 0)
              throw std::runtime_error("invalid limit");

            o.warningsLimit = static_cast<std::size_t>(limit);
            o.warningsLimitSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --limit: " + std::string(*v));
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--limit=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--limit=").size());

          try
          {
            const int limit = std::stoi(v);
            if (limit <= 0)
              throw std::runtime_error("invalid limit");

            o.warningsLimit = static_cast<std::size_t>(limit);
            o.warningsLimitSet = true;
          }
          catch (...)
          {
            error("Invalid integer for --limit: " + v);
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--preset")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --preset");
            exitCode = 2;
            return o;
          }
          o.preset = std::string(*v);
        }
        else if (a == "--with-sqlite")
        {
          o.withSqlite = true;
        }
        else if (a == "--with-mysql")
        {
          o.withMySql = true;
        }
        else if (!a.empty() && a[0] != '-')
        {
          if (o.singleCpp)
          {
            error("Only one single C++ source file can be passed to vix build.");
            exitCode = 2;
            return o;
          }

          fs::path candidate = fs::path(a);
          if (candidate.extension() == ".cpp" ||
              candidate.extension() == ".cc" ||
              candidate.extension() == ".cxx")
          {
            o.singleCpp = true;
            o.cppFile = fs::absolute(candidate);
          }
          else
          {
            error("Unknown positional argument: " + a);
            hint("For single-file mode, pass a .cpp file.");
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--preset=", 0) == 0)
        {
          o.preset = a.substr(std::string("--preset=").size());
          if (o.preset.empty())
          {
            error("Missing value for --preset");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--target")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --target <triple>");
            exitCode = 2;
            return o;
          }
          o.targetTriple = std::string(*v);
        }
        else if (a.rfind("--target=", 0) == 0)
        {
          o.targetTriple = a.substr(std::string("--target=").size());
          if (o.targetTriple.empty())
          {
            error("Missing value for --target <triple>");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--build-target")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --build-target <name>");
            exitCode = 2;
            return o;
          }
          o.buildTarget = std::string(*v);
        }
        else if (a.rfind("--build-target=", 0) == 0)
        {
          o.buildTarget = a.substr(std::string("--build-target=").size());
          if (o.buildTarget.empty())
          {
            error("Missing value for --build-target <name>");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--targets")
        {
          const auto targets = build::detect_available_targets();

          info("Detected build targets:");
          for (const auto &t : targets)
          {
            if (t == "x86_64-linux-gnu")
              step(t + " (native)");
            else
              step(t + " (cross)");
          }

          exitCode = -1;
          return o;
        }
        else if (a == "--cmake-verbose")
        {
          o.cmakeVerbose = true;
        }
        else if (a == "--sysroot")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --sysroot <path>");
            exitCode = 2;
            return o;
          }
          o.sysroot = std::string(*v);
        }
        else if (a.rfind("--sysroot=", 0) == 0)
        {
          o.sysroot = a.substr(std::string("--sysroot=").size());
        }
        else if (a == "--static")
        {
          o.linkStatic = true;
        }
        else if (a == "-j" || a == "--jobs")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for -j/--jobs");
            exitCode = 2;
            return o;
          }

          try
          {
            o.jobs = std::stoi(std::string(*v));
          }
          catch (...)
          {
            error("Invalid integer for -j/--jobs: " + std::string(*v));
            exitCode = 2;
            return o;
          }
        }
        else if (a.rfind("--jobs=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--jobs=").size());
          try
          {
            o.jobs = std::stoi(v);
          }
          catch (...)
          {
            error("Invalid integer for --jobs: " + v);
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--bin")
        {
          o.exportBin = true;
        }
        else if (a == "--out")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --out <path>");
            exitCode = 2;
            return o;
          }
          o.outPath = std::string(*v);
        }
        else if (a.rfind("--out=", 0) == 0)
        {
          o.outPath = a.substr(std::string("--out=").size());
          if (o.outPath.empty())
          {
            error("Missing value for --out <path>");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--clean")
        {
          o.clean = true;
        }
        else if (a == "--quiet" || a == "-q")
        {
          o.quiet = true;
        }
        else if (a == "--dir" || a == "-d")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --dir <path>");
            exitCode = 2;
            return o;
          }
          o.dir = std::string(*v);
        }
        else if (a.rfind("--dir=", 0) == 0)
        {
          o.dir = a.substr(std::string("--dir=").size());
          if (o.dir.empty())
          {
            error("Missing value for --dir <path>");
            exitCode = 2;
            return o;
          }
        }
        else if (a == "--fast")
        {
          o.fast = true;
        }
        else if (a == "--no-cache")
        {
          o.useCache = false;
        }
        else if (a == "--linker")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --linker <auto|default|mold|lld>");
            exitCode = 2;
            return o;
          }

          const auto parsed = util::parse_linker_mode(*v);
          if (!parsed)
          {
            error("Invalid value for --linker: " + std::string(*v));
            hint("Valid: auto, default, mold, lld");
            exitCode = 2;
            return o;
          }
          o.linker = *parsed;
        }
        else if (a.rfind("--linker=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--linker=").size());
          const auto parsed = util::parse_linker_mode(v);
          if (!parsed)
          {
            error("Invalid value for --linker: " + v);
            hint("Valid: auto, default, mold, lld");
            exitCode = 2;
            return o;
          }
          o.linker = *parsed;
        }
        else if (a == "--launcher")
        {
          auto v = util::take_value(args, i);
          if (!v)
          {
            error("Missing value for --launcher <auto|none|sccache|ccache>");
            exitCode = 2;
            return o;
          }

          const auto parsed = util::parse_launcher_mode(*v);
          if (!parsed)
          {
            error("Invalid value for --launcher: " + std::string(*v));
            hint("Valid: auto, none, sccache, ccache");
            exitCode = 2;
            return o;
          }
          o.launcher = *parsed;
        }
        else if (a.rfind("--launcher=", 0) == 0)
        {
          const std::string v = a.substr(std::string("--launcher=").size());
          const auto parsed = util::parse_launcher_mode(v);
          if (!parsed)
          {
            error("Invalid value for --launcher: " + v);
            hint("Valid: auto, none, sccache, ccache");
            exitCode = 2;
            return o;
          }
          o.launcher = *parsed;
        }
        else if (a == "--no-status")
        {
          o.status = false;
        }
        else if (a == "--no-up-to-date")
        {
          o.dryUpToDate = false;
        }
        else
        {
          error("Unknown argument: " + a);
          hint("Run: vix build --help");
          exitCode = 2;
          return o;
        }
      }

      if ((o.warningsPageSet || o.warningsLimitSet) && !o.warnings)
      {
        error("--page and --limit can only be used with --warnings");
        hint("Try: vix build --warnings --page 2 --limit 10");
        exitCode = 2;
        return o;
      }

      return o;
    }

    static std::optional<std::string> detect_launcher(const process::Options &opt)
    {
      switch (opt.launcher)
      {
      case process::LauncherMode::None:
        return std::nullopt;
      case process::LauncherMode::Sccache:
        return util::executable_on_path("sccache")
                   ? std::optional<std::string>("sccache")
                   : std::nullopt;
      case process::LauncherMode::Ccache:
        return util::executable_on_path("ccache")
                   ? std::optional<std::string>("ccache")
                   : std::nullopt;
      case process::LauncherMode::Auto:
      default:
        if (util::executable_on_path("sccache"))
          return std::optional<std::string>("sccache");
        if (util::executable_on_path("ccache"))
          return std::optional<std::string>("ccache");
        return std::nullopt;
      }
    }

    static std::optional<std::string> detect_fast_linker_flag(const process::Options &opt)
    {
#ifdef _WIN32
      (void)opt;
      return std::nullopt;
#else
      const bool has_mold = util::executable_on_path("mold");
      const bool has_ld_lld = util::executable_on_path("ld.lld");

      if (opt.linker == process::LinkerMode::Default)
        return std::nullopt;

      if (opt.linker == process::LinkerMode::Mold)
        return has_mold ? std::optional<std::string>("-fuse-ld=mold")
                        : std::nullopt;

      if (opt.linker == process::LinkerMode::Lld)
        return has_ld_lld ? std::optional<std::string>("-fuse-ld=lld")
                          : std::nullopt;

      if (has_mold)
        return std::optional<std::string>("-fuse-ld=mold");
      if (has_ld_lld)
        return std::optional<std::string>("-fuse-ld=lld");

      return std::nullopt;
#endif
    }

    static std::string warning_check_cxx_compiler()
    {
      const char *cxx = std::getenv("CXX");

      if (cxx && *cxx)
        return cxx;

      if (util::executable_on_path("clang++"))
        return "clang++";

      if (util::executable_on_path("g++"))
        return "g++";

      return "c++";
    }

    static std::optional<std::string> warning_check_c_compiler(
        const std::string &cxxCompiler)
    {
      const char *cc = std::getenv("CC");

      if (cc && *cc)
        return std::string(cc);

      if (cxxCompiler == "clang++" && util::executable_on_path("clang"))
        return std::string("clang");

      if (cxxCompiler == "g++" && util::executable_on_path("gcc"))
        return std::string("gcc");

      return std::nullopt;
    }

    static bool warning_check_uses_clang(const std::string &compiler)
    {
      return compiler.find("clang") != std::string::npos;
    }

    static std::string warning_check_cxx_flags(
        const std::string &compiler)
    {
      std::string flags =
          "-Wall "
          "-Wextra "
          "-Wpedantic "
          "-Wshadow "
          "-Wconversion "
          "-Wsign-conversion "
          "-Wformat=2 "
          "-Wold-style-cast "
          "-Woverloaded-virtual";

      if (warning_check_uses_clang(compiler))
      {
        flags +=
            " -Wlogical-op-parentheses"
            " -Wunreachable-code";
      }

      return flags;
    }

    static bool has_cmake_cache(const fs::path &buildDir)
    {
      return util::file_exists(buildDir / "CMakeCache.txt");
    }

    static void clean_local_build_dirs(
        const fs::path &projectDir,
        const std::string &targetTriple,
        bool quiet)
    {
      std::vector<fs::path> dirs = {
          projectDir / "build-dev",
          projectDir / "build-ninja",
          projectDir / "build-release"};

      if (!targetTriple.empty())
      {
        dirs.push_back(projectDir / ("build-dev-" + targetTriple));
        dirs.push_back(projectDir / ("build-ninja-" + targetTriple));
        dirs.push_back(projectDir / ("build-release-" + targetTriple));
      }

      for (const auto &dir : dirs)
      {
        if (!fs::exists(dir))
          continue;

        std::error_code ec;
        fs::remove_all(dir, ec);

        if (ec)
        {
          error("Failed to remove   build directory: " + dir.string());
          hint(ec.message());
          throw std::runtime_error("clean failed");
        }

        if (!quiet)
          step("removed " + dir.string());
      }
    }

    static std::vector<std::pair<std::string, std::string>>
    build_cmake_vars(
        const process::Preset &p,
        const process::Options &opt,
        const fs::path &toolchainFile,
        const std::optional<std::string> &launcher,
        const std::optional<std::string> &fastLinkerFlag,
        const fs::path &globalPackagesFile)
    {
      std::vector<std::pair<std::string, std::string>> vars;
      vars.reserve(32);

      vars.emplace_back("CMAKE_BUILD_TYPE", p.buildType);
      vars.emplace_back("CMAKE_EXPORT_COMPILE_COMMANDS", "ON");

#ifndef _WIN32
      if (util::file_exists("/usr/bin/ar"))
        vars.emplace_back("CMAKE_AR", "/usr/bin/ar");

      if (util::file_exists("/usr/bin/ranlib"))
        vars.emplace_back("CMAKE_RANLIB", "/usr/bin/ranlib");
#endif

      if (opt.warningCheck)
      {
        const std::string cxxCompiler = warning_check_cxx_compiler();
        const std::optional<std::string> cCompiler =
            warning_check_c_compiler(cxxCompiler);

        vars.emplace_back("VIX_ENABLE_WARNINGS", "ON");
        vars.emplace_back(
            "CMAKE_CXX_FLAGS",
            warning_check_cxx_flags(cxxCompiler));

        if (!std::getenv("CXX"))
          vars.emplace_back("CMAKE_CXX_COMPILER", cxxCompiler);

        if (!std::getenv("CC") && cCompiler)
          vars.emplace_back("CMAKE_C_COMPILER", *cCompiler);
      }

      if (!opt.targetTriple.empty())
        vars.emplace_back("CMAKE_TOOLCHAIN_FILE", toolchainFile.string());

      if (opt.linkStatic)
        vars.emplace_back("VIX_LINK_STATIC", "ON");

      if (!opt.targetTriple.empty())
        vars.emplace_back("VIX_TARGET_TRIPLE", opt.targetTriple);

      (void)globalPackagesFile;

      if (launcher && !launcher->empty())
      {
        vars.emplace_back("CMAKE_C_COMPILER_LAUNCHER", *launcher);
        vars.emplace_back("CMAKE_CXX_COMPILER_LAUNCHER", *launcher);
      }

      if (fastLinkerFlag && !fastLinkerFlag->empty())
      {
        vars.emplace_back("CMAKE_EXE_LINKER_FLAGS", *fastLinkerFlag);
        vars.emplace_back("CMAKE_SHARED_LINKER_FLAGS", *fastLinkerFlag);
        vars.emplace_back("CMAKE_MODULE_LINKER_FLAGS", *fastLinkerFlag);
      }

      if (opt.withSqlite)
      {
        vars.emplace_back("VIX_ENABLE_DB", "ON");
        vars.emplace_back("VIX_DB_USE_SQLITE", "ON");
        vars.emplace_back("VIX_DB_REQUIRE_SQLITE", "ON");
      }

      if (opt.withMySql)
      {
        vars.emplace_back("VIX_ENABLE_DB", "ON");
        vars.emplace_back("VIX_DB_USE_MYSQL", "ON");
        vars.emplace_back("VIX_DB_REQUIRE_MYSQL", "ON");
      }

      std::sort(
          vars.begin(),
          vars.end(),
          [](const auto &a, const auto &b)
          {
            return a.first < b.first;
          });

      return vars;
    }

    static std::string make_signature(
        const process::Plan &plan,
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      std::ostringstream oss;

      oss << "preset=" << plan.preset.name << "\n";
      oss << "generator=" << plan.preset.generator << "\n";
      oss << "buildType=" << plan.preset.buildType << "\n";
      oss << "static=" << (opt.linkStatic ? "1" : "0") << "\n";
      oss << "targetTriple=" << opt.targetTriple << "\n";
      oss << "sysroot=" << opt.sysroot << "\n";
      oss << "fast=" << (opt.fast ? "1" : "0") << "\n";
      oss << "useCache=" << (opt.useCache ? "1" : "0") << "\n";
      oss << "warningCheck=" << (opt.warningCheck ? "1" : "0") << "\n";
      oss << "linker=" << static_cast<int>(opt.linker) << "\n";
      oss << "launcher=" << static_cast<int>(opt.launcher) << "\n";
      oss << "verbose=" << (opt.verbose ? "1" : "0") << "\n";
      oss << "cmakeVerbose=" << (opt.cmakeVerbose ? "1" : "0") << "\n";

      if (plan.launcher)
        oss << "launcherTool=" << *plan.launcher << "\n";

      if (plan.fastLinkerFlag)
        oss << "linkerFlag=" << *plan.fastLinkerFlag << "\n";

      oss << "projectFingerprint=" << plan.projectFingerprint << "\n";

      oss << "vars:\n";
      oss << util::signature_join(plan.cmakeVars);

      oss << "rawCMakeArgs:\n";
      for (const auto &arg : opt.cmakeArgs)
        oss << arg << "\n";

      if (!opt.targetTriple.empty())
      {
        oss << "toolchain:\n";
        oss << toolchainContent;
        if (!toolchainContent.empty() && toolchainContent.back() != '\n')
          oss << "\n";
      }

      return util::trim(oss.str()) + "\n";
    }

    static std::optional<process::Plan> make_plan(
        const process::Options &opt,
        const fs::path &cwd)
    {
      fs::path base = cwd;

      if (!opt.dir.empty())
        base = fs::absolute(fs::path(opt.dir));

      fs::path userProjectDir;
      const auto root = util::find_project_root(base);

      if (root)
      {
        userProjectDir = *root;
      }
      else if (util::file_exists(base / "CMakeLists.txt") ||
               util::file_exists(base / "vix.app"))
      {
        userProjectDir = base;
      }
      else
      {
        return std::nullopt;
      }

      userProjectDir = fs::absolute(userProjectDir).lexically_normal();

      fs::path cmakeSourceDir = userProjectDir;
      std::string defaultTargetName = userProjectDir.filename().string();
      bool generatedFromVixApp = false;

      const fs::path cmakeListsPath = userProjectDir / "CMakeLists.txt";
      const fs::path appManifestPath = userProjectDir / "vix.app";

      const bool hasCMakeLists = util::file_exists(cmakeListsPath);
      const bool hasVixApp = util::file_exists(appManifestPath);

      if (!hasCMakeLists && hasVixApp)
      {
        const app::AppManifestLoadResult loadResult =
            app::load_app_manifest(appManifestPath);

        if (!loadResult.success())
        {
          error("Failed to load vix.app.");
          hint(loadResult.error);
          return std::nullopt;
        }

        const app::AppCMakeGenerateResult generateResult =
            app::generate_app_cmake_project(
                loadResult.manifest,
                userProjectDir);

        if (!generateResult.success())
        {
          error("Failed to generate internal CMake project from vix.app.");
          hint(generateResult.error);
          return std::nullopt;
        }

        cmakeSourceDir = generateResult.sourceDir;
        defaultTargetName = loadResult.manifest.name;
        generatedFromVixApp = true;
      }

      const auto presetOpt = build::resolve_builtin_preset(opt.preset);

      if (!presetOpt)
        return std::nullopt;

      process::Plan plan;
      plan.userProjectDir = userProjectDir;
      plan.cmakeSourceDir = cmakeSourceDir;
      plan.projectDir = userProjectDir;
      plan.defaultTargetName = defaultTargetName;
      plan.generatedFromVixApp = generatedFromVixApp;
      plan.preset = *presetOpt;

      plan.launcher = detect_launcher(opt);
      plan.fastLinkerFlag = detect_fast_linker_flag(opt);
      plan.projectFingerprint =
          util::compute_cmake_config_fingerprint(plan.cmakeSourceDir);

      if (!opt.targetTriple.empty())
        plan.buildDir = userProjectDir / (plan.preset.buildDirName + "-" + opt.targetTriple);
      else
        plan.buildDir = userProjectDir / plan.preset.buildDirName;

      plan.configureLog = plan.buildDir / "configure.log";
      plan.buildLog = plan.buildDir / "build.log";
      plan.sigFile = plan.buildDir / ".vix-config.sig";
      plan.toolchainFile = plan.buildDir / "vix-toolchain.cmake";

      const fs::path globalPackagesFile = plan.buildDir / "vix-global-packages.cmake";

      std::string toolchainContent;

      if (!opt.targetTriple.empty())
      {
        toolchainContent =
            build::toolchain_contents_for_triple(opt.targetTriple, opt.sysroot);
      }

      plan.cmakeVars = build_cmake_vars(
          plan.preset,
          opt,
          plan.toolchainFile,
          plan.launcher,
          plan.fastLinkerFlag,
          globalPackagesFile);

      plan.signature = make_signature(plan, opt, toolchainContent);

      return plan;
    }
    static bool need_configure(const process::Options &opt, const process::Plan &plan)
    {
      if (!opt.useCache)
        return true;
      if (opt.clean)
        return true;
      if (!has_cmake_cache(plan.buildDir))
        return true;
      if (!util::signature_matches(plan.sigFile, plan.signature))
        return true;
      return false;
    }

    static std::string platform_executable_name(const std::string &name)
    {
#ifdef _WIN32
      if (name.size() >= 4 && name.substr(name.size() - 4) == ".exe")
        return name;
      return name + ".exe";
#else
      return name;
#endif
    }

    static bool is_executable_candidate(const fs::path &p)
    {
      std::error_code ec{};

      if (!fs::is_regular_file(p, ec) || ec)
        return false;

#ifdef _WIN32
      return p.extension() == ".exe";
#else
      const auto perms = fs::status(p, ec).permissions();
      if (ec)
        return false;

      using pr = fs::perms;
      return (perms & pr::owner_exec) != pr::none ||
             (perms & pr::group_exec) != pr::none ||
             (perms & pr::others_exec) != pr::none;
#endif
    }

    static bool looks_like_test_binary(const fs::path &p)
    {
      const std::string n = p.filename().string();
      return n.find("_test") != std::string::npos ||
             n.find("_tests") != std::string::npos ||
             n.rfind("test_", 0) == 0;
    }

    static std::optional<fs::path> resolve_main_executable(
        const fs::path &buildDir,
        const fs::path &projectDir,
        const std::string &buildTarget,
        const std::string &defaultTargetName)
    {
      const std::string preferredBase =
          !buildTarget.empty()
              ? buildTarget
              : (!defaultTargetName.empty()
                     ? defaultTargetName
                     : projectDir.filename().string());

      const std::string preferredName = platform_executable_name(preferredBase);

      const std::vector<fs::path> preferredPaths = {
          buildDir / preferredName,
          buildDir / "bin" / preferredName,
          buildDir / "src" / preferredName};

      for (const auto &p : preferredPaths)
      {
        if (is_executable_candidate(p) && !looks_like_test_binary(p))
          return p;
      }

      std::vector<fs::path> candidates;
      std::error_code ec{};

      for (auto it = fs::recursive_directory_iterator(
               buildDir,
               fs::directory_options::skip_permission_denied,
               ec);
           !ec && it != fs::recursive_directory_iterator();
           ++it)
      {
        const fs::path p = it->path();

        if (p.string().find("CMakeFiles") != std::string::npos)
          continue;

        if (!is_executable_candidate(p))
          continue;

        if (looks_like_test_binary(p))
          continue;

#ifdef _WIN32
        const std::string baseName = p.stem().string();
#else
        const std::string baseName = p.filename().string();
#endif

        if (baseName == preferredBase || p.filename().string() == preferredName)
          return p;

        candidates.push_back(p);
      }

      if (candidates.size() == 1)
        return candidates.front();

      return std::nullopt;
    }

    static fs::path vix_home_dir()
    {
#ifdef _WIN32
      const char *home = std::getenv("USERPROFILE");
#else
      const char *home = std::getenv("HOME");
#endif

      if (home && *home)
      {
        return fs::path(home) / ".vix";
      }

      return fs::current_path() / ".vix";
    }

    static std::string json_escape_string(const std::string &value)
    {
      std::string out;
      out.reserve(value.size());

      for (char c : value)
      {
        switch (c)
        {
        case '\\':
          out += "\\\\";
          break;
        case '"':
          out += "\\\"";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          out += c;
          break;
        }
      }

      return out;
    }

    static void write_last_binary(const fs::path &path)
    {
      const fs::path metaDir = vix_home_dir();
      const fs::path metaFile = metaDir / "meta.json";

      std::error_code ec;
      fs::create_directories(metaDir, ec);

      if (ec)
        return;

      std::ofstream out(metaFile);
      if (!out)
        return;

      out << "{\n";
      out << "  \"last_binary\": \""
          << json_escape_string(fs::absolute(path).string())
          << "\"\n";
      out << "}\n";
    }

    static bool export_built_binary(
        const fs::path &sourceExe,
        const fs::path &destination,
        bool quiet)
    {
      std::error_code ec{};

      fs::path finalDest = destination;

      if (fs::exists(destination, ec) && fs::is_directory(destination, ec))
        finalDest = destination / sourceExe.filename();

      const fs::path parent = finalDest.parent_path();
      if (!parent.empty())
      {
        fs::create_directories(parent, ec);
        if (ec)
        {
          error("Failed to create output directory: " + parent.string());
          hint(ec.message());
          return false;
        }
      }

      fs::copy_file(sourceExe, finalDest, fs::copy_options::overwrite_existing, ec);
      if (ec)
      {
        error("Failed to export binary to: " + finalDest.string());
        hint(ec.message());
        return false;
      }

#ifndef _WIN32
      const auto perms = fs::status(sourceExe, ec).permissions();
      if (!ec)
        fs::permissions(finalDest, perms, fs::perm_options::replace, ec);
#endif

      if (!quiet)
        success("Exported binary: " + finalDest.string());

      write_last_binary(finalDest);

      return true;
    }

    static bool can_use_graph_build(
        const process::Options &opt,
        const process::Plan &plan,
        const build::BuildGraphScanResult &scan)
    {
      (void)opt;
      (void)plan;
      (void)scan;

      return false;
    }

    static fs::path graph_output_binary_path(
        const process::Options &opt,
        const process::Plan &plan)
    {
      if (!opt.outPath.empty())
        return fs::absolute(fs::path(opt.outPath));

      const std::string target =
          !plan.defaultTargetName.empty()
              ? plan.defaultTargetName
              : plan.projectDir.filename().string();

      return plan.buildDir / platform_executable_name(target);
    }

    static bool file_exists_regular(const fs::path &path)
    {
      std::error_code ec;
      return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

    static bool collect_compile_task_paths(
        const build::BuildGraph &graph,
        const build::BuildTask &task,
        fs::path &sourcePath,
        fs::path &objectPath,
        std::vector<fs::path> &dependencyPaths)
    {
      sourcePath.clear();
      objectPath.clear();
      dependencyPaths.clear();

      for (const auto &inputId : task.inputs)
      {
        const build::BuildNode *node = graph.find_node(inputId);
        if (!node)
          continue;

        if (node->kind == build::BuildNodeKind::Source && sourcePath.empty())
        {
          sourcePath = node->path;
          continue;
        }

        if (node->kind == build::BuildNodeKind::Header ||
            node->kind == build::BuildNodeKind::Config)
        {
          dependencyPaths.push_back(node->path);
        }
      }

      for (const auto &outputId : task.outputs)
      {
        const build::BuildNode *node = graph.find_node(outputId);
        if (!node)
          continue;

        if (node->kind == build::BuildNodeKind::Object)
        {
          objectPath = node->path;
          break;
        }
      }

      return !sourcePath.empty() && !objectPath.empty();
    }

    static build::BuildTaskResult run_cached_graph_compile_task(
        const build::BuildGraph &graph,
        const build::ObjectCache &objectCache,
        build::BuildTask &task)
    {
      build::BuildTaskResult result;
      result.taskId = task.id;

      fs::path sourcePath;
      fs::path objectPath;
      std::vector<fs::path> dependencyPaths;

      if (!collect_compile_task_paths(
              graph,
              task,
              sourcePath,
              objectPath,
              dependencyPaths))
      {
        result.state = build::BuildTaskState::Failed;
        result.exitCode = 127;
        result.output = "Invalid graph compile task: " + task.id + "\n";
        return result;
      }

      const fs::path dependencyFilePath =
          build::dependency_file_for_object(objectPath);

      const build::ObjectCacheResult restored =
          objectCache.resolve_compile_task(
              task,
              sourcePath,
              dependencyPaths,
              objectPath,
              dependencyFilePath,
              graph.config().buildFingerprint);

      if (restored.hit)
      {
        result.state = build::BuildTaskState::Skipped;
        result.exitCode = 0;
        result.output = "cache hit: " + sourcePath.string() + "\n";
        return result;
      }

      result = build::BuildScheduler::execute_command_task(task);

      if (result.exitCode != 0)
        return result;

      const std::string inputHash =
          build::ObjectCache::compute_input_hash(sourcePath, dependencyPaths);

      const std::string objectKey =
          build::ObjectCache::compute_object_key(
              sourcePath,
              inputHash,
              task.commandHash,
              graph.config().buildFingerprint);

      (void)objectCache.store(
          objectKey,
          sourcePath,
          objectPath,
          dependencyFilePath,
          inputHash,
          task.commandHash);

      return result;
    }

    static bool debug_build_details_enabled()
    {
      const char *level = std::getenv("VIX_LOG_LEVEL");

      if (!level || !*level)
        return false;

      std::string value(level);

      for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

      return value == "debug" || value == "trace";
    }

    static void print_debug_command_if_enabled(
        const process::ExecResult &result)
    {
      if (!debug_build_details_enabled())
        return;

      if (result.displayCommand.empty())
        return;

      std::cerr << GRAY
                << "command: "
                << RESET
                << result.displayCommand
                << "\n";
    }

    static bool looks_like_compiler_warning(const std::string &line)
    {
      return line.find(": warning:") != std::string::npos ||
             line.find(" warning: ") != std::string::npos;
    }

    static std::vector<std::string> collect_compiler_warnings(
        const std::string &output,
        std::size_t maxWarnings = 5)
    {
      std::istringstream in(output);
      std::string line;

      std::vector<std::string> warnings;

      while (std::getline(in, line))
      {
        if (!looks_like_compiler_warning(line))
          continue;

        warnings.push_back(line);

        if (warnings.size() >= maxWarnings)
          break;
      }

      return warnings;
    }

    static std::size_t count_compiler_warnings(const std::string &output)
    {
      std::istringstream in(output);
      std::string line;

      std::size_t count = 0;

      while (std::getline(in, line))
      {
        if (looks_like_compiler_warning(line))
          ++count;
      }

      return count;
    }

    static void print_compiler_warnings_summary(const std::string &output)
    {
      const std::size_t total = count_compiler_warnings(output);

      if (total == 0)
        return;

      const std::vector<std::string> warningLines =
          collect_compiler_warnings(output, 5);

      std::vector<build::BuildWarning> warnings;
      warnings.reserve(warningLines.size());

      for (const std::string &line : warningLines)
      {
        const auto parsed = build::parse_build_warning(line);

        if (parsed)
        {
          warnings.push_back(*parsed);
          continue;
        }

        build::BuildWarning warning;
        warning.raw = line;
        warnings.push_back(warning);
      }

      build::print_build_warnings_summary(
          std::cout,
          warnings,
          total);
    }

    static std::vector<build::BuildWarning> collect_all_build_warnings(
        const std::string &output)
    {
      std::istringstream in(output);
      std::string line;

      std::vector<build::BuildWarning> warnings;

      while (std::getline(in, line))
      {
        if (!looks_like_compiler_warning(line))
          continue;

        const auto parsed = build::parse_build_warning(line);

        if (parsed)
        {
          warnings.push_back(*parsed);
          continue;
        }

        build::BuildWarning warning;
        warning.raw = line;
        warnings.push_back(warning);
      }

      return warnings;
    }

    static std::string display_build_profile(const process::Plan &plan)
    {
      if (plan.preset.buildType == "Release")
        return "release";

      return "dev";
    }

    static int print_last_build_warnings(
        const process::Options &opt,
        const process::Plan &plan)
    {
      const std::string log =
          util::read_text_file_or_empty(plan.buildLog);

      if (log.empty())
      {
        error("No build log found.");
        hint("Run `vix build` first.");
        return 1;
      }

      const std::vector<build::BuildWarning> warnings =
          collect_all_build_warnings(log);

      if (warnings.empty())
      {
        build::print_task_header_full(
            std::cout,
            "Warnings",
            build::default_build_target_name(opt, plan),
            display_build_profile(plan),
            {});

        build::print_build_success(
            std::cout,
            "No compiler warnings found");

        return 0;
      }

      build::print_task_header_full(
          std::cout,
          "Warnings",
          build::default_build_target_name(opt, plan),
          display_build_profile(plan),
          {});

      const std::size_t total = warnings.size();
      const std::size_t limit = opt.warningsLimit == 0 ? 10 : opt.warningsLimit;
      const std::size_t page = opt.warningsPage == 0 ? 1 : opt.warningsPage;

      const std::size_t start = (page - 1) * limit;

      if (start >= total)
      {
        error("Warnings page is out of range.");
        hint("Total warnings: " + std::to_string(total));
        hint("Last page: " + std::to_string((total + limit - 1) / limit));
        return 1;
      }

      const std::size_t end = std::min(start + limit, total);

      std::vector<build::BuildWarning> pageWarnings;
      pageWarnings.reserve(end - start);

      for (std::size_t i = start; i < end; ++i)
        pageWarnings.push_back(warnings[i]);

      build::print_build_warnings_summary(
          std::cout,
          pageWarnings,
          total);

      const std::size_t lastPage = (total + limit - 1) / limit;

      build::print_build_success(
          std::cout,
          "Listed warnings " +
              std::to_string(start + 1) +
              "-" +
              std::to_string(end) +
              " of " +
              std::to_string(total));

      if (page < lastPage)
      {
        hint("Next page: vix build --warnings --page " +
             std::to_string(page + 1) +
             " --limit " +
             std::to_string(limit));
      }
      return 0;
    }

    static void print_graph_warnings_modern(const std::string &output)
    {
      print_compiler_warnings_summary(output);
    }

    static std::string explain_display_path(const fs::path &path)
    {
      if (path.empty())
        return "<unknown>";

      return path.filename().string();
    }

    static bool graph_has_dirty_project_inputs_for_explain(
        const build::BuildGraph &graph)
    {
      for (const auto &kv : graph.nodes())
      {
        const build::BuildNode &node = kv.second;

        if (!(node.dirty() || node.missing()))
          continue;

        if (node.kind == build::BuildNodeKind::Source ||
            node.kind == build::BuildNodeKind::Header ||
            node.kind == build::BuildNodeKind::Config)
        {
          return true;
        }
      }

      return false;
    }

    static std::string explain_node_change(
        const build::BuildNode &current,
        const build::BuildNode *previous)
    {
      if (!previous)
        return "new input detected";

      if (current.state == build::BuildNodeState::Missing)
        return "input is missing";

      if (previous->state == build::BuildNodeState::Missing &&
          current.state != build::BuildNodeState::Missing)
      {
        return "input was restored";
      }

      if (current.hash != previous->hash)
        return "content hash changed";

      if (current.size != previous->size)
        return "file size changed";

      if (current.mtime != previous->mtime)
        return "file timestamp changed";

      if (current.state != previous->state)
        return "node state changed";

      return "";
    }

    static std::string explain_task_rebuild_reason(
        const build::BuildGraph &graph,
        const build::BuildGraph *previousGraph,
        const build::BuildTask &task)
    {
      if (!previousGraph)
        return "no previous build graph";

      const build::BuildTask *previousTask =
          previousGraph->find_task(task.id);

      if (!previousTask)
        return "new build task";

      if (task.commandHash != previousTask->commandHash)
        return "compiler command changed";

      for (const std::string &outputId : task.outputs)
      {
        const build::BuildNode *outputNode = graph.find_node(outputId);

        if (!outputNode)
          continue;

        if (!file_exists_regular(outputNode->path))
          return "output file is missing";
      }

      for (const std::string &inputId : task.inputs)
      {
        const build::BuildNode *currentNode = graph.find_node(inputId);

        if (!currentNode)
          continue;

        const build::BuildNode *previousNode =
            previousGraph->find_node(inputId);

        const std::string changeReason =
            explain_node_change(*currentNode, previousNode);

        if (changeReason.empty())
          continue;

        if (currentNode->kind == build::BuildNodeKind::Source)
          return "source file changed";

        if (currentNode->kind == build::BuildNodeKind::Header)
          return explain_display_path(currentNode->path) + " changed";

        if (currentNode->kind == build::BuildNodeKind::Config)
          return "build configuration changed";

        return changeReason;
      }

      return "dependency changed";
    }

    static void print_rebuild_explanation(
        const build::BuildGraph &graph,
        const build::BuildGraph *previousGraph,
        const process::Options &opt,
        const process::Plan &plan)
    {
      const std::vector<build::BuildTask> dirtyTasks =
          graph.dirty_compile_tasks();

      if (dirtyTasks.empty())
      {
        if (graph_has_dirty_project_inputs_for_explain(graph))
        {
          std::cout << "Project input changed\n";
          std::cout << "  reason: dependency changed, delegating target to Ninja\n\n";

          std::cout << "Relinking "
                    << build::default_build_target_name(opt, plan)
                    << "\n";

          std::cout << "  reason: target may depend on changed input\n\n";
          return;
        }

        std::cout << "No rebuild required\n";
        return;
      }

      bool printedAny = false;

      for (const build::BuildTask &task : dirtyTasks)
      {
        fs::path sourcePath;

        for (const std::string &inputId : task.inputs)
        {
          const build::BuildNode *node = graph.find_node(inputId);

          if (!node)
            continue;

          if (node->kind == build::BuildNodeKind::Source)
          {
            sourcePath = node->path;
            break;
          }
        }

        if (sourcePath.empty())
          continue;

        const std::string reason =
            explain_task_rebuild_reason(
                graph,
                previousGraph,
                task);

        std::cout << "Rebuilding "
                  << explain_display_path(sourcePath)
                  << "\n";

        std::cout << "  reason: "
                  << reason
                  << "\n\n";

        printedAny = true;
      }

      if (printedAny)
      {
        std::cout << "Relinking "
                  << build::default_build_target_name(opt, plan)
                  << "\n";

        std::cout << "  reason: object file changed\n\n";
      }
    }

    static build::BuildGraph make_build_graph_after_configure(
        const process::Options &opt,
        const process::Plan &plan,
        std::size_t &importedCompileCommands,
        std::size_t &importedNinjaTasks,
        build::BuildGraphScanResult &scan)
    {
      build::BuildGraphConfig graphConfig;
      graphConfig.projectDir = plan.userProjectDir;
      graphConfig.buildDir = plan.buildDir;
      graphConfig.objectDir = plan.buildDir / ".vix" / "obj";
      graphConfig.compiler = "c++";
      graphConfig.buildFingerprint =
          make_object_cache_build_fingerprint(plan, opt);

      graphConfig.includeDirs.push_back((plan.userProjectDir / "include").string());
      graphConfig.includeDirs.push_back((plan.userProjectDir / "src").string());

      graphConfig.flags.push_back("-Wall");
      graphConfig.flags.push_back("-Wextra");

      build::BuildGraph graph(graphConfig);

      scan = graph.scan_project();

      const fs::path compileCommandsPath =
          build::default_compile_commands_path(plan.buildDir);

      importedCompileCommands =
          graph.load_compile_commands(compileCommandsPath);

      const fs::path buildNinjaPath =
          build::default_build_ninja_path(plan.buildDir);

      importedNinjaTasks =
          graph.load_ninja_build(buildNinjaPath);

      graph.load_dependency_files();

      const fs::path graphPath =
          build::BuildGraph::default_graph_path(plan.buildDir);

      const auto previousGraph =
          build::BuildGraph::load(graphPath);

      graph.propagate_dirty();

      if (previousGraph)
        graph.mark_clean_from_previous(*previousGraph);
      else
        graph.mark_all_dirty();

      graph.propagate_dirty();

      (void)opt;

      return graph;
    }

    static std::vector<fs::path> graph_object_paths(const build::BuildGraph &graph)
    {
      std::vector<fs::path> objects;

      for (const build::BuildTask &task : graph.compile_tasks())
      {
        for (const auto &outputId : task.outputs)
        {
          const build::BuildNode *node = graph.find_node(outputId);
          if (!node)
            continue;

          if (node->kind != build::BuildNodeKind::Object)
            continue;

          if (!file_exists_regular(node->path))
            continue;

          objects.push_back(node->path);
        }
      }

      std::sort(objects.begin(), objects.end());
      objects.erase(std::unique(objects.begin(), objects.end()), objects.end());

      return objects;
    }

    static int run_graph_link(
        const build::BuildGraph &graph,
        const process::Options &opt,
        const process::Plan &plan,
        const fs::path &outputBinary)
    {
      (void)opt;
      const std::vector<fs::path> objects = graph_object_paths(graph);

      if (objects.empty())
      {
        error("Graph build produced no object files.");
        return 1;
      }

      std::vector<std::string> argv;
      argv.reserve(objects.size() + 8);

      argv.push_back(graph.config().compiler.empty() ? "c++" : graph.config().compiler);

      if (plan.fastLinkerFlag && !plan.fastLinkerFlag->empty())
        argv.push_back(*plan.fastLinkerFlag);

      for (const fs::path &object : objects)
        argv.push_back(object.string());

      argv.push_back("-o");
      argv.push_back(outputBinary.string());

      std::string output;
      const process::ExecResult r =
          build::run_process_capture(argv, {}, output);

      if (r.exitCode != 0)
      {
        error("Graph link failed.");
        if (!output.empty())
          std::cerr << output;
        return r.exitCode == 0 ? 1 : r.exitCode;
      }

#ifndef _WIN32
      std::error_code ec;
      fs::permissions(
          outputBinary,
          fs::perms::owner_exec |
              fs::perms::group_exec |
              fs::perms::others_exec,
          fs::perm_options::add,
          ec);
#endif

      return 0;
    }

    static int run_graph_build(
        build::BuildGraph &graph,
        const fs::path &graphPath,
        const process::Options &opt,
        const process::Plan &plan,
        const artifact_cache::Artifact &projectArtifact,
        const std::vector<artifact_cache::ProjectInput> &projectInputs,
        bool verboseMode)
    {
      {
        std::string err;
        if (!util::ensure_dir(graph.config().objectDir, err))
        {
          error("Unable to create Vix graph object directory: " +
                graph.config().objectDir.string());

          if (!err.empty())
            hint(err);

          return 1;
        }
      }

      build::ObjectCache objectCache(plan.buildDir);

      if (!objectCache.ensure_layout())
      {
        error("Unable to initialize Vix object cache.");
        return 1;
      }

      const fs::path outputBinary = graph_output_binary_path(opt, plan);
      const std::vector<build::BuildTask> dirtyTasks = graph.dirty_compile_tasks();

      const bool outputMissing = !file_exists_regular(outputBinary);
      const bool needsCompile = !dirtyTasks.empty();
      const bool needsLink = needsCompile || outputMissing;

      if (!needsCompile && !needsLink)
      {
        if (!graph.save(graphPath) && !opt.quiet)
          hint("Warning: unable to write Vix build graph");

        if (!opt.quiet)
        {
          build::print_build_success(std::cout, "Up to date");
          build::print_build_success(std::cout, "Done");
        }

        return 0;
      }

      if (verboseMode && !opt.quiet)
      {
        step("graph build: " + std::to_string(dirtyTasks.size()) + " dirty compile tasks");
      }

      if (needsCompile)
      {
        build::BuildSchedulerOptions schedulerOptions;
        schedulerOptions.jobs = opt.jobs;
        schedulerOptions.quiet = opt.quiet;
        schedulerOptions.stopOnFirstFailure = true;

        build::BuildScheduler scheduler(schedulerOptions);
        scheduler.add_tasks(dirtyTasks);

        const build::BuildSchedulerResult result =
            scheduler.run(
                [&](build::BuildTask &task)
                {
                  return run_cached_graph_compile_task(
                      graph,
                      objectCache,
                      task);
                });

        if (!result.success())
        {
          for (const auto &taskResult : result.results)
          {
            if (!taskResult.output.empty())
              std::cerr << taskResult.output;
          }

          return 1;
        }
      }

      const int linkCode =
          run_graph_link(
              graph,
              opt,
              plan,
              outputBinary);

      if (linkCode != 0)
        return linkCode;

      if (!store_project_target_artifact(projectArtifact, opt, plan) &&
          !opt.quiet &&
          opt.verbose)
      {
        build::print_build_info(
            std::cout,
            "Artifact cache store skipped");
      }

      const auto state =
          artifact_cache::ArtifactCache::make_build_state(
              plan.signature,
              plan.projectFingerprint,
              projectArtifact.root.string(),
              outputBinary.string(),
              opt.buildTarget,
              plan.preset.name,
              plan.preset.buildType,
              projectArtifact.target,
              projectArtifact.compiler,
              projectInputs);

      if (!artifact_cache::ArtifactCache::write_build_state(plan.buildDir, state) &&
          !opt.quiet)
      {
        hint("Warning: unable to write Vix build state");
      }

      if (!graph.save(graphPath) && !opt.quiet)
        hint("Warning: unable to write Vix build graph");

      if (opt.exportBin)
      {
        const fs::path dest = plan.userProjectDir / outputBinary.filename();

        if (!export_built_binary(outputBinary, dest, opt.quiet))
          return 1;
      }
      else if (!opt.outPath.empty())
      {
        write_last_binary(outputBinary);

        if (!opt.quiet)
          success("Exported binary: " + outputBinary.string());
      }
      else
      {
        write_last_binary(outputBinary);
      }

      if (!opt.quiet)
      {
        build::print_build_success(
            std::cout,
            needsCompile ? "Built with graph" : "Linked with graph");

        build::print_build_success(std::cout, "Done");
      }

      return 0;
    }

    static void print_vix_build_header(
        const std::string &action,
        const process::Options &opt,
        const process::Plan &plan)
    {
      build::print_task_header_full(
          std::cout,
          action,
          build::default_build_target_name(opt, plan),
          display_build_profile(plan),
          {});
    }

    static void print_vix_build_success(const std::string &message)
    {
      build::print_build_success(std::cout, message);
    }

    static void print_vix_build_success_timed(
        const std::string &message,
        long long milliseconds)
    {
      build::print_task_success_timed(
          std::cout,
          message,
          milliseconds);
    }

    static bool can_use_native_vix_app_build(
        const process::Options &opt,
        const app::AppManifest &manifest)
    {
      if (!opt.useCache)
        return false;

      if (opt.clean)
        return false;

      if (!opt.targetTriple.empty())
        return false;

      if (!opt.cmakeArgs.empty())
        return false;

      if (opt.withSqlite || opt.withMySql)
        return false;

      if (opt.linkStatic)
        return false;

      if (!opt.buildTarget.empty() && opt.buildTarget != manifest.name)
        return false;

      if (manifest.type != app::AppTargetType::Executable)
        return false;

      if (!manifest.links.empty())
        return false;

      if (!manifest.packages.empty())
        return false;

      if (!manifest.resources.empty())
        return false;

      if (!manifest.compileFeatures.empty())
        return false;

      if (manifest.sources.empty())
        return false;

      return true;
    }

    static fs::path native_vix_app_build_dir(
        const fs::path &projectDir,
        const process::Options &opt)
    {
      if (opt.preset == "release")
        return projectDir / ".vix" / "native" / "release";

      return projectDir / ".vix" / "native" / "dev";
    }

    static fs::path native_vix_app_output_path(
        const fs::path &projectDir,
        const fs::path &buildDir,
        const app::AppManifest &manifest)
    {
      if (!manifest.outputDir.empty())
        return projectDir / manifest.outputDir / platform_executable_name(manifest.name);

      return buildDir / platform_executable_name(manifest.name);
    }

    static std::string native_cpp_standard_flag(const std::string &standard)
    {
      if (standard == "c++11" || standard == "cpp11" || standard == "11")
        return "-std=c++11";

      if (standard == "c++14" || standard == "cpp14" || standard == "14")
        return "-std=c++14";

      if (standard == "c++17" || standard == "cpp17" || standard == "17")
        return "-std=c++17";

      if (standard == "c++20" || standard == "cpp20" || standard == "20")
        return "-std=c++20";

      if (standard == "c++23" || standard == "cpp23" || standard == "23")
        return "-std=c++23";

      if (standard == "c++26" || standard == "cpp26" || standard == "26")
        return "-std=c++26";

      return "-std=c++20";
    }

    static std::vector<std::string> native_vix_app_compile_command(
        const fs::path &projectDir,
        const fs::path &source,
        const fs::path &object,
        const app::AppManifest &manifest,
        const process::Plan &plan)
    {
      std::vector<std::string> command;

      command.push_back("c++");
      command.push_back(native_cpp_standard_flag(manifest.standard));

      if (plan.preset.buildType == "Release")
      {
        command.push_back("-O2");
        command.push_back("-DNDEBUG");
      }
      else
      {
        command.push_back("-g");
        command.push_back("-O0");
      }

      command.push_back("-MMD");
      command.push_back("-MP");

      const fs::path dependencyFile =
          build::dependency_file_for_object(object);

      command.push_back("-MF");
      command.push_back(dependencyFile.string());

      for (const std::string &dir : manifest.includeDirs)
      {
        command.push_back("-I");
        command.push_back((projectDir / dir).lexically_normal().string());
      }

      for (const std::string &define : manifest.defines)
        command.push_back("-D" + define);

      for (const std::string &option : manifest.compileOptions)
        command.push_back(option);

      command.push_back("-c");
      command.push_back((projectDir / source).lexically_normal().string());

      command.push_back("-o");
      command.push_back(object.string());

      return command;
    }

    static std::vector<std::string> native_vix_app_link_command(
        const std::vector<fs::path> &objects,
        const fs::path &outputBinary,
        const app::AppManifest &manifest,
        const process::Plan &plan)
    {
      std::vector<std::string> command;

      command.push_back("c++");

      if (plan.fastLinkerFlag)
        command.push_back(*plan.fastLinkerFlag);

      for (const fs::path &object : objects)
        command.push_back(object.string());

      for (const std::string &option : manifest.linkOptions)
        command.push_back(option);

      command.push_back("-o");
      command.push_back(outputBinary.string());

      return command;
    }

    static int run_native_vix_app_build(
        const process::Options &opt,
        const fs::path &projectDir,
        const app::AppManifest &manifest,
        const std::chrono::steady_clock::time_point &commandStart)
    {
      const auto presetOpt = build::resolve_builtin_preset(opt.preset);

      if (!presetOpt)
      {
        error("Unknown preset: " + opt.preset);
        return 2;
      }

      process::Plan plan;
      plan.userProjectDir = projectDir;
      plan.projectDir = projectDir;
      plan.cmakeSourceDir = projectDir;
      plan.defaultTargetName = manifest.name;
      plan.generatedFromVixApp = false;
      plan.preset = *presetOpt;
      plan.buildDir = native_vix_app_build_dir(projectDir, opt);
      plan.launcher = detect_launcher(opt);
      plan.fastLinkerFlag = detect_fast_linker_flag(opt);

      std::string err;

      if (!util::ensure_dir(plan.buildDir, err))
      {
        error("Unable to create native vix.app build directory: " + plan.buildDir.string());

        if (!err.empty())
          hint(err);

        return 1;
      }

      const fs::path objectDir = plan.buildDir / "obj";

      if (!util::ensure_dir(objectDir, err))
      {
        error("Unable to create native vix.app object directory: " + objectDir.string());

        if (!err.empty())
          hint(err);

        return 1;
      }

      const fs::path outputBinary =
          native_vix_app_output_path(projectDir, plan.buildDir, manifest);

      if (!outputBinary.parent_path().empty() &&
          !util::ensure_dir(outputBinary.parent_path(), err))
      {
        error("Unable to create native vix.app output directory: " +
              outputBinary.parent_path().string());

        if (!err.empty())
          hint(err);

        return 1;
      }

      if (!opt.quiet)
        print_vix_build_header("Building", opt, plan);

      build::ObjectCache objectCache(plan.buildDir);

      if (!objectCache.ensure_layout())
      {
        error("Unable to initialize Vix object cache.");
        return 1;
      }

      build::BuildGraphConfig graphConfig;
      graphConfig.projectDir = projectDir;
      graphConfig.buildDir = plan.buildDir;
      graphConfig.objectDir = objectDir;
      graphConfig.compiler = "c++";
      graphConfig.buildFingerprint =
          make_object_cache_build_fingerprint(plan, opt);

      build::BuildGraph graph(graphConfig);

      build::BuildSchedulerOptions schedulerOptions;
      schedulerOptions.jobs = opt.jobs;
      schedulerOptions.quiet = opt.quiet;
      schedulerOptions.stopOnFirstFailure = true;

      build::BuildScheduler scheduler(schedulerOptions);

      std::vector<fs::path> objectPaths;
      objectPaths.reserve(manifest.sources.size());

      for (const std::string &sourceString : manifest.sources)
      {
        const fs::path sourceRel(sourceString);
        const fs::path sourcePath =
            (projectDir / sourceRel).lexically_normal();

        if (!fs::exists(sourcePath))
        {
          error("Source file not found: " + sourcePath.string());
          return 1;
        }

        std::string objectName =
            sourceRel.lexically_normal().generic_string();

        for (char &c : objectName)
        {
          const unsigned char uc = static_cast<unsigned char>(c);

          if (!(std::isalnum(uc) || c == '.' || c == '_' || c == '-'))
            c = '_';
        }

        const fs::path objectPath =
            objectDir / (objectName + ".o");

        objectPaths.push_back(objectPath);

        build::BuildNode sourceNode =
            build::make_file_build_node(
                build::BuildNodeKind::Source,
                sourcePath);

        build::BuildNode objectNode =
            build::make_file_build_node(
                build::BuildNodeKind::Object,
                objectPath);

        const std::vector<std::string> command =
            native_vix_app_compile_command(
                projectDir,
                sourceRel,
                objectPath,
                manifest,
                plan);

        build::BuildTask task =
            build::make_compile_task(
                sourceNode.id,
                objectNode.id,
                command,
                projectDir);

        graph.add_node(sourceNode);
        graph.add_node(objectNode);
        graph.add_task(task);
        scheduler.add_task(task);
      }

      const build::BuildSchedulerResult result =
          scheduler.run(
              [&](build::BuildTask &task)
              {
                return run_cached_graph_compile_task(
                    graph,
                    objectCache,
                    task);
              });

      if (!result.success())
      {
        for (const auto &taskResult : result.results)
        {
          if (!taskResult.output.empty())
            std::cerr << taskResult.output;
        }

        return 1;
      }

      const std::vector<std::string> linkCommand =
          native_vix_app_link_command(
              objectPaths,
              outputBinary,
              manifest,
              plan);

      std::string linkOutput;

      const process::ExecResult linkResult =
          build::run_process_capture(
              linkCommand,
              {},
              linkOutput);

      if (linkResult.exitCode != 0)
      {
        error("Native vix.app link failed.");

        if (!linkOutput.empty())
          std::cerr << linkOutput;

        return linkResult.exitCode == 0 ? 1 : linkResult.exitCode;
      }

#ifndef _WIN32
      std::error_code ec;
      fs::permissions(
          outputBinary,
          fs::perms::owner_exec |
              fs::perms::group_exec |
              fs::perms::others_exec,
          fs::perm_options::add,
          ec);
#endif

      write_last_binary(outputBinary);

      if (opt.exportBin || !opt.outPath.empty())
      {
        fs::path dest;

        if (opt.exportBin)
          dest = projectDir / outputBinary.filename();
        else
          dest = fs::absolute(fs::path(opt.outPath));

        if (!export_built_binary(outputBinary, dest, opt.quiet))
          return 1;
      }

      if (!opt.quiet)
      {
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - commandStart)
                .count();

        print_vix_build_success("Native vix.app");
        print_vix_build_success_timed("Done", ms);
      }

      return 0;
    }

    class BuildCommand
    {
    public:
      explicit BuildCommand(process::Options opt) : opt_(std::move(opt)) {}

      int run()
      {
        const fs::path cwd = fs::current_path();
        const auto commandStart = std::chrono::steady_clock::now();

        if (opt_.singleCpp)
          return run_single_cpp_build();

        {
          fs::path base = cwd;

          if (!opt_.dir.empty())
            base = fs::absolute(fs::path(opt_.dir));

          const app::AppProjectResolveResult project =
              app::resolve_app_project(base);

          if (project.success() &&
              project.kind == app::AppProjectKind::VixApp)
          {
            const app::AppManifestLoadResult loadResult =
                app::load_app_manifest(project.appManifestPath);

            if (!loadResult.success())
            {
              error("Failed to load vix.app.");
              hint(loadResult.error);
              return 1;
            }

            if (!opt_.warnings &&
                can_use_native_vix_app_build(opt_, loadResult.manifest))
            {
              return run_native_vix_app_build(
                  opt_,
                  project.userProjectDir,
                  loadResult.manifest,
                  commandStart);
            }

            if (debug_build_details_enabled() && !opt_.quiet)
              hint("Native vix.app fallback: generated CMake path");
          }
        }

        const auto planOpt = make_plan(opt_, cwd);
        if (!planOpt)
        {
          error("Unable to determine the project directory (missing CMakeLists.txt?)");
          hint("Run from your project root, or pass: vix build --dir <path>");
          return 1;
        }

        plan_ = *planOpt;

        if (opt_.warnings)
        {
          return print_last_build_warnings(
              opt_,
              plan_);
        }

        const fs::path globalPackagesFile =
            plan_.buildDir / "vix-global-packages.cmake";

        if (opt_.clean)
        {
          try
          {
            clean_local_build_dirs(
                plan_.userProjectDir,
                opt_.targetTriple,
                opt_.quiet);
          }
          catch (const std::exception &)
          {
            return 1;
          }
        }

        const bool verboseMode = opt_.verbose || opt_.cmakeVerbose;
        const bool defer = (!opt_.quiet && verboseMode);
        DeferredConsole out(defer);

        std::string tc;

#ifndef _WIN32
        if (!util::executable_on_path("ld"))
        {
          if (!opt_.quiet)
          {
            hint("System linker 'ld' not found. Build may fail at link step.");
            hint("Fix (recommended): sudo apt install -y binutils build-essential");
          }
        }

        if (plan_.fastLinkerFlag &&
            *plan_.fastLinkerFlag == "-fuse-ld=lld" &&
            !util::executable_on_path("ld.lld"))
        {
          if (!opt_.quiet)
          {
            hint("Requested lld but 'ld.lld' is missing -> falling back to default linker.");
            hint("Install optional speedup: sudo apt install -y lld");
          }

          plan_.fastLinkerFlag.reset();
        }

        if (plan_.fastLinkerFlag &&
            *plan_.fastLinkerFlag == "-fuse-ld=mold" &&
            !util::executable_on_path("mold"))
        {
          if (!opt_.quiet)
          {
            hint("Requested mold but 'mold' is missing -> falling back to default linker.");
            hint("Install optional speedup: sudo apt install -y mold");
          }

          plan_.fastLinkerFlag.reset();
        }

        plan_.cmakeVars = build_cmake_vars(
            plan_.preset,
            opt_,
            plan_.toolchainFile,
            plan_.launcher,
            plan_.fastLinkerFlag,
            globalPackagesFile);

        if (!opt_.targetTriple.empty())
        {
          tc = build::toolchain_contents_for_triple(
              opt_.targetTriple,
              opt_.sysroot);
        }

        plan_.signature = make_signature(plan_, opt_, tc);
#endif

        if (!opt_.targetTriple.empty())
        {
          const std::string gcc = opt_.targetTriple + "-gcc";
          const std::string gxx = opt_.targetTriple + "-g++";

          if (!util::executable_on_path(gcc) ||
              !util::executable_on_path(gxx))
          {
            error("Cross toolchain not found on PATH for target: " + opt_.targetTriple);
            hint("Install the cross compiler and ensure binaries exist:");
            hint("  " + gcc);
            hint("  " + gxx);
            return 1;
          }
        }

        {
          std::string err;
          if (!util::ensure_dir(plan_.buildDir, err))
          {
            error("Unable to create build directory: " + plan_.buildDir.string());

            if (!err.empty())
              hint(err);

            return 1;
          }
        }

        artifact_cache::Artifact projectArtifact =
            make_project_artifact(plan_, opt_, tc);

        const auto previousState =
            artifact_cache::ArtifactCache::read_build_state(plan_.buildDir);

        std::vector<artifact_cache::ProjectInput> projectInputs =
            artifact_cache::ArtifactCache::snapshot_project_inputs(
                plan_.userProjectDir,
                previousState ? &previousState->inputs : nullptr);

        const bool buildStateHit =
            opt_.useCache &&
            !opt_.clean &&
            previousState &&
            artifact_cache::ArtifactCache::build_state_matches(
                *previousState,
                plan_.signature,
                plan_.projectFingerprint,
                opt_.buildTarget,
                plan_.preset.name,
                plan_.preset.buildType,
                projectArtifact.target,
                projectArtifact.compiler,
                projectInputs);

        if (buildStateHit && debug_build_details_enabled() && !opt_.quiet)
        {
          step("build state: hit -> " +
               artifact_cache::ArtifactCache::build_state_path(plan_.buildDir).string());
        }

        if (buildStateHit &&
            !opt_.explain &&
            !opt_.exportBin &&
            opt_.outPath.empty())
        {
          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - commandStart)
                  .count();

          if (!opt_.quiet)
          {
            print_vix_build_header("Checking", opt_, plan_);
            print_vix_build_success_timed("Up to date", ms);
          }

          return 0;
        }

        if (debug_build_details_enabled() && !opt_.quiet)
        {
          if (artifact_cache::ArtifactCache::exists(projectArtifact))
            step("artifact cache: hit -> " + projectArtifact.root.string());
          else
            step("artifact cache: miss -> " + projectArtifact.root.string());

          out.print("  Using project directory:\n");
          out.print("    • " + plan_.userProjectDir.string() + "\n");

          if (plan_.generatedFromVixApp)
          {
            out.print("  Using generated CMake source:\n");
            out.print("    • " + plan_.cmakeSourceDir.string() + "\n");
          }

          out.print("\n");
        }

        if (!opt_.targetTriple.empty())
        {
          tc = build::toolchain_contents_for_triple(
              opt_.targetTriple,
              opt_.sysroot);

          projectArtifact = make_project_artifact(plan_, opt_, tc);

          if (!write_if_different(plan_.toolchainFile, tc))
          {
            error("Failed to write toolchain file: " + plan_.toolchainFile.string());
            hint("Check filesystem permissions.");
            return 1;
          }
        }

        bool configuredThisRun = false;
        long long totalMs = 0;

        if (need_configure(opt_, plan_))
        {
          configuredThisRun = true;

          if (verboseMode && !opt_.quiet)
          {
            out.print("Configuring " + build::default_build_target_name(opt_, plan_) +
                      " (" + display_build_profile(plan_) + ")\n");

            if (debug_build_details_enabled())
            {
              if (plan_.launcher)
                out.print("  • compiler cache: " + *plan_.launcher + "\n");

              if (plan_.fastLinkerFlag)
                out.print("  • fast linker: " + *plan_.fastLinkerFlag + "\n");

              for (const auto &kv : plan_.cmakeVars)
                out.print("  • " + kv.first + "=" + kv.second + "\n");

              out.print("\n");
            }
          }

          const auto t0 = std::chrono::steady_clock::now();
          const auto argv = build::cmake_configure_argv(plan_, opt_);

          const process::ExecResult r = build::run_process_live_to_log(
              argv,
              {},
              plan_.configureLog,
              (opt_.quiet || !verboseMode),
              opt_.cmakeVerbose,
              false);

          if (r.exitCode != 0)
          {
            out.discard();

            const std::string log =
                util::read_text_file_or_empty(plan_.configureLog);

            const bool handled =
                vix::cli::ErrorHandler::printBuildErrors(
                    log,
                    plan_.cmakeSourceDir / "CMakeLists.txt",
                    "CMake configure failed");

            if (!opt_.quiet)
            {
              if (!handled)
                hint("run with VIX_LOG_LEVEL=debug to inspect the configure command");

              print_debug_command_if_enabled(r);
            }

            return (r.exitCode == 0) ? 2 : r.exitCode;
          }

          if (opt_.useCache)
          {
            if (!util::write_text_file_atomic(plan_.sigFile, plan_.signature))
            {
              if (!opt_.quiet)
                hint("Warning: unable to write config signature file");
            }
          }

          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();

          totalMs += ms;

          if (!opt_.quiet && verboseMode)
          {
            out.print(PAD + std::string(GREEN) + "✔ Configured in " + RESET +
                      util::format_seconds(ms) + "\n");
          }
        }
        else
        {
          if (debug_build_details_enabled() && !opt_.quiet)
          {
            out.print("  Using existing configuration (cache-friendly).\n");
            out.print("    • " + plan_.buildDir.string() + "\n\n");
          }
        }

        std::size_t importedCompileCommands = 0;
        std::size_t importedNinjaTasks = 0;
        build::BuildGraphScanResult scan{};

        build::BuildGraph graph =
            make_build_graph_after_configure(
                opt_,
                plan_,
                importedCompileCommands,
                importedNinjaTasks,
                scan);

        const fs::path graphPath =
            build::BuildGraph::default_graph_path(plan_.buildDir);

        if (debug_build_details_enabled() && !opt_.quiet)
        {
          step("build graph: " +
               std::to_string(scan.sources) + " sources, " +
               std::to_string(scan.headers) + " headers, " +
               std::to_string(graph.compile_tasks().size()) + " compile tasks, " +
               std::to_string(importedCompileCommands) + " imported commands, " +
               std::to_string(importedNinjaTasks) + " ninja tasks");
        }

        if (can_use_target_artifact_cache(opt_) &&
            restore_project_target_artifact(projectArtifact, opt_, plan_))
        {
          if (!graph.save(graphPath) && !opt_.quiet)
            hint("Warning: unable to write Vix build graph");

          const fs::path restoredBinary =
              build::default_project_executable_path(opt_, plan_);

          const auto state =
              artifact_cache::ArtifactCache::make_build_state(
                  plan_.signature,
                  plan_.projectFingerprint,
                  projectArtifact.root.string(),
                  restoredBinary.string(),
                  opt_.buildTarget,
                  plan_.preset.name,
                  plan_.preset.buildType,
                  projectArtifact.target,
                  projectArtifact.compiler,
                  projectInputs);

          if (!artifact_cache::ArtifactCache::write_build_state(plan_.buildDir, state) &&
              !opt_.quiet)
          {
            hint("Warning: unable to write Vix build state");
          }

          if (!opt_.quiet)
          {
            print_vix_build_header("Restoring", opt_, plan_);
            print_vix_build_success("Artifact cache hit");
            print_vix_build_success("Done");
          }

          return 0;
        }

        std::optional<build::BuildGraph> previousGraphForExplain;

        if (opt_.explain)
        {
          previousGraphForExplain =
              build::BuildGraph::load(graphPath);

          print_rebuild_explanation(
              graph,
              previousGraphForExplain ? &*previousGraphForExplain : nullptr,
              opt_,
              plan_);
        }

        if (can_use_target_graph_executor(
                opt_,
                importedCompileCommands,
                importedNinjaTasks))
        {
          build::BuildGraphExecutorOptions executorOptions;
          executorOptions.buildDir = plan_.buildDir;
          executorOptions.target = build::default_graph_target_name(opt_, plan_);
          executorOptions.jobs = opt_.jobs;
          executorOptions.quiet = opt_.quiet;
          executorOptions.verbose = verboseMode;

          build::BuildGraphExecutor executor(executorOptions);

          const build::BuildGraphExecutorResult graphResult =
              executor.run_target(graph);

          if (graphResult.ok)
          {
            if (!store_project_target_artifact(projectArtifact, opt_, plan_) &&
                !opt_.quiet &&
                opt_.verbose)
            {
              build::print_build_info(
                  std::cout,
                  "Artifact cache store skipped");
            }

            std::string lastBinary;

            const auto exeOpt = resolve_main_executable(
                plan_.buildDir,
                plan_.userProjectDir,
                opt_.buildTarget,
                plan_.defaultTargetName);

            if (exeOpt)
              lastBinary = exeOpt->string();

            const auto state =
                artifact_cache::ArtifactCache::make_build_state(
                    plan_.signature,
                    plan_.projectFingerprint,
                    projectArtifact.root.string(),
                    lastBinary,
                    opt_.buildTarget,
                    plan_.preset.name,
                    plan_.preset.buildType,
                    projectArtifact.target,
                    projectArtifact.compiler,
                    projectInputs);

            if (!artifact_cache::ArtifactCache::write_build_state(plan_.buildDir, state) &&
                !opt_.quiet)
            {
              hint("Warning: unable to write Vix build state");
            }

            if (!graph.save(graphPath) && !opt_.quiet)
              hint("Warning: unable to write Vix build graph");

            if (!opt_.quiet)
            {
              print_vix_build_header("Building", opt_, plan_);

              print_graph_warnings_modern(graphResult.output);

              if (configuredThisRun)
                print_vix_build_success("Configured");

              print_vix_build_success("Graph target: " + graphResult.target);

              if (graphResult.dirtyCompileTasks == 0)
              {
                print_vix_build_success("Up to date");
              }
              else
              {
                print_vix_build_success(
                    "Compiled " + std::to_string(graphResult.dirtyCompileTasks) +
                    " dirty files");
              }

              print_vix_build_success("Done");
            }

            return 0;
          }

          if (debug_build_details_enabled() && !opt_.quiet)
            hint("Graph target executor fallback: " + graphResult.output);
        }

        if (graph_executor_enabled() && can_use_graph_build(opt_, plan_, scan))
        {
          return run_graph_build(
              graph,
              graphPath,
              opt_,
              plan_,
              projectArtifact,
              projectInputs,
              verboseMode);
        }

        {
          if (!opt_.quiet)
          {
            if (verboseMode)
            {
              out.flush_to_stdout();

              build::print_build_header_full(
                  std::cout,
                  build::default_build_target_name(opt_, plan_),
                  display_build_profile(plan_),
                  plan_.launcher,
                  plan_.fastLinkerFlag,
                  opt_.jobs <= 0 ? build::default_jobs() : opt_.jobs);
            }
            else
            {
              build::print_build_header_full(
                  std::cout,
                  build::default_build_target_name(opt_, plan_),
                  display_build_profile(plan_),
                  std::nullopt,
                  std::nullopt,
                  0);
            }

            std::cout.flush();
          }

          const auto t0 = std::chrono::steady_clock::now();
          const auto argv = build::cmake_build_argv(plan_, opt_);
          const auto env = build::ninja_env(opt_, plan_);

          const bool showRawBuildOutput = opt_.cmakeVerbose;
          const bool progressOnly = !showRawBuildOutput;

          const process::ExecResult r = build::run_process_live_to_log(
              argv,
              env,
              plan_.buildLog,
              opt_.quiet,
              opt_.cmakeVerbose,
              progressOnly);

          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();

          totalMs += ms;

          if (r.exitCode != 0)
          {
            out.discard();

            const std::string log =
                util::read_text_file_or_empty(plan_.buildLog);

            const bool handled =
                vix::cli::ErrorHandler::printBuildErrors(
                    log,
                    plan_.cmakeSourceDir / "CMakeLists.txt",
                    "Build failed");

            if (!opt_.quiet)
            {
              if (!handled)
                hint("run with VIX_LOG_LEVEL=debug to inspect the build command");

              print_debug_command_if_enabled(r);
            }
            return (r.exitCode == 0) ? 3 : r.exitCode;
          }

          if (!store_project_target_artifact(projectArtifact, opt_, plan_) &&
              !opt_.quiet &&
              opt_.verbose)
          {
            build::print_build_info(
                std::cout,
                "Artifact cache store skipped");
          }
          std::string lastBinary;

          const auto exeOpt = resolve_main_executable(
              plan_.buildDir,
              plan_.userProjectDir,
              opt_.buildTarget,
              plan_.defaultTargetName);

          if (exeOpt)
            lastBinary = exeOpt->string();

          const auto state =
              artifact_cache::ArtifactCache::make_build_state(
                  plan_.signature,
                  plan_.projectFingerprint,
                  projectArtifact.root.string(),
                  lastBinary,
                  opt_.buildTarget,
                  plan_.preset.name,
                  plan_.preset.buildType,
                  projectArtifact.target,
                  projectArtifact.compiler,
                  projectInputs);

          if (!artifact_cache::ArtifactCache::write_build_state(plan_.buildDir, state) &&
              !opt_.quiet)
          {
            hint("Warning: unable to write Vix build state");
          }

          if (!graph.save(graphPath) && !opt_.quiet)
            hint("Warning: unable to write Vix build graph");

          const std::string buildLog =
              util::read_text_file_or_empty(plan_.buildLog);

          if (!opt_.quiet)
            print_compiler_warnings_summary(buildLog);

          const std::size_t builtTargets =
              count_built_targets_from_log(buildLog);

          if (!opt_.quiet)
          {
            if (verboseMode)
            {
              const std::string profile =
                  (plan_.preset.buildType == "Release")
                      ? "release [optimized]"
                      : "dev [unoptimized + debuginfo]";

              build::print_build_done(
                  std::cout,
                  profile,
                  util::format_seconds(ms));
            }
            else
            {
              if (configuredThisRun)
                build::print_build_success(std::cout, "Configured");

              if (builtTargets > 0)
              {
                build::print_build_success(
                    std::cout,
                    "Built (" + std::to_string(builtTargets) + " targets)");
              }
              else
              {
                build::print_build_success(std::cout, "Built");
              }

              build::print_build_success(
                  std::cout,
                  "Done in " + util::format_seconds(totalMs));
            }
          }
        }

        if (opt_.exportBin || !opt_.outPath.empty())
        {
          const auto exeOpt = resolve_main_executable(
              plan_.buildDir,
              plan_.userProjectDir,
              opt_.buildTarget,
              plan_.defaultTargetName);

          if (!exeOpt)
          {
            error("Unable to resolve the main executable to export.");
            hint("Use --build-target <name> if your project produces multiple executables.");
            hint("Run: vix build --build-target <target> -v");
            return 1;
          }

          fs::path dest;

          if (opt_.exportBin)
            dest = plan_.userProjectDir / exeOpt->filename();
          else
            dest = fs::absolute(fs::path(opt_.outPath));

          if (!export_built_binary(*exeOpt, dest, opt_.quiet))
            return 1;
        }

        out.flush_to_stdout();
        return 0;
      }

    private:
      int run_single_cpp_build();

    private:
      process::Options opt_;
      process::Plan plan_{};
    };

  } // namespace

  int run(const std::vector<std::string> &args)
  {
    int parseExit = 0;
    process::Options opt = parse_args_or_exit(args, parseExit);

    if (opt.exportBin && !opt.outPath.empty())
    {
      error("Options --bin and --out cannot be used together.");
      hint("Use either --bin or --out <path>.");
      return 2;
    }

    if (parseExit == -2)
      return help();
    if (parseExit != 0)
      return parseExit;

    if (!build::resolve_builtin_preset(opt.preset))
    {
      error("Unknown preset: " + opt.preset);
      hint("Available presets: dev, dev-ninja, release");
      return 2;
    }

    BuildCommand cmd(std::move(opt));
    return cmd.run();
  }

  int BuildCommand::run_single_cpp_build()
  {
    if (opt_.cppFile.empty())
    {
      error("No C++ source file provided.");
      return 1;
    }

    if (!fs::exists(opt_.cppFile))
    {
      error("Source file not found: " + opt_.cppFile.string());
      return 1;
    }

    run_detail::Options runOpt{};
    runOpt.singleCpp = true;
    runOpt.cppFile = fs::absolute(opt_.cppFile);

    runOpt.preset = opt_.preset;
    runOpt.dir = opt_.dir;
    runOpt.jobs = opt_.jobs;
    runOpt.clean = opt_.clean;

    runOpt.quiet = opt_.quiet;
    runOpt.verbose = opt_.verbose;

    runOpt.withSqlite = opt_.withSqlite;
    runOpt.withMySql = opt_.withMySql;

    runOpt.enableSanitizers = false;
    runOpt.enableUbsanOnly = false;

    runOpt.forceServerLike = false;
    runOpt.forceScriptLike = true;

    runOpt.watch = false;
    runOpt.timeoutSec = 0;
    runOpt.cwd.clear();

    runOpt.runArgs.clear();
    runOpt.runEnv.clear();
    runOpt.scriptFlags = opt_.cmakeArgs;

    fs::path exePath;
    const int code = run_detail::build_script_executable(runOpt, exePath);
    if (code != 0)
      return code;

    if (exePath.empty() || !fs::exists(exePath))
    {
      error("Built executable was not produced.");
      return 1;
    }

    fs::path dest;

    if (!opt_.outPath.empty())
    {
      dest = fs::absolute(fs::path(opt_.outPath));
    }
    else
    {
      dest = fs::current_path() / exePath.filename();
    }

    return export_built_binary(exePath, dest, opt_.quiet) ? 0 : 1;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix build [source.cpp] [options] -- [cmake args...]\n\n";

    out << "Description:\n";
    out << "  Configure and build a C++ project with Vix.\n";
    out << "  Works with CMake projects, vix.app projects, and single C++ files.\n\n";

    out << "Core features:\n";
    out << "  • Embedded build presets\n";
    out << "  • Fast no-op detection\n";
    out << "  • Target-aware builds\n";
    out << "  • Artifact cache\n";
    out << "  • Object cache\n";
    out << "  • Graph-based incremental builds when safe\n";
    out << "  • Auto ccache/sccache launcher\n";
    out << "  • Auto mold/lld linker\n";
    out << "  • Human-readable compiler errors and warnings\n\n";

    out << "Presets:\n";
    out << "  dev        Debug build in build-dev\n";
    out << "  dev-ninja  Debug build in build-ninja\n";
    out << "  release    Release build in build-release\n\n";

    out << "Project:\n";
    out << "  [source.cpp]              Build one C++ source file directly\n";
    out << "  -d, --dir <path>          Project directory\n";
    out << "  --dir=<path>              Same as --dir <path>\n\n";

    out << "Build:\n";
    out << "  --preset <name>           Use a preset: dev, dev-ninja, release\n";
    out << "  --preset=<name>           Same as --preset <name>\n";
    out << "  --build-target <name>     Build a specific CMake target\n";
    out << "  --build-target=<name>     Same as --build-target <name>\n";
    out << "  -j, --jobs <n>            Number of parallel build jobs\n";
    out << "  --jobs=<n>                Same as --jobs <n>\n";
    out << "  --clean                   Remove local build directories and configure again\n";
    out << "  --fast                    Use fast no-op detection when possible\n";
    out << "  --explain                 Explain why files or targets rebuild\n";
    out << "  --warnings                Show warnings from the last build log\n";
    out << "  --warning-check           Build with strong compiler warnings enabled\n";
    out << "  --page <n>                Warning page to display with --warnings, default: 1\n";
    out << "  --limit <n>               Warnings per page with --warnings, default: 10\n";
    out << "  --no-cache                Disable Vix cache shortcuts\n";
    out << "  --no-status               Disable Ninja progress status\n";
    out << "  --no-up-to-date           Disable Ninja dry-run up-to-date detection\n\n";

    out << "Output:\n";
    out << "  --bin                     Export the built executable to the project root\n";
    out << "  --out <path>              Export the built executable to a specific path\n";
    out << "  --out=<path>              Same as --out <path>\n\n";

    out << "Tooling:\n";
    out << "  --launcher <mode>         Compiler launcher: auto, none, sccache, ccache\n";
    out << "  --launcher=<mode>         Same as --launcher <mode>\n";
    out << "  --linker <mode>           Linker mode: auto, default, mold, lld\n";
    out << "  --linker=<mode>           Same as --linker <mode>\n\n";

    out << "Cross-compilation:\n";
    out << "  --target <triple>         Cross-compilation target triple\n";
    out << "  --target=<triple>         Same as --target <triple>\n";
    out << "  --sysroot <path>          Sysroot for the cross toolchain\n";
    out << "  --sysroot=<path>          Same as --sysroot <path>\n";
    out << "  --targets                 List detected cross toolchains on PATH\n\n";

    out << "Linking and dependencies:\n";
    out << "  --static                  Request static linking\n";
    out << "  --with-sqlite             Enable SQLite support\n";
    out << "  --with-mysql              Enable MySQL support\n\n";

    out << "Logs and output:\n";
    out << "  -q, --quiet               Minimal output\n";
    out << "  -v, --verbose             Show detailed build information\n";
    out << "  --cmake-verbose           Show raw CMake configure output\n";
    out << "  -h, --help                Show this help\n\n";

    out << "CMake passthrough:\n";
    out << "  -- [cmake args...]        Pass extra arguments to CMake configure\n\n";

    out << "Environment variables:\n";
    out << "  VIX_BUILD_HEARTBEAT=0     Disable configure/build heartbeat\n";
    out << "  VIX_BUILD_HEARTBEAT=1     Force heartbeat when no output is produced\n";
    out << "  VIX_GRAPH_EXECUTOR=0      Disable graph target executor\n";
    out << "  VIX_LOG_LEVEL=debug       Show deeper diagnostic output\n";
    out << "  VIX_LOG_LEVEL=trace       Show trace-level diagnostic output\n\n";

    out << "Examples:\n";
    out << "  vix build\n";
    out << "  vix build -v\n";
    out << "  vix build --fast\n";
    out << "  vix build --clean\n";
    out << "  vix build --explain\n";
    out << "  vix build --warnings\n";
    out << "  vix build --warning-check --build-target all -v --clean\n";
    out << "  vix build --warnings --page 2\n";
    out << "  vix build --warnings --limit 50\n";
    out << "  vix build --warnings --page 3 --limit 20\n";
    out << "  vix build --preset release\n";
    out << "  vix build --preset=release\n";
    out << "  vix build --build-target all\n";
    out << "  vix build --build-target vix -v\n";
    out << "  vix build --build-target=vix\n";
    out << "  vix build -j 8\n";
    out << "  vix build --jobs=8\n";
    out << "  vix build --launcher ccache --linker mold\n";
    out << "  vix build --launcher=ccache --linker=mold\n";
    out << "  vix build --with-sqlite\n";
    out << "  vix build --with-mysql\n";
    out << "  vix build --preset release --static\n";
    out << "  vix build --target aarch64-linux-gnu\n";
    out << "  vix build --target=aarch64-linux-gnu\n";
    out << "  vix build --sysroot /opt/sysroot\n";
    out << "  vix build --targets\n";
    out << "  vix build --bin\n";
    out << "  vix build --out dist/app\n";
    out << "  vix build --out=dist/app\n";
    out << "  vix build main.cpp\n";
    out << "  vix build main.cpp --bin\n";
    out << "  vix build main.cpp --out app\n";
    out << "  vix build main.cpp --with-sqlite --out app\n";
    out << "  vix build main.cpp --target x86_64-windows-gnu --out app.exe\n";
    out << "  vix build --linker lld -- -DVIX_SYNC_BUILD_TESTS=ON\n";
    out << "  VIX_GRAPH_EXECUTOR=0 vix build --build-target vix\n\n";

    out << "Logs:\n";
    out << "  build-dev/configure.log\n";
    out << "  build-dev/build.log\n";
    out << "  build-ninja/configure.log\n";
    out << "  build-ninja/build.log\n";
    out << "  build-release/configure.log\n";
    out << "  build-release/build.log\n\n";

    return 0;
  }

} // namespace vix::commands::BuildCommand
