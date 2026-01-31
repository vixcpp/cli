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

#include <vix/cli/util/Args.hpp>
#include <vix/cli/util/Console.hpp>
#include <vix/cli/util/Fs.hpp>
#include <vix/cli/util/Hash.hpp>
#include <vix/cli/util/Strings.hpp>
#include <vix/cli/cmake/CMakeBuild.hpp>
#include <vix/cli/cmake/Toolchain.hpp>

namespace fs = std::filesystem;
using namespace vix::cli::style;
namespace process = vix::cli::process;
namespace util = vix::cli::util;
namespace build = vix::cli::build;

namespace vix::commands::BuildCommand
{
  namespace
  {

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

    static std::optional<process::Preset> resolve_preset(const std::string &name)
    {
      const auto presets = builtin_presets();
      auto it = presets.find(name);
      if (it == presets.end())
        return std::nullopt;
      return it->second;
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
          auto targets = build::detect_available_targets();

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

          int jobs = 0;
          try
          {
            jobs = std::stoi(std::string(*v));
          }
          catch (...)
          {
            error("Invalid integer for -j/--jobs: " + std::string(*v));
            exitCode = 2;
            return o;
          }

          o.jobs = jobs;
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
        return util::executable_on_path("sccache") ? std::optional<std::string>("sccache")
                                                   : std::nullopt;
      case process::LauncherMode::Ccache:
        return util::executable_on_path("ccache") ? std::optional<std::string>("ccache")
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
#ifdef _WIN32
      (void)build::run_process_capture({tool, "--version"}, {}, out);
#else
      (void)build::run_process_capture({tool, "--version"}, {}, out);
#endif
      out = util::trim(out);
      if (out.empty())
        return tool + ":<unknown>\n";

      auto pos = out.find('\n');
      if (pos != std::string::npos)
        out = out.substr(0, pos);

      return tool + ":" + out + "\n";
    }

    static bool has_cmake_cache(const fs::path &buildDir)
    {
      return util::file_exists(buildDir / "CMakeCache.txt");
    }

    static std::vector<std::pair<std::string, std::string>>
    build_cmake_vars(
        const process::Preset &p, const process::Options &opt,
        const fs::path &toolchainFile,
        const std::optional<std::string> &launcher,
        const std::optional<std::string> &fastLinkerFlag)
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

      std::sort(vars.begin(), vars.end(),
                [](const auto &a, const auto &b)
                { return a.first < b.first; });

      return vars;
    }

    static std::string make_signature(
        const process::Plan &plan, const process::Options &opt,
        const std::string &toolchainContent)
    {
      std::ostringstream oss;

      // Core config
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

      if (!opt.targetTriple.empty())
      {
        oss << "toolchain:\n";
        oss << toolchainContent;
        if (!toolchainContent.empty() && toolchainContent.back() != '\n')
          oss << "\n";
      }

      return util::trim(oss.str()) + "\n";
    }

