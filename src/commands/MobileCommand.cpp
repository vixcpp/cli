/**
 *
 *  @file MobileCommand.cpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2026, Gaspard Kirira.
 *  All rights reserved.
 *  https://github.com/vixcpp/vix
 *
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Vix.cpp
 *
 */

#include <vix/cli/commands/MobileCommand.hpp>
#include <vix/ui/mobile/AndroidProject.hpp>
#include <vix/ui/mobile/IOSProject.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define VIX_MOB_ISATTY(fd) _isatty(fd)
#define VIX_MOB_FILENO(stream) _fileno(stream)
#else
#include <unistd.h>
#define VIX_MOB_ISATTY(fd) ::isatty(fd)
#define VIX_MOB_FILENO(stream) ::fileno(stream)
#endif

namespace fs = std::filesystem;

namespace
{
  // -------------------------------------------------------------------------
  //  Color handling (ANSI), auto-detected and overridable.
  // -------------------------------------------------------------------------

  struct MobileTheme
  {
    bool color = true;

    std::string_view reset() const { return color ? "\x1b[0m" : ""; }
    std::string_view dim() const { return color ? "\x1b[2m" : ""; }
    std::string_view bold() const { return color ? "\x1b[1m" : ""; }

    std::string_view green() const { return color ? "\x1b[32m" : ""; }
    std::string_view cyan() const { return color ? "\x1b[36m" : ""; }
    std::string_view yellow() const { return color ? "\x1b[33m" : ""; }
    std::string_view red() const { return color ? "\x1b[31m" : ""; }
    std::string_view gray() const { return color ? "\x1b[90m" : ""; }
  };

  bool mob_detect_color(std::ostream &os)
  {
    if (std::getenv("NO_COLOR") != nullptr)
    {
      return false;
    }

    if (std::getenv("FORCE_COLOR") != nullptr)
    {
      return true;
    }

    std::FILE *target = (&os == &std::cerr) ? stderr : stdout;

    return VIX_MOB_ISATTY(VIX_MOB_FILENO(target)) != 0;
  }

  // -------------------------------------------------------------------------
  //  Output verbosity, controlled by CLI flags (modern-runtime style).
  // -------------------------------------------------------------------------

  enum class MobileLogMode
  {
    Normal,
    Quiet,
    Json
  };

  struct MobileReporter
  {
    MobileTheme theme;
    MobileLogMode mode = MobileLogMode::Normal;

    bool normal() const { return mode == MobileLogMode::Normal; }
    bool json() const { return mode == MobileLogMode::Json; }

    void banner(std::string_view title) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << '\n'
                << "  " << theme.green() << theme.bold() << title << theme.reset()
                << '\n';
    }

    void row(std::string_view key, std::string_view value,
             std::string_view valueColor = {}) const
    {
      if (!normal())
      {
        return;
      }

      constexpr std::size_t keyWidth = 9;

      std::string label(key);
      if (label.size() < keyWidth)
      {
        label.append(keyWidth - label.size(), ' ');
      }

      std::cout << "  " << theme.dim() << label << theme.reset() << "  "
                << (valueColor.empty() ? theme.reset() : valueColor)
                << value << theme.reset() << '\n';
    }

    void info_line(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.cyan() << "info" << theme.reset()
                << "  " << message << '\n';
    }

    void step(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.gray() << "\u2022 " << theme.reset()
                << message << '\n';
    }

    void hint(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.gray() << message << theme.reset() << '\n';
    }

    void success(std::string_view message) const
    {
      if (!normal())
      {
        return;
      }

      std::cout << "  " << theme.green() << "\u2714" << theme.reset()
                << " " << message << '\n';
    }

    // Errors always print (even in quiet mode), to stderr.
    void error(std::string_view message) const
    {
      std::cerr << "  " << theme.red() << theme.bold() << "error" << theme.reset()
                << "  " << message << '\n';
    }

    void error_hint(std::string_view message) const
    {
      if (message.empty())
      {
        return;
      }

      std::cerr << "  " << theme.gray() << message << theme.reset() << '\n';
    }

