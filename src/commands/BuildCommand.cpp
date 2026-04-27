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

namespace vix::commands::BuildCommand
{
  namespace
  {
    static constexpr std::uint64_t LOCAL_FNV_OFFSET = 1469598103934665603ull;

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

    static std::map<std::string, process::Preset> builtin_presets()
    {
      std::map<std::string, process::Preset> m;

      m.emplace("dev", process::Preset{"dev", "Ninja", "Debug", "build-dev"});
      m.emplace("dev-ninja", process::Preset{"dev-ninja", "Ninja", "Debug", "build-ninja"});
      m.emplace("release", process::Preset{"release", "Ninja", "Release", "build-release"});

      return m;
    }

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

    static std::optional<process::Preset> resolve_preset(const std::string &name)
    {
      const auto presets = builtin_presets();
      const auto it = presets.find(name);
      if (it == presets.end())
        return std::nullopt;
      return it->second;
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

    static std::string first_line_of(std::string s)
    {
      s = util::trim(s);
      const auto pos = s.find('\n');
      if (pos != std::string::npos)
        s = s.substr(0, pos);
      return util::trim(s);
    }

    static std::string detect_compiler_identity()
    {
      const std::vector<std::string> candidates = {
          "c++",
          "clang++",
          "g++"};

      for (const auto &tool : candidates)
      {
        if (!util::executable_on_path(tool))
          continue;

        std::string out;
        (void)build::run_process_capture({tool, "--version"}, {}, out);

        out = first_line_of(out);
        if (out.empty())
          return sanitize_cache_component(tool);

        return sanitize_cache_component(tool + "-" + out);
      }

      return "unknown-compiler";
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

    static artifact_cache::Artifact make_project_artifact(
        const process::Plan &plan,
        const process::Options &opt,
        const std::string &toolchainContent)
    {
      artifact_cache::Artifact a;
      a.package = sanitize_cache_component(plan.projectDir.filename().string());
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

    static std::string run_tool_version_line(const std::string &tool)
    {
      if (!util::executable_on_path(tool))
        return tool + ":<missing>\n";

      std::string out;
      (void)build::run_process_capture({tool, "--version"}, {}, out);

      out = util::trim(out);
      if (out.empty())
        return tool + ":<unknown>\n";

      const auto pos = out.find('\n');
      if (pos != std::string::npos)
        out = out.substr(0, pos);

      return tool + ":" + out + "\n";
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
          error("Failed to remove build directory: " + dir.string());
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

      if (!opt.targetTriple.empty())
        vars.emplace_back("CMAKE_TOOLCHAIN_FILE", toolchainFile.string());

      if (opt.linkStatic)
        vars.emplace_back("VIX_LINK_STATIC", "ON");

      if (!opt.targetTriple.empty())
        vars.emplace_back("VIX_TARGET_TRIPLE", opt.targetTriple);

      if (!globalPackagesFile.empty())
        vars.emplace_back("CMAKE_PROJECT_TOP_LEVEL_INCLUDES", globalPackagesFile.string());

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
      oss << "linker=" << static_cast<int>(opt.linker) << "\n";
      oss << "launcher=" << static_cast<int>(opt.launcher) << "\n";
      oss << "verbose=" << (opt.verbose ? "1" : "0") << "\n";

      oss << "tools:\n";
      oss << run_tool_version_line("cmake");
      oss << run_tool_version_line("ninja");
#ifndef _WIN32
      oss << run_tool_version_line("c++");
      oss << run_tool_version_line("clang++");
      oss << run_tool_version_line("g++");
      oss << run_tool_version_line("mold");
      oss << run_tool_version_line("ld.lld");
#endif

      if (plan.launcher)
        oss << "launcherTool:" << *plan.launcher << "\n";
      if (plan.fastLinkerFlag)
        oss << "linkerFlag:" << *plan.fastLinkerFlag << "\n";

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

      fs::path projectDir;
      const auto root = util::find_project_root(base);

      if (root)
      {
        projectDir = *root;
      }
      else if (util::file_exists(base / "CMakeLists.txt"))
      {
        projectDir = base;
      }
      else
      {
        return std::nullopt;
      }

      const auto presetOpt = resolve_preset(opt.preset);
      if (!presetOpt)
        return std::nullopt;

      process::Plan plan;
      plan.projectDir = fs::absolute(projectDir);
      plan.preset = *presetOpt;

      plan.launcher = detect_launcher(opt);
      plan.fastLinkerFlag = detect_fast_linker_flag(opt);
      plan.projectFingerprint = util::compute_cmake_config_fingerprint(plan.projectDir);

      if (!opt.targetTriple.empty())
        plan.buildDir = plan.projectDir / (plan.preset.buildDirName + "-" + opt.targetTriple);
      else
        plan.buildDir = plan.projectDir / plan.preset.buildDirName;

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
        const std::string &buildTarget)
    {
      const std::string preferredBase =
          !buildTarget.empty()
              ? buildTarget
              : projectDir.filename().string();

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

      return true;
    }

    class BuildCommand
    {
    public:
      explicit BuildCommand(process::Options opt) : opt_(std::move(opt)) {}

      int run()
      {
        const fs::path cwd = fs::current_path();

        if (opt_.singleCpp)
          return run_single_cpp_build();

        const auto planOpt = make_plan(opt_, cwd);
        if (!planOpt)
        {
          error("Unable to determine the project directory (missing CMakeLists.txt?)");
          hint("Run from your project root, or pass: vix build --dir <path>");
          return 1;
        }

        plan_ = *planOpt;
        const fs::path globalPackagesFile = plan_.buildDir / "vix-global-packages.cmake";

        if (opt_.clean)
        {
          try
          {
            clean_local_build_dirs(plan_.projectDir, opt_.targetTriple, opt_.quiet);
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

        if (plan_.fastLinkerFlag && *plan_.fastLinkerFlag == "-fuse-ld=lld" &&
            !util::executable_on_path("ld.lld"))
        {
          if (!opt_.quiet)
          {
            hint("Requested lld but 'ld.lld' is missing -> falling back to default linker.");
            hint("Install optional speedup: sudo apt install -y lld");
          }
          plan_.fastLinkerFlag.reset();
        }

        if (plan_.fastLinkerFlag && *plan_.fastLinkerFlag == "-fuse-ld=mold" &&
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
          tc = build::toolchain_contents_for_triple(opt_.targetTriple, opt_.sysroot);

        plan_.signature = make_signature(plan_, opt_, tc);
#endif

        if (!opt_.targetTriple.empty())
        {
          const std::string gcc = opt_.targetTriple + "-gcc";
          const std::string gxx = opt_.targetTriple + "-g++";

          if (!util::executable_on_path(gcc) || !util::executable_on_path(gxx))
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

        if (verboseMode && !opt_.quiet)
        {
          if (artifact_cache::ArtifactCache::exists(projectArtifact))
            step("artifact cache: hit -> " + projectArtifact.root.string());
          else
            step("artifact cache: miss -> " + projectArtifact.root.string());
        }

        const auto globalPackages = build::load_global_packages();
        const std::string globalPackagesCMake =
            build::make_global_packages_cmake(globalPackages);

        if (!write_if_different(globalPackagesFile, globalPackagesCMake))
        {
          error("Failed to write global packages file: " + globalPackagesFile.string());
          hint("Check filesystem permissions.");
          return 1;
        }

        if (verboseMode && !opt_.quiet)
        {
          out.print("  Using project directory:\n");
          out.print("    • " + plan_.projectDir.string() + "\n\n");
        }

        if (!opt_.targetTriple.empty())
        {
          tc = build::toolchain_contents_for_triple(opt_.targetTriple, opt_.sysroot);
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
            out.print("  Configuring " + plan_.projectDir.filename().string() +
                      " (" + plan_.preset.name + ")\n");

            if (plan_.launcher)
              out.print("    • compiler cache: " + *plan_.launcher + "\n");
            if (plan_.fastLinkerFlag)
              out.print("    • fast linker: " + *plan_.fastLinkerFlag + "\n");

            for (const auto &kv : plan_.cmakeVars)
              out.print("    • " + kv.first + "=" + kv.second + "\n");

            out.print("\n");
          }

          const auto t0 = std::chrono::steady_clock::now();
          const auto argv = build::cmake_configure_argv(plan_, opt_);

          const process::ExecResult r = build::run_process_live_to_log(
              argv,
              {},
              plan_.configureLog,
              (opt_.quiet || !verboseMode),
              opt_.cmakeVerbose,
              /*progressOnly=*/false);

          if (r.exitCode != 0)
          {
            out.discard();

            const std::string log = util::read_text_file_or_empty(plan_.configureLog);
            const bool handled = vix::cli::ErrorHandler::printBuildErrors(
                log,
                plan_.projectDir / "CMakeLists.txt",
                "CMake configure failed");

            if (!opt_.quiet && !handled)
            {
              util::log_hint_if(opt_.quiet, "Command:");
              step(r.displayCommand);
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

          if (opt_.quiet)
          {
            success("Configured in " + util::format_seconds(ms));
          }
          else if (verboseMode)
          {
            out.print(PAD + std::string(GREEN) + "✔ Configured in " + RESET +
                      util::format_seconds(ms) + "\n\n");
          }
        }
        else
        {
          if (verboseMode && !opt_.quiet)
          {
            out.print("  Using existing configuration (cache-friendly).\n");
            out.print("    • " + plan_.buildDir.string() + "\n\n");
          }
        }

        if (opt_.fast && build::ninja_is_up_to_date(opt_, plan_))
        {
          if (!persist_project_artifact(projectArtifact) && !opt_.quiet)
            hint("Warning: unable to persist artifact cache metadata");

          if (!opt_.quiet)
          {
            if (configuredThisRun)
              util::ok_line(std::cout, "Configured");
            util::ok_line(std::cout, "Up to date");
            util::ok_line(std::cout, "Done");
          }
          return 0;
        }

        {
          if (verboseMode && !opt_.quiet)
          {
            out.print("  Building " + plan_.projectDir.filename().string() + " [" +
                      plan_.preset.name + "]\n");
            out.flush_to_stdout();
          }

          const auto t0 = std::chrono::steady_clock::now();
          const auto argv = build::cmake_build_argv(plan_, opt_);
          const auto env = build::ninja_env(opt_, plan_);

          const process::ExecResult r = build::run_process_live_to_log(
              argv,
              env,
              plan_.buildLog,
              /*quiet=*/opt_.quiet,
              /*cmakeVerbose=*/opt_.cmakeVerbose,
              /*progressOnly=*/!verboseMode);

          const auto ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();

          totalMs += ms;

          if (r.exitCode != 0)
          {
            out.discard();

            const std::string log = util::read_text_file_or_empty(plan_.buildLog);
            const bool handled = vix::cli::ErrorHandler::printBuildErrors(
                log,
                plan_.projectDir / "CMakeLists.txt",
                "Build failed");

            if (!opt_.quiet && !handled)
            {
              util::log_hint_if(opt_.quiet, "Command:");
              step(r.displayCommand);
            }

            return (r.exitCode == 0) ? 3 : r.exitCode;
          }

          if (!persist_project_artifact(projectArtifact) && !opt_.quiet)
            hint("Warning: unable to persist artifact cache metadata");

          const std::string buildLog = util::read_text_file_or_empty(plan_.buildLog);
          const std::size_t builtTargets = count_built_targets_from_log(buildLog);

          if (opt_.quiet)
          {
            success("Finished in " + util::format_seconds(totalMs));
          }
          else if (verboseMode)
          {
            const std::string profile =
                (plan_.preset.buildType == "Release")
                    ? "release [optimized]"
                    : "dev [unoptimized + debuginfo]";

            out.print(PAD + std::string(GREEN) + "Finished" + RESET + " " +
                      profile + " in " + util::format_seconds(ms) + "\n\n");
          }
          else
          {
            if (configuredThisRun)
              util::ok_line(std::cout, "Configured");

            if (builtTargets > 0)
              util::ok_line(std::cout, "Built (" + std::to_string(builtTargets) + " targets)");
            else
              util::ok_line(std::cout, "Built");

            util::ok_line(std::cout, "Done in " + util::format_seconds(totalMs));
          }
        }

        if (opt_.exportBin || !opt_.outPath.empty())
        {
          const auto exeOpt = resolve_main_executable(
              plan_.buildDir,
              plan_.projectDir,
              opt_.buildTarget);

          if (!exeOpt)
          {
            error("Unable to resolve the main executable to export.");
            hint("Use --build-target <name> if your project produces multiple executables.");
            return 1;
          }

          fs::path dest;
          if (opt_.exportBin)
          {
            dest = plan_.projectDir / exeOpt->filename();
          }
          else
          {
            dest = fs::absolute(fs::path(opt_.outPath));
          }

          if (!export_built_binary(*exeOpt, dest, opt_.quiet))
            return 1;
        }

        out.flush_to_stdout();
        return 0;

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

    if (!resolve_preset(opt.preset))
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

    if (!opt_.exportBin && opt_.outPath.empty())
    {
      error("Single-file build requires --bin or --out <path>.");
      hint("Example: vix build main.cpp --out app.exe");
      return 2;
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
    if (opt_.exportBin)
      dest = fs::current_path() / exePath.filename();
    else
      dest = fs::absolute(fs::path(opt_.outPath));

    return export_built_binary(exePath, dest, opt_.quiet) ? 0 : 1;
  }

  int help()
  {
    std::ostream &out = std::cout;

    out << "Usage:\n";
    out << "  vix build [options] -- [cmake args...]\n\n";

    out << "Description:\n";
    out << "  Configure and build a CMake project using embedded Vix presets.\n";
    out << "  Ultra-fast loops:\n";
    out << "    • No shell/tee overhead (spawn + pipe)\n";
    out << "    • Strong signature cache (tool versions + cmake file hashes)\n";
    out << "    • Optional fast no-op exit via Ninja dry-run (--fast)\n";
    out << "    • Auto sccache/ccache + mold/lld (auto)\n";
    out << "    • Prepare reusable global artifact metadata cache\n\n";

    out << "Presets (embedded):\n";
    out << "  dev        -> Ninja + Debug   (build-dev)\n";
    out << "  dev-ninja  -> Ninja + Debug   (build-ninja)\n";
    out << "  release    -> Ninja + Release (build-release)\n\n";

    out << "Options:\n";
    out << "  --bin                 Export the built executable to the project root\n";
    out << "  --out <path>          Export the built executable to a specific path\n";
    out << "  --preset <name>       Preset to use (dev, dev-ninja, release)\n";
    out << "  --target <triple>     Cross-compilation target triple (auto toolchain)\n";
    out << "  --sysroot <path>      Sysroot for cross toolchain (optional)\n";
    out << "  --static              Request static linking (VIX_LINK_STATIC=ON)\n";
    out << "  --with-sqlite         Enable SQLite support (VIX_DB_USE_SQLITE=ON)\n";
    out << "  --with-mysql          Enable MySQL support (VIX_DB_USE_MYSQL=ON)\n";
    out << "  -j, --jobs <n>        Parallel build jobs (default: CPU count, clamped)\n";
    out << "  --clean               Remove local build directories and reconfigure from scratch\n";
    out << "  --no-cache            Disable signature cache shortcut\n";
    out << "  --fast                Fast loop: if Ninja says up-to-date, exit immediately\n";
    out << "  --linker <mode>       auto|default|mold|lld (auto prefers mold then lld)\n";
    out << "  --launcher <mode>     auto|none|sccache|ccache (auto prefers sccache)\n";
    out << "  --no-status           Disable NINJA_STATUS progress format\n";
    out << "  --no-up-to-date       Disable Ninja dry-run up-to-date detection\n";
    out << "  -d, --dir <path>      Project directory (where CMakeLists.txt lives)\n";
    out << "  -q, --quiet           Minimal output (still logs to files)\n";
    out << "  -v, --verbose         Show detailed configure and build summary\n";
    out << "  --targets             List detected cross toolchains on PATH\n";
    out << "  --cmake-verbose       Show raw CMake configure output (no summary filtering)\n";
    out << "  --build-target <name> Build only a specific CMake target (ex: blog)\n";
    out << "  -h, --help            Show this help\n\n";

    out << "Environment variables:\n";
    out << "  VIX_BUILD_HEARTBEAT=1 Enable build heartbeat when no output is produced\n";
    out << "                       for several seconds (disabled by default)\n\n";

    out << "Examples:\n";
    out << "  vix build\n";
    out << "  vix build --with-sqlite\n";
    out << "  vix build --with-mysql\n";
    out << "  vix build --preset release --with-sqlite\n";
    out << "  vix build --preset dev-ninja --with-mysql\n";
    out << "  vix build --verbose\n";
    out << "  vix build --fast\n";
    out << "  vix build --preset release\n";
    out << "  vix build --preset release --static\n";
    out << "  vix build --launcher sccache --linker mold\n";
    out << "  vix build --target aarch64-linux-gnu\n";
    out << "  vix build --preset release --target aarch64-linux-gnu\n";
    out << "  vix build --linker lld -- -DVIX_SYNC_BUILD_TESTS=ON\n";
    out << "  vix build --bin\n";
    out << "  vix build --out dist/runner\n";
    out << "  vix build main.cpp --bin\n";
    out << "  vix build main.cpp --out app\n";
    out << "  vix build main.cpp --with-sqlite --out app\n";
    out << "  vix build main.cpp --target x86_64-windows-gnu --out app.exe\n";
    out << "  vix build main.cpp --target aarch64-linux-gnu --out app\n";
    out << "  VIX_BUILD_HEARTBEAT=1 vix build\n";
    out << "  vix build -j 8\n\n";

    out << "Logs:\n";
    out << "  build-dev*/configure.log\n";
    out << "  build-dev*/build.log\n\n";

    return 0;
  }

} // namespace vix::commands::BuildCommand