    static std::optional<process::Plan> make_plan(const process::Options &opt, const fs::path &cwd)
    {
      fs::path base = cwd;
      if (!opt.dir.empty())
        base = fs::path(opt.dir);

      auto root = util::find_project_root(base);
      if (!root)
        return std::nullopt;

      auto presetOpt = resolve_preset(opt.preset);
      if (!presetOpt)
        return std::nullopt;

      process::Plan plan;
      plan.projectDir = *root;
      plan.preset = *presetOpt;

      plan.launcher = detect_launcher(opt);
      plan.fastLinkerFlag = detect_fast_linker_flag(opt);
      plan.projectFingerprint = util::compute_project_files_fingerprint(plan.projectDir);

      if (!opt.targetTriple.empty())
        plan.buildDir =
            plan.projectDir / (plan.preset.buildDirName + "-" + opt.targetTriple);
      else
        plan.buildDir = plan.projectDir / plan.preset.buildDirName;

      plan.configureLog = plan.buildDir / "configure.log";
      plan.buildLog = plan.buildDir / "build.log";
      plan.sigFile = plan.buildDir / ".vix-config.sig";
      plan.toolchainFile = plan.buildDir / "vix-toolchain.cmake";

      std::string toolchainContent;
      if (!opt.targetTriple.empty())
        toolchainContent =
            build::toolchain_contents_for_triple(opt.targetTriple, opt.sysroot);

      plan.cmakeVars = build_cmake_vars(
          plan.preset, opt, plan.toolchainFile,
          plan.launcher, plan.fastLinkerFlag);
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

    class BuildCommand
    {
    public:
      explicit BuildCommand(process::Options opt) : opt_(std::move(opt)) {}

      int run()
      {
        const fs::path cwd = fs::current_path();

        auto planOpt = make_plan(opt_, cwd);
        if (!planOpt)
        {
          error("Unable to determine the project directory (missing CMakeLists.txt?)");
          hint("Run from your project root, or pass: vix build --dir <path>");
          return 1;
        }

        plan_ = *planOpt;

        const bool defer = (!opt_.quiet && !opt_.cmakeVerbose);
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
            plan_.preset, opt_, plan_.toolchainFile,
            plan_.launcher, plan_.fastLinkerFlag);

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

        if (!opt_.quiet)
        {
          out.print("  Using project directory:\n");
          out.print("    • " + plan_.projectDir.string() + "\n\n");
        }

        if (!opt_.targetTriple.empty())
        {
          tc = build::toolchain_contents_for_triple(opt_.targetTriple, opt_.sysroot);
          if (!util::write_text_file_atomic(plan_.toolchainFile, tc))
          {
            error("Failed to write toolchain file: " + plan_.toolchainFile.string());
            hint("Check filesystem permissions.");
            return 1;
          }
        }

        // CONFIGURE
        if (need_configure(opt_, plan_))
        {
          if (!opt_.quiet)
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
          auto argv = build::cmake_configure_argv(plan_, opt_);

          const process::ExecResult r = build::run_process_live_to_log(
              argv, {}, plan_.configureLog,
              (opt_.quiet || defer),
              opt_.cmakeVerbose,
              /*progressOnly=*/false);

          if (r.exitCode != 0)
          {
            out.discard();

            const std::string log = util::read_text_file_or_empty(plan_.configureLog);
            vix::cli::ErrorHandler::printBuildErrors(
                log,
                plan_.projectDir / "CMakeLists.txt",
                "CMake configure failed");

            if (!opt_.quiet)
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

          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0)
                              .count();

          if (!opt_.quiet)
            out.print(PAD + std::string(GREEN) + "✔ Configured in " + RESET +
                      util::format_seconds(ms) + "\n\n");
          else
            success("Configured in " + util::format_seconds(ms));
        }
        else
        {
          if (!opt_.quiet)
          {
            out.print("  Using existing configuration (cache-friendly).\n");
            out.print("    • " + plan_.buildDir.string() + "\n\n");
          }
          else
          {
            util::log_header_if(opt_.quiet, "Using existing configuration (cache-friendly).");
            util::log_bullet_if(opt_.quiet, plan_.buildDir.string());
          }
        }

        // FAST NO-OP
        if (opt_.fast && build::ninja_is_up_to_date(opt_, plan_))
        {
          if (!opt_.quiet)
          {
            out.print(PAD + std::string(GREEN) + "Up to date" + RESET + " (" +
                      plan_.preset.name + ")\n\n");
            out.flush_to_stdout();
          }
          return 0;
        }

        // BUILD
        {
          if (!opt_.quiet)
            out.print("  Building " + plan_.projectDir.filename().string() + " [" +
                      plan_.preset.name + "]\n");

          if (!opt_.quiet && defer)
            out.flush_to_stdout();

          const auto t0 = std::chrono::steady_clock::now();
          auto argv = build::cmake_build_argv(plan_, opt_);
          auto env = build::ninja_env(opt_, plan_);

          const process::ExecResult r = build::run_process_live_to_log(
              argv, env, plan_.buildLog,
              /*quiet=*/opt_.quiet,
              /*cmakeVerbose=*/opt_.cmakeVerbose,
              /*progressOnly=*/true);

          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0)
                              .count();

          if (r.exitCode != 0)
          {
            out.discard();

            const std::string log = util::read_text_file_or_empty(plan_.buildLog);
            vix::cli::ErrorHandler::printBuildErrors(
                log,
                plan_.projectDir / "CMakeLists.txt",
                "Build failed");

            if (!opt_.quiet)
            {
              util::log_hint_if(opt_.quiet, "Command:");
              step(r.displayCommand);
            }

            return (r.exitCode == 0) ? 3 : r.exitCode;
          }

          const std::string profile = (plan_.preset.buildType == "Release")
                                          ? "release [optimized]"
                                          : "dev [unoptimized + debuginfo]";

          if (!opt_.quiet)
            out.print(PAD + std::string(GREEN) + "Finished" + RESET + " " + profile +
                      " in " + util::format_seconds(ms) + "\n\n");
          else
            success("Finished " + profile + " in " + util::format_seconds(ms));
        }

        out.flush_to_stdout();
        return 0;
      }