    void event(std::string_view name, std::string_view key = {},
               std::string_view value = {}) const
    {
      if (!json())
      {
        return;
      }

      std::cout << "{\"event\":\"" << name << "\"";

      if (!key.empty())
      {
        std::cout << ",\"" << key << "\":\"" << value << "\"";
      }

      std::cout << "}\n";
      std::cout.flush();
    }
  };

  // -------------------------------------------------------------------------
  //  Output-flag parsing (--quiet / --json / --color), shared by subcommands.
  // -------------------------------------------------------------------------

  bool mob_take_output_flag(const std::string &arg,
                            MobileLogMode &mode,
                            int &colorOverride) // -1 auto, 0 off, 1 on
  {
    if (arg == "--quiet" || arg == "-q" || arg == "--silent")
    {
      mode = MobileLogMode::Quiet;
      return true;
    }

    if (arg == "--json")
    {
      mode = MobileLogMode::Json;
      return true;
    }

    if (arg == "--no-color" || arg == "--no-colour")
    {
      colorOverride = 0;
      return true;
    }

    if (arg == "--color" || arg == "--colour")
    {
      colorOverride = 1;
      return true;
    }

    return false;
  }

  void mob_finalize_reporter(MobileReporter &out, MobileLogMode mode, int colorOverride)
  {
    out.mode = mode;
    out.theme.color =
        (colorOverride == -1) ? mob_detect_color(std::cout) : (colorOverride == 1);
  }

  // -------------------------------------------------------------------------
  //  Option structures
  // -------------------------------------------------------------------------

  struct AndroidMobileOptions
  {
    std::string name{"Vix Mobile"};
    std::string packageName{"com.vixcpp.mobile"};
    std::string url{"http://127.0.0.1:8080"};

    fs::path outputDirectory{"mobile/android"};

    int minSdk{23};
    int targetSdk{36};
    int compileSdk{36};

    int versionCode{1};
    std::string versionName{"1.0.0"};

    std::string androidGradlePluginVersion{"8.13.2"};

    bool force{false};

    MobileLogMode logMode{MobileLogMode::Normal};
    int colorOverride{-1};
  };

  struct IOSMobileOptions
  {
    std::string name{"Vix Mobile"};
    std::string packageName{"com.vixcpp.ios"};
    std::string url{"http://127.0.0.1:8080"};
    std::string versionName{"1.0.0"};
    std::string deploymentTarget{"15.0"};
    fs::path outputDirectory{"mobile/ios"};
    bool force{false};
    MobileLogMode logMode{MobileLogMode::Normal};
    int colorOverride{-1};
  };

  struct IOSProjectCommandOptions
  {
    fs::path projectDirectory{"mobile/ios"};
    std::string packageName{};
    bool release{false};
    MobileLogMode logMode{MobileLogMode::Normal};
    int colorOverride{-1};
  };

  bool is_help_arg(const std::string &arg)
  {
    return arg == "-h" || arg == "--help" || arg == "help";
  }

  bool parse_positive_int(const std::string &value, int &out)
  {
    if (value.empty())
    {
      return false;
    }

    long parsed = 0;

    const char *begin = value.data();
    const char *end = value.data() + value.size();

    auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc{} || result.ptr != end)
    {
      return false;
    }

    if (parsed <= 0 || parsed > 100000000)
    {
      return false;
    }

    out = static_cast<int>(parsed);
    return true;
  }

  bool consume_value(
      const MobileReporter &out,
      const std::vector<std::string> &args,
      std::size_t &index,
      const std::string &option,
      std::string &value)
  {
    if (index + 1 >= args.size())
    {
      out.error("Missing value for " + option + ".");
      return false;
    }

    value = args[++index];

    if (value.empty())
    {
      out.error("Value for " + option + " cannot be empty.");
      return false;
    }

    return true;
  }

  bool parse_prefixed_value(
      const std::string &arg,
      const char *prefix,
      std::string &out)
  {
    const std::string p(prefix);

    if (arg.rfind(p, 0) != 0)
    {
      return false;
    }

    out = arg.substr(p.size());
    return true;
  }

  bool valid_package_part(std::string_view part)
  {
    if (part.empty())
    {
      return false;
    }

    const unsigned char first =
        static_cast<unsigned char>(part.front());

    if (!std::isalpha(first) && part.front() != '_')
    {
      return false;
    }

    for (char ch : part)
    {
      const unsigned char c =
          static_cast<unsigned char>(ch);

      if (std::isalnum(c) || ch == '_')
      {
        continue;
      }

      return false;
    }

    return true;
  }

  bool is_valid_package_name(const std::string &packageName)
  {
    if (packageName.empty())
    {
      return false;
    }

    std::size_t start = 0;
    int parts = 0;

    while (start < packageName.size())
    {
      const std::size_t dot = packageName.find('.', start);

      const std::string_view part =
          dot == std::string::npos
              ? std::string_view(packageName).substr(start)
              : std::string_view(packageName).substr(start, dot - start);

      if (!valid_package_part(part))
      {
        return false;
      }

      ++parts;

      if (dot == std::string::npos)
      {
        break;
      }

      start = dot + 1;
    }

    return parts >= 2;
  }

  bool output_directory_is_safe(
      const fs::path &directory,
      bool force,
      std::string &err)
  {
    err.clear();

    std::error_code ec;

    if (!fs::exists(directory, ec))
    {
      return true;
    }

    if (ec)
    {
      err = "cannot inspect output directory: " +
            directory.string() +
            ": " +
            ec.message();

      return false;
    }

    if (!fs::is_directory(directory, ec))
    {
      err = "output path exists but is not a directory: " +
            directory.string();

      return false;
    }

    if (force)
    {
      return true;
    }

    if (fs::is_empty(directory, ec) && !ec)
    {
      return true;
    }

    err = "output directory already exists and is not empty: " +
          directory.string();

    return false;
  }

  bool is_android_project_directory(const fs::path &directory)
  {
    std::error_code ec;

    return fs::exists(directory / "app" / "build.gradle", ec) && !ec;
  }

  fs::path default_android_project_directory()
  {
    if (is_android_project_directory(fs::path(".")))
    {
      return fs::path(".");
    }

    if (is_android_project_directory(fs::path("mobile") / "android"))
    {
      return fs::path("mobile") / "android";
    }

    return fs::path("mobile") / "android";
  }

  bool android_project_has_gradle_wrapper(const fs::path &projectDirectory)
  {
#ifdef _WIN32
    const fs::path wrapper = projectDirectory / "gradlew.bat";
#else
    const fs::path wrapper = projectDirectory / "gradlew";
#endif

    std::error_code ec;

    return fs::exists(wrapper, ec) && !ec;
  }

  struct AndroidProjectCommandOptions
  {
    fs::path projectDirectory{default_android_project_directory()};
    bool projectDirectoryExplicit{false};

    std::string packageName{};
    std::string gradleCommand{};
    std::string gradleVersion{"8.14.4"};
    std::string distributionType{"bin"};

    bool release{false};
    bool skipInstall{false};
    bool force{false};

    MobileLogMode logMode{MobileLogMode::Normal};
    int colorOverride{-1};
  };

  void resolve_android_project_directory(AndroidProjectCommandOptions &options)
  {
    if (options.projectDirectoryExplicit)
    {
      return;
    }

    options.projectDirectory = default_android_project_directory();
  }

  bool read_text_file(
      const fs::path &path,
      std::string &out,
      std::string &err)
  {
    out.clear();
    err.clear();

    std::ifstream in(path, std::ios::binary);

    if (!in.is_open())
    {
      err = "cannot read file: " + path.string();
      return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    if (in.bad())
    {
      err = "failed while reading file: " + path.string();
      return false;
    }

    out = buffer.str();
    return true;
  }

  std::string shell_quote(const std::string &value)
  {
#ifdef _WIN32
    std::string out = "\"";

    for (char c : value)
    {
      if (c == '"')
      {
        out += "\\\"";
      }
      else
      {
        out.push_back(c);
      }
    }

    out += "\"";
    return out;
#else
    std::string out = "'";

    for (char c : value)
    {
      if (c == '\'')
      {
        out += "'\\''";
      }
      else
      {
        out.push_back(c);
      }
    }

    out += "'";
    return out;
#endif
  }

  std::string gradle_command_for_project(
      const fs::path &projectDirectory,
      const std::string &explicitCommand)
  {
    if (!explicitCommand.empty())
    {
      return explicitCommand;
    }

#ifdef _WIN32
    const fs::path wrapper = projectDirectory / "gradlew.bat";
#else
    const fs::path wrapper = projectDirectory / "gradlew";
#endif

    std::error_code ec;

    if (fs::exists(wrapper, ec) && !ec)
    {
#ifdef _WIN32
      return "gradlew.bat";
#else
      return "./gradlew";
#endif
    }

#ifdef _WIN32
    return "gradle.bat";
#else
    return "gradle";
#endif
  }

  int run_system_command(const std::string &command)
  {
    const int code = std::system(command.c_str());

    if (code == 0)
    {
      return 0;
    }

    return 1;
  }

#if defined(__APPLE__)
  std::string capture_system_command(const std::string &command)
  {
    std::string output;
    std::FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr)
    {
      return output;
    }

    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
      output += buffer;
    }

    (void)pclose(pipe);
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r'))
    {
      output.pop_back();
    }
    return output;
  }