    private:
      process::Options opt_;
      process::Plan plan_{};
    };
  } // namespace

  int run(const std::vector<std::string> &args)
  {
    int parseExit = 0;
    process::Options opt = parse_args_or_exit(args, parseExit);

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
    out << "    • Auto sccache/ccache + mold/lld (auto)\n\n";

    out << "Presets (embedded):\n";
    out << "  dev        -> Ninja + Debug   (build-dev)\n";
    out << "  dev-ninja  -> Ninja + Debug   (build-dev-ninja)\n";
    out << "  release    -> Ninja + Release (build-release)\n\n";

    out << "Options:\n";
    out << "  --preset <name>       Preset to use (dev, dev-ninja, release)\n";
    out << "  --target <triple>     Cross-compilation target triple (auto toolchain)\n";
    out << "  --sysroot <path>      Sysroot for cross toolchain (optional)\n";
    out << "  --static              Request static linking (VIX_LINK_STATIC=ON)\n";
    out << "  -j, --jobs <n>        Parallel build jobs (default: CPU count, clamped)\n";
    out << "  --clean               Force reconfigure (ignore cache/signature)\n";
    out << "  --no-cache            Disable signature cache shortcut\n";
    out << "  --fast                Fast loop: if Ninja says up-to-date, exit immediately\n";
    out << "  --linker <mode>       auto|default|mold|lld (auto prefers mold then lld)\n";
    out << "  --launcher <mode>     auto|none|sccache|ccache (auto prefers sccache)\n";
    out << "  --no-status           Disable NINJA_STATUS progress format\n";
    out << "  --no-up-to-date       Disable Ninja dry-run up-to-date detection\n";
    out << "  -d, --dir <path>      Project directory (where CMakeLists.txt lives)\n";
    out << "  -q, --quiet           Minimal output (still logs to files)\n";
    out << "  --targets             List detected cross toolchains on PATH\n";
    out << "  --cmake-verbose       Show raw CMake configure output (no summary filtering)\n";
    out << "  --build-target <name> Build only a specific CMake target (ex: blog)\n";
    out << "  -h, --help            Show this help\n\n";

    out << "Environment variables:\n";
    out << "  VIX_BUILD_HEARTBEAT=1 Enable build heartbeat when no output is produced\n";
    out << "                       for several seconds (disabled by default)\n\n";

    out << "Examples:\n";
    out << "  vix build\n";
    out << "  vix build --fast\n";
    out << "  vix build --preset release\n";
    out << "  vix build --preset release --static\n";
    out << "  vix build --launcher sccache --linker mold\n";
    out << "  vix build --target aarch64-linux-gnu\n";
    out << "  vix build --preset release --target aarch64-linux-gnu\n";
    out << "  vix build --linker lld -- -DVIX_SYNC_BUILD_TESTS=ON\n";
    out << "  VIX_BUILD_HEARTBEAT=1 vix build\n";
    out << "  vix build -j 8\n\n";

    out << "Logs:\n";
    out << "  build-dev*/configure.log\n";
    out << "  build-dev*/build.log\n\n";

    return 0;
  }

} // namespace vix::commands::BuildCommand