#endif

  int run_android_devices(const MobileReporter &out)
  {
    out.info_line("Connected Android devices:");

    const int result = run_system_command("adb devices");

    if (result != 0)
    {
      out.error("Unable to list Android devices.");
      out.error_hint("Make sure adb is installed and available in PATH.");
      return result;
    }

    return 0;
  }

  std::string make_gradle_command(
      const fs::path &projectDirectory,
      const std::string &task,
      const std::string &explicitGradleCommand = {})
  {
    const std::string gradle =
        gradle_command_for_project(projectDirectory, explicitGradleCommand);

    std::ostringstream cmd;

    cmd << "cd "
        << shell_quote(projectDirectory.string())
        << " && "
        << gradle
        << " "
        << task;

    return cmd.str();
  }

  bool extract_quoted_value_after_key(
      const std::string &text,
      const std::string &key,
      std::string &out)
  {
    out.clear();

    const std::size_t keyPos = text.find(key);

    if (keyPos == std::string::npos)
    {
      return false;
    }

    std::size_t pos = keyPos + key.size();

    while (pos < text.size() &&
           (text[pos] == ' ' ||
            text[pos] == '\t' ||
            text[pos] == '\n' ||
            text[pos] == '\r'))
    {
      ++pos;
    }

    // Gradle commonly uses `key "value"`, while Xcode project files use
    // `KEY = "value"`. Support both forms for the generated projects.
    if (pos < text.size() && text[pos] == '=')
    {
      ++pos;
      while (pos < text.size() &&
             (text[pos] == ' ' ||
              text[pos] == '\t' ||
              text[pos] == '\n' ||
              text[pos] == '\r'))
      {
        ++pos;
      }
    }

    if (pos >= text.size() ||
        (text[pos] != '\'' && text[pos] != '"'))
    {
      return false;
    }

    const char quote = text[pos++];

    const std::size_t end = text.find(quote, pos);

    if (end == std::string::npos)
    {
      return false;
    }

    out = text.substr(pos, end - pos);
    return !out.empty();
  }

  bool resolve_android_package_name(
      const fs::path &projectDirectory,
      std::string &packageName,
      std::string &err)
  {
    err.clear();

    if (!packageName.empty())
    {
      return true;
    }

    const fs::path buildFile =
        projectDirectory / "app" / "build.gradle";

    std::string content;

    if (!read_text_file(buildFile, content, err))
    {
      return false;
    }

    if (extract_quoted_value_after_key(
            content,
            "applicationId",
            packageName))
    {
      return true;
    }

    if (extract_quoted_value_after_key(
            content,
            "namespace",
            packageName))
    {
      return true;
    }

    err =
        "cannot resolve Android package name from " +
        buildFile.string();

    return false;
  }

  int parse_android_project_command_options(
      const MobileReporter &out,
      const std::vector<std::string> &args,
      AndroidProjectCommandOptions &options)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (is_help_arg(arg))
      {
        return 2;
      }

      if (mob_take_output_flag(arg, options.logMode, options.colorOverride))
      {
        continue;
      }

      if (arg == "--project")
      {
        std::string value;

        if (!consume_value(out, args, i, "--project", value))
        {
          return 1;
        }

        options.projectDirectory = value;
        options.projectDirectoryExplicit = true;
        continue;
      }

      if (arg == "--package")
      {
        if (!consume_value(out, args, i, "--package", options.packageName))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--gradle")
      {
        if (!consume_value(out, args, i, "--gradle", options.gradleCommand))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--release")
      {
        options.release = true;
        continue;
      }

      if (arg == "--debug")
      {
        options.release = false;
        continue;
      }

      if (arg == "--no-install")
      {
        options.skipInstall = true;
        continue;
      }

      if (arg == "--gradle-version")
      {
        if (!consume_value(out, args, i, "--gradle-version", options.gradleVersion))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--distribution-type")
      {
        if (!consume_value(out, args, i, "--distribution-type", options.distributionType))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--force")
      {
        options.force = true;
        continue;
      }

      std::string value;

      if (parse_prefixed_value(arg, "--project=", value))
      {
        options.projectDirectory = value;
        options.projectDirectoryExplicit = true;
        continue;
      }

      if (parse_prefixed_value(arg, "--package=", value))
      {
        options.packageName = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--gradle=", value))
      {
        options.gradleCommand = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--gradle-version=", value))
      {
        options.gradleVersion = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--distribution-type=", value))
      {
        options.distributionType = value;
        continue;
      }

      out.error("Unexpected mobile android argument: " + arg);
      out.error_hint("Usage: vix mobile build android [--project mobile/android]");
      out.error_hint("Usage: vix mobile run android [--project mobile/android]");

      return 1;
    }

    if (options.gradleVersion.empty())
    {
      out.error("Gradle wrapper version cannot be empty.");
      return 1;
    }

    if (options.distributionType != "bin" &&
        options.distributionType != "all")
    {
      out.error("Invalid Gradle wrapper distribution type.");
      out.error_hint("Allowed values: bin, all");
      return 1;
    }

    return 0;
  }

  int run_build_android(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);

    AndroidProjectCommandOptions options;

    const int parsed =
        parse_android_project_command_options(out, args, options);

    if (parsed == 2)
    {
      return vix::commands::MobileCommand::help();
    }

    if (parsed != 0)
    {
      return parsed;
    }

    mob_finalize_reporter(out, options.logMode, options.colorOverride);

    resolve_android_project_directory(options);

    const fs::path appBuildFile =
        options.projectDirectory / "app" / "build.gradle";

    std::error_code ec;

    if (!fs::exists(appBuildFile, ec) || ec)
    {
      out.error("Android mobile project not found.");
      out.error_hint("Expected file: " + appBuildFile.string());
      out.error_hint("Run: vix mobile init android --name \"My App\" --url https://example.com");

      return 1;
    }

    out.event("build_start", "project", options.projectDirectory.string());

    out.banner("Vix Mobile \u00b7 build");
    out.row("project", options.projectDirectory.string());
    out.row("variant", options.release ? "release" : "debug");

    vix::ui::AndroidProject project;
    if (!options.gradleCommand.empty())
    {
      project.set_gradle_command(options.gradleCommand);
    }

    const vix::ui::Result<fs::path> result = project.build(
        options.projectDirectory,
        options.release
            ? vix::ui::AndroidBuildType::Release
            : vix::ui::AndroidBuildType::Debug,
        vix::ui::AndroidArtifact::Apk);

    if (result.is_failed())
    {
      out.error("Android mobile build failed.");
      out.error_hint(result.error_message());
      return 1;
    }

    out.event("build_done", "apk", result.value().string());
    out.success("Android mobile build completed.");
    out.row("apk", result.value().string(), out.theme.cyan());

    return 0;
  }

  int run_wrapper_android(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);

    AndroidProjectCommandOptions options;

    const int parsed =
        parse_android_project_command_options(out, args, options);

    if (parsed == 2)
    {
      return vix::commands::MobileCommand::help();
    }

    if (parsed != 0)
    {
      return parsed;
    }

    mob_finalize_reporter(out, options.logMode, options.colorOverride);

    resolve_android_project_directory(options);

    const fs::path appBuildFile =
        options.projectDirectory / "app" / "build.gradle";

    std::error_code ec;

    if (!fs::exists(appBuildFile, ec) || ec)
    {
      out.error("Android mobile project not found.");
      out.error_hint("Expected file: " + appBuildFile.string());
      out.error_hint("Run: vix mobile init android --name \"My App\" --url https://example.com");

      return 1;
    }

    if (android_project_has_gradle_wrapper(options.projectDirectory) &&
        !options.force)
    {
      out.success("Gradle wrapper already exists.");
      out.row("project", options.projectDirectory.string());
      out.hint("Use --force to regenerate the wrapper.");

      return 0;
    }

    std::ostringstream task;

    task
        << "wrapper"
        << " --gradle-version "
        << shell_quote(options.gradleVersion)
        << " --distribution-type "
        << shell_quote(options.distributionType);

    out.banner("Vix Mobile \u00b7 wrapper");
    out.row("project", options.projectDirectory.string());
    out.row("gradle", options.gradleVersion);

    const int result =
        run_system_command(
            make_gradle_command(
                options.projectDirectory,
                task.str(),
                options.gradleCommand));

    if (result != 0)
    {
      out.error("Gradle wrapper generation failed.");
      out.error_hint("The Gradle wrapper requires Gradle to be installed once.");
      out.error_hint("Install Gradle, add it to PATH, or pass --gradle <command>.");
      out.error_hint("Example: vix mobile wrapper android --gradle gradle");
      return result;
    }

    out.event("wrapper_done", "project", options.projectDirectory.string());
    out.success("Gradle wrapper generated.");
    out.step((options.projectDirectory / "gradlew").string());
    out.step((options.projectDirectory / "gradle" / "wrapper").string());

    out.hint("Next: vix mobile build android --project " + options.projectDirectory.string());

    return 0;
  }

  int run_run_android(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);

    AndroidProjectCommandOptions options;

    const int parsed =
        parse_android_project_command_options(out, args, options);

    if (parsed == 2)
    {
      return vix::commands::MobileCommand::help();
    }

    if (parsed != 0)
    {
      return parsed;
    }

    mob_finalize_reporter(out, options.logMode, options.colorOverride);

    resolve_android_project_directory(options);

    const fs::path appBuildFile =
        options.projectDirectory / "app" / "build.gradle";

    std::error_code ec;

    if (!fs::exists(appBuildFile, ec) || ec)
    {
      out.error("Android mobile project not found.");
      out.error_hint("Expected file: " + appBuildFile.string());
      out.error_hint("Run: vix mobile init android --name \"My App\" --url https://example.com");

      return 1;
    }

    std::string err;

    if (!resolve_android_package_name(
            options.projectDirectory,
            options.packageName,
            err))
    {
      out.error("Unable to resolve Android package name.");
      out.error_hint(err.empty() ? "Pass --package <name>." : err);
      return 1;
    }

    const std::string installTask =
        options.release
            ? ":app:installRelease"
            : ":app:installDebug";

    out.banner("Vix Mobile \u00b7 run");
    out.row("project", options.projectDirectory.string());
    out.row("package", options.packageName, out.theme.cyan());
    out.row("variant", options.release ? "release" : "debug");

    if (!options.skipInstall)
    {
      out.info_line("Installing Android mobile shell...");

      const int installResult =
          run_system_command(
              make_gradle_command(
                  options.projectDirectory,
                  installTask,
                  options.gradleCommand));

      if (installResult != 0)
      {
        out.error("Android mobile install failed.");
        out.error_hint("Make sure Gradle, Android SDK, and adb are installed.");
        out.error_hint("Make sure an Android device or emulator is connected.");
        out.error_hint("You can pass --gradle <command> if Gradle is not in PATH.");
        return installResult;
      }
    }

    out.info_line("Launching Android mobile shell...");

    std::ostringstream launchCommand;

    launchCommand
        << "adb shell am start -n "
        << shell_quote(options.packageName + "/.MainActivity");

    const int launchResult =
        run_system_command(launchCommand.str());

    if (launchResult != 0)
    {
      out.error("Android mobile launch failed.");
      out.error_hint("Make sure adb is installed and a device is connected.");
      return launchResult;
    }

    out.event("launched", "package", options.packageName);
    out.success("Android mobile shell launched.");
    out.row("package", options.packageName, out.theme.cyan());
    return 0;
  }

  int parse_ios_init_options(
      const MobileReporter &out,
      const std::vector<std::string> &args,
      IOSMobileOptions &options)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];
      if (is_help_arg(arg))
      {
        return 2;
      }
      if (mob_take_output_flag(arg, options.logMode, options.colorOverride))
      {
        continue;
      }

      if (arg == "--name")
      {
        if (!consume_value(out, args, i, "--name", options.name))
        {
          return 1;
        }
        continue;
      }
      if (arg == "--package")
      {
        if (!consume_value(out, args, i, "--package", options.packageName))
        {
          return 1;
        }
        continue;
      }
      if (arg == "--url")
      {
        if (!consume_value(out, args, i, "--url", options.url))
        {
          return 1;
        }
        continue;
      }
      if (arg == "--version-name")
      {
        if (!consume_value(out, args, i, "--version-name", options.versionName))
        {
          return 1;
        }
        continue;
      }
      if (arg == "--deployment-target")
      {
        if (!consume_value(
                out, args, i, "--deployment-target", options.deploymentTarget))
        {
          return 1;
        }
        continue;
      }

      if (arg == "--output" || arg == "-o")
      {
        std::string value;
        if (!consume_value(out, args, i, arg, value))
        {
          return 1;
        }
        options.outputDirectory = value;
        continue;
      }

      if (arg == "--force")
      {
        options.force = true;
        continue;
      }

      std::string value;
      if (parse_prefixed_value(arg, "--name=", value))
      {
        options.name = value;
      }
      else if (parse_prefixed_value(arg, "--package=", value))
      {
        options.packageName = value;
      }
      else if (parse_prefixed_value(arg, "--url=", value))
      {
        options.url = value;
      }
      else if (parse_prefixed_value(arg, "--version-name=", value))
      {
        options.versionName = value;
      }
      else if (parse_prefixed_value(arg, "--deployment-target=", value))
      {
        options.deploymentTarget = value;
      }
      else if (parse_prefixed_value(arg, "--output=", value))
      {
        options.outputDirectory = value;
      }
      else
      {
        out.error("Unexpected mobile init ios argument: " + arg);
        out.error_hint("Usage: vix mobile init ios --name \"My App\" --url https://example.com");
        return 1;
      }
    }

    return 0;
  }

  int run_init_ios(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);
    IOSMobileOptions options;
    const int parsed = parse_ios_init_options(out, args, options);
    if (parsed == 2)
    {
      return vix::commands::MobileCommand::help();
    }
    if (parsed != 0)
    {
      return parsed;
    }

    mob_finalize_reporter(out, options.logMode, options.colorOverride);
    std::string error;
    if (!output_directory_is_safe(
            options.outputDirectory, options.force, error))
    {
      out.error(error);
      out.error_hint("Use --force to overwrite generated files in this directory.");
      return 1;
    }

    vix::ui::MobileConfig config;
    config.set_name(options.name)
        .set_app_id(options.packageName)
        .set_version(options.versionName)
        .set_url(options.url);
    vix::ui::IOSProject project{
        vix::ui::MobileProject(std::move(config))};
    project.set_deployment_target(options.deploymentTarget);

    const vix::ui::Result<void> generated =
        project.generate(options.outputDirectory);
    if (generated.is_failed())
    {
      out.error("Failed to generate iOS mobile shell.");
      out.error_hint(generated.error_message());
      return 1;
    }

    out.event("generated", "out", options.outputDirectory.string());
    out.banner("Vix Mobile · iOS");
    out.row("app", options.name);
    out.row("out", options.outputDirectory.string(), out.theme.cyan());
    out.row("url", options.url, out.theme.cyan());
    out.success("iOS mobile shell generated.");
    out.hint("Next: vix mobile build ios --project " +
             options.outputDirectory.string());
    return 0;
  }

  int parse_ios_project_command_options(
      const MobileReporter &out,
      const std::vector<std::string> &args,
      IOSProjectCommandOptions &options)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];
      if (is_help_arg(arg))
      {
        return 2;
      }
      if (mob_take_output_flag(arg, options.logMode, options.colorOverride))
      {
        continue;
      }
      if (arg == "--project")
      {
        std::string value;
        if (!consume_value(out, args, i, "--project", value))
        {
          return 1;
        }
        options.projectDirectory = value;
        continue;
      }
      if (arg == "--package")
      {
        if (!consume_value(out, args, i, "--package", options.packageName))
        {
          return 1;
        }
        continue;
      }
      if (arg == "--release")
      {
        options.release = true;
        continue;
      }
      if (arg == "--debug")
      {
        options.release = false;
        continue;
      }

      std::string value;
      if (parse_prefixed_value(arg, "--project=", value))
      {
        options.projectDirectory = value;
      }
      else if (parse_prefixed_value(arg, "--package=", value))
      {
        options.packageName = value;
      }
      else
      {
        out.error("Unexpected mobile ios argument: " + arg);
        out.error_hint("Usage: vix mobile build ios [--project mobile/ios]");
        return 1;
      }
    }
    return 0;
  }

  bool resolve_ios_bundle_identifier(
      const fs::path &directory,
      std::string &bundle,
      std::string &error)
  {
    if (!bundle.empty())
    {
      return true;
    }

    std::error_code filesystem_error;
    for (fs::directory_iterator iterator(directory, filesystem_error);
         !filesystem_error && iterator != fs::directory_iterator();
         iterator.increment(filesystem_error))
    {
      if (iterator->path().extension() != ".xcodeproj")
      {
        continue;
      }

      std::string content;
      if (!read_text_file(iterator->path() / "project.pbxproj", content, error))
      {
        return false;
      }

      if (extract_quoted_value_after_key(
              content, "PRODUCT_BUNDLE_IDENTIFIER", bundle))
      {
        return true;
      }
    }

    error = "cannot resolve iOS bundle identifier from generated Xcode project";
    return false;
  }

  int run_build_ios(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);
    IOSProjectCommandOptions options;
    const int parsed = parse_ios_project_command_options(out, args, options);
    if (parsed == 2)
    {
      return vix::commands::MobileCommand::help();
    }
    if (parsed != 0)
    {
      return parsed;
    }

    mob_finalize_reporter(out, options.logMode, options.colorOverride);
    out.event("build_start", "project", options.projectDirectory.string());
    vix::ui::IOSProject project;
    const vix::ui::Result<fs::path> result = project.build(
        options.projectDirectory,
        options.release ? vix::ui::IOSBuildType::Release :
                          vix::ui::IOSBuildType::Debug);
    if (result.is_failed())
    {
      out.error("iOS mobile build failed.");
      out.error_hint(result.error_message());
      return 1;
    }

    out.event("build_done", "app", result.value().string());
    out.success("iOS mobile build completed.");
    out.row("app", result.value().string(), out.theme.cyan());
    return 0;
  }

  int run_run_ios(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);
    IOSProjectCommandOptions options;
    const int parsed = parse_ios_project_command_options(out, args, options);
    if (parsed == 2)
    {
      return vix::commands::MobileCommand::help();
    }
    if (parsed != 0)
    {
      return parsed;
    }

    mob_finalize_reporter(out, options.logMode, options.colorOverride);
#if !defined(__APPLE__)
    out.error("iOS mobile run requires macOS, Xcode, and an iOS Simulator.");
    return 1;
#else
    vix::ui::IOSProject project;
    const vix::ui::Result<fs::path> built = project.build(
        options.projectDirectory,
        options.release ? vix::ui::IOSBuildType::Release :
                          vix::ui::IOSBuildType::Debug);
    if (built.is_failed())
    {
      out.error("iOS mobile build failed.");
      out.error_hint(built.error_message());
      return 1;
    }

    std::string bundle;
    std::string error;
    if (!resolve_ios_bundle_identifier(options.projectDirectory, bundle, error))
    {
      out.error("Unable to resolve iOS bundle identifier.");
      out.error_hint(error);
      return 1;
    }

    const std::string simulator = capture_system_command(
        "xcrun simctl list devices booted | "
        "sed -n 's/.*(\\([0-9A-Fa-f-]*\\)) (Booted).*/\\1/p' | head -n 1");
    if (simulator.empty())
    {
      out.error("No booted iOS Simulator is available.");
      out.error_hint("Boot an iOS Simulator with Xcode and try again.");
      return 1;
    }

    const int install_result = run_system_command(
        "xcrun simctl install " + shell_quote(simulator) + " " +
        shell_quote(built.value().string()));
    if (install_result != 0)
    {
      out.error("Unable to install the iOS application in the Simulator.");
      return install_result;
    }

    const std::string launched = capture_system_command(
        "xcrun simctl launch " + shell_quote(simulator) + " " +
        shell_quote(bundle));
    if (launched.empty())
    {
      out.error("Unable to launch the iOS application in the Simulator.");
      return 1;
    }

    out.event("launched", "bundle", bundle);
    out.success("iOS mobile shell launched.");
    out.row("bundle", bundle, out.theme.cyan());
    return 0;
#endif
  }

  int parse_android_init_options(
      const MobileReporter &out,
      const std::vector<std::string> &args,
      AndroidMobileOptions &options)
  {
    for (std::size_t i = 0; i < args.size(); ++i)
    {
      const std::string &arg = args[i];

      if (is_help_arg(arg))
      {
        return 2;
      }

      if (mob_take_output_flag(arg, options.logMode, options.colorOverride))
      {
        continue;
      }

      if (arg == "--name")
      {
        if (!consume_value(out, args, i, "--name", options.name))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--url")
      {
        if (!consume_value(out, args, i, "--url", options.url))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--package")
      {
        if (!consume_value(out, args, i, "--package", options.packageName))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--output" || arg == "-o")
      {
        std::string value;

        if (!consume_value(out, args, i, arg, value))
        {
          return 1;
        }

        options.outputDirectory = value;
        continue;
      }

      if (arg == "--min-sdk")
      {
        std::string value;

        if (!consume_value(out, args, i, "--min-sdk", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.minSdk))
        {
          out.error("Invalid --min-sdk value.");
          return 1;
        }

        continue;
      }

      if (arg == "--target-sdk")
      {
        std::string value;

        if (!consume_value(out, args, i, "--target-sdk", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.targetSdk))
        {
          out.error("Invalid --target-sdk value.");
          return 1;
        }

        continue;
      }

      if (arg == "--compile-sdk")
      {
        std::string value;

        if (!consume_value(out, args, i, "--compile-sdk", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.compileSdk))
        {
          out.error("Invalid --compile-sdk value.");
          return 1;
        }

        continue;
      }

      if (arg == "--version-code")
      {
        std::string value;

        if (!consume_value(out, args, i, "--version-code", value))
        {
          return 1;
        }

        if (!parse_positive_int(value, options.versionCode))
        {
          out.error("Invalid --version-code value.");
          return 1;
        }

        continue;
      }

      if (arg == "--version-name")
      {
        if (!consume_value(out, args, i, "--version-name", options.versionName))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--agp")
      {
        if (!consume_value(out, args, i, "--agp", options.androidGradlePluginVersion))
        {
          return 1;
        }

        continue;
      }

      if (arg == "--force")
      {
        options.force = true;
        continue;
      }

      std::string value;

      if (parse_prefixed_value(arg, "--name=", value))
      {
        options.name = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--url=", value))
      {
        options.url = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--package=", value))
      {
        options.packageName = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--output=", value))
      {
        options.outputDirectory = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--min-sdk=", value))
      {
        if (!parse_positive_int(value, options.minSdk))
        {
          out.error("Invalid --min-sdk value.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--target-sdk=", value))
      {
        if (!parse_positive_int(value, options.targetSdk))
        {
          out.error("Invalid --target-sdk value.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--compile-sdk=", value))
      {
        if (!parse_positive_int(value, options.compileSdk))
        {
          out.error("Invalid --compile-sdk value.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--version-code=", value))
      {
        if (!parse_positive_int(value, options.versionCode))
        {
          out.error("Invalid --version-code value.");
          return 1;
        }

        continue;
      }

      if (parse_prefixed_value(arg, "--version-name=", value))
      {
        options.versionName = value;
        continue;
      }

      if (parse_prefixed_value(arg, "--agp=", value))
      {
        options.androidGradlePluginVersion = value;
        continue;
      }

      out.error("Unexpected mobile init android argument: " + arg);
      out.error_hint("Usage: vix mobile init android --name \"My App\" --url https://example.com");
      return 1;
    }

    if (options.url.empty())
    {
      out.error("Mobile app URL cannot be empty.");
      return 1;
    }

    if (!is_valid_package_name(options.packageName))
    {
      out.error("Invalid Android package name: " + options.packageName);
      out.error_hint("Example: com.softadastra.app");
      return 1;
    }

    if (options.minSdk > options.targetSdk)
    {
      out.error("Invalid Android SDK configuration.");
      out.error_hint("--min-sdk cannot be greater than --target-sdk.");
      return 1;
    }

    if (options.targetSdk > options.compileSdk)
    {
      out.error("Invalid Android SDK configuration.");
      out.error_hint("--target-sdk cannot be greater than --compile-sdk.");
      return 1;
    }

    return 0;
  }

  int run_init_android(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);

    AndroidMobileOptions options;

    const int parsed =
        parse_android_init_options(out, args, options);

    if (parsed == 2)
    {
      return vix::commands::MobileCommand::help();
    }

    if (parsed != 0)
    {
      return parsed;
    }

    mob_finalize_reporter(out, options.logMode, options.colorOverride);

    std::string err;

    if (!output_directory_is_safe(
            options.outputDirectory,
            options.force,
            err))
    {
      out.error(err);
      out.error_hint("Use --force to overwrite generated files in this directory.");
      return 1;
    }

    vix::ui::MobileConfig config;
    config.set_name(options.name)
        .set_app_id(options.packageName)
        .set_version(options.versionName)
        .set_url(options.url);

    vix::ui::AndroidProject project{
        vix::ui::MobileProject(std::move(config))};
    project.set_min_sdk(options.minSdk)
        .set_target_sdk(options.targetSdk)
        .set_compile_sdk(options.compileSdk)
        .set_version_code(options.versionCode)
        .set_android_gradle_plugin_version(
            options.androidGradlePluginVersion);

    const vix::ui::Result<void> generated =
        project.generate(options.outputDirectory);
    if (generated.is_failed())
    {
      out.error("Failed to generate Android mobile shell.");
      out.error_hint(generated.error_message());
      return 1;
    }

    out.event("generated", "out", options.outputDirectory.string());

    out.banner("Vix Mobile \u00b7 android");
    out.row("app", options.name);
    out.row("out", options.outputDirectory.string(), out.theme.cyan());
    out.row("url", options.url, out.theme.cyan());

    out.success("Android mobile shell generated.");

    out.hint("Next: cd " + options.outputDirectory.string() + " && gradle :app:assembleDebug");
    out.hint("Later this will be wrapped by: vix mobile build android");

    return 0;
  }

}

namespace vix::commands
{
  int MobileCommand::run(const std::vector<std::string> &args)
  {
    MobileReporter out;
    out.theme.color = mob_detect_color(std::cout);

    if (args.empty())
    {
      return help();
    }

    if (is_help_arg(args[0]))
    {
      return help();
    }

    if (args[0] == "init")
    {
      if (args.size() < 2)
      {
        out.error("Missing mobile target.");
        out.error_hint("Usage: vix mobile init <android|ios> --name \"My App\" --url https://example.com");
        return 1;
      }

      if (args[1] == "android")
      {
        std::vector<std::string> rest(args.begin() + 2, args.end());
        return run_init_android(rest);
      }

      if (args[1] == "ios")
      {
        std::vector<std::string> rest(args.begin() + 2, args.end());
        return run_init_ios(rest);
      }

      out.error("Unsupported mobile init target: " + args[1]);
      out.error_hint("Supported targets: android, ios");
      return 1;
    }

    if (args[0] == "android")
    {
      std::vector<std::string> rest(args.begin() + 1, args.end());
      return run_init_android(rest);
    }

    if (args[0] == "ios")
    {
      std::vector<std::string> rest(args.begin() + 1, args.end());
      return run_init_ios(rest);
    }

    if (args[0] == "build")
    {
      std::vector<std::string> rest;

      if (args.size() >= 2 && args[1] == "android")
      {
        rest.assign(args.begin() + 2, args.end());
      }
      else if (args.size() >= 2 && args[1] == "ios")
      {
        rest.assign(args.begin() + 2, args.end());
        return run_build_ios(rest);
      }
      else
      {
        rest.assign(args.begin() + 1, args.end());
      }

      return run_build_android(rest);
    }

    if (args[0] == "devices")
    {
      return run_android_devices(out);
    }

    if (args[0] == "wrapper")
    {
      std::vector<std::string> rest;

      if (args.size() >= 2 && args[1] == "android")
      {
        rest.assign(args.begin() + 2, args.end());
      }
      else if (args.size() >= 2 && args[1] == "ios")
      {
        out.error("Unsupported mobile wrapper target: ios");
        out.error_hint("Supported target: android");
        return 1;
      }
      else
      {
        rest.assign(args.begin() + 1, args.end());
      }

      return run_wrapper_android(rest);
    }

    if (args[0] == "run")
    {
      std::vector<std::string> rest;

      if (args.size() >= 2 && args[1] == "android")
      {
        rest.assign(args.begin() + 2, args.end());
      }
      else if (args.size() >= 2 && args[1] == "ios")
      {
        rest.assign(args.begin() + 2, args.end());
        return run_run_ios(rest);
      }
      else
      {
        rest.assign(args.begin() + 1, args.end());
      }

      return run_run_android(rest);
    }

    out.error("Unknown mobile command: " + args[0]);
    out.error_hint("Usage: vix mobile init <android|ios> --name \"My App\" --url https://example.com");
    return 1;
  }

  int MobileCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix mobile init android [options]\n"
        << "  vix mobile init ios [options]\n"
        << "  vix mobile android [options]\n"
        << "  vix mobile ios [options]\n"
        << "  vix mobile build android [options]\n"
        << "  vix mobile build ios [options]\n"
        << "  vix mobile build [android] [options]\n"
        << "  vix mobile run [android|ios] [options]\n\n"
        << "  vix mobile wrapper [android] [options]\n"
        << "  vix mobile devices\n"
        << "  vix mobile run android [options]\n"
        << "  vix mobile run ios [options]\n\n"
        << "Description:\n"
        << "  Generate a mobile WebView shell for a Vix web or PWA application.\n"
        << "  Generates Android or iOS projects that open a target URL in WebView.\n"
        << "  It does not embed the Vix C++ runtime yet.\n\n"

        << "Required options:\n"
        << "  --name <name>              App display name\n"
        << "  --url <url>                Web/PWA URL opened by the mobile shell\n\n"

        << "Android options:\n"
        << "  --package <name>           Android package name. Default: com.vixcpp.mobile\n"
        << "  --output <dir>, -o <dir>   Output directory. Default: mobile/android\n"
        << "  --min-sdk <n>              Minimum Android SDK. Default: 23\n"
        << "  --target-sdk <n>           Target Android SDK. Default: 36\n"
        << "  --compile-sdk <n>          Compile Android SDK. Default: 36\n"
        << "  --version-code <n>         Android version code. Default: 1\n"
        << "  --version-name <name>      Android version name. Default: 1.0.0\n"
        << "  --agp <version>            Android Gradle Plugin version. Default: 8.13.2\n"
        << "  --gradle <command>         Gradle command to use. Default: ./gradlew or gradle\n"
        << "  --force                    Allow writing into a non-empty output directory\n\n"

        << "iOS options:\n"
        << "  --package <name>           iOS bundle identifier. Default: com.vixcpp.ios\n"
        << "  --output <dir>, -o <dir>   Output directory. Default: mobile/ios\n"
        << "  --version-name <name>      iOS marketing version. Default: 1.0.0\n"
        << "  --deployment-target <ver>  iOS deployment target. Default: 15.0\n\n"

        << "Android wrapper/build/run options:\n"
        << "  --project <dir>            Android project directory. Default: mobile/android\n"
        << "  --package <name>           Android package name used by run\n"
        << "  --debug                    Build/install debug variant (default)\n"
        << "  --release                  Build/install release variant\n"
        << "  --gradle <command>         Gradle command to use. Default: ./gradlew or gradle\n"
        << "  --gradle-version <version> Gradle wrapper version. Default: 8.14.4\n"
        << "  --distribution-type <type> Wrapper distribution type: bin or all. Default: bin\n"
        << "  --no-install               Run without installing first\n\n"

        << "iOS build/run options:\n"
        << "  --project <dir>            iOS project directory. Default: mobile/ios\n"
        << "  --package <name>           Bundle identifier used by run (auto-detected by default)\n"
        << "  --debug                    Build the Debug simulator application (default)\n"
        << "  --release                  Build the Release simulator application\n\n"

        << "Output options:\n"
        << "  --quiet, -q                Only print errors\n"
        << "  --json                     Emit machine-readable lifecycle events\n"
        << "  --no-color                 Disable ANSI colors (also honors NO_COLOR)\n\n"

        << "Examples:\n"
        << "  vix mobile init android --name \"My App\" --url https://example.com\n"
        << "  vix mobile init android --name \"Vix Note\" --url http://192.168.1.10:5179\n"
        << "  vix mobile build android\n"
        << "  vix mobile init ios --name \"My App\" --url https://example.com\n"
        << "  vix mobile build ios\n"
        << "  vix mobile run ios --project mobile/ios\n"
        << "  vix mobile run android\n"
        << "  vix mobile run android --project mobile/android --package com.softadastra.app\n"
        << "  vix mobile wrapper android\n"
        << "  vix mobile wrapper android --gradle-version 8.13\n"
        << "  vix mobile devices\n"
        << "  vix mobile init android --name \"Softadastra\" --package com.softadastra.app --url https://softadastra.com\n";

    return 0;
  }
}
