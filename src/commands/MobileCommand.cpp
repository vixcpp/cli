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
#include <vix/utils/Env.hpp>

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

    bool allowCleartext{false};
    bool force{false};

    MobileLogMode logMode{MobileLogMode::Normal};
    int colorOverride{-1};
  };

  bool is_help_arg(const std::string &arg)
  {
    return arg == "-h" || arg == "--help" || arg == "help";
  }

  bool starts_with(std::string_view value, std::string_view prefix)
  {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
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

  fs::path detect_android_sdk_directory()
  {
    const char *androidHome = vix::utils::vix_getenv("ANDROID_HOME");
    if (androidHome != nullptr && *androidHome != '\0')
    {
      return fs::path(androidHome);
    }

    const char *androidSdkRoot = vix::utils::vix_getenv("ANDROID_SDK_ROOT");
    if (androidSdkRoot != nullptr && *androidSdkRoot != '\0')
    {
      return fs::path(androidSdkRoot);
    }

    const char *home = vix::utils::vix_getenv("HOME");
    if (home != nullptr && *home != '\0')
    {
      fs::path candidate = fs::path(home) / "Android" / "Sdk";

      std::error_code ec;
      if (fs::exists(candidate, ec) && !ec)
      {
        return candidate;
      }
    }

    return {};
  }

  std::string escape_local_properties_path(const fs::path &path)
  {
    std::string value = path.string();
    std::string output;

    for (char c : value)
    {
      if (c == '\\')
      {
        output += "\\\\";
      }
      else if (c == ':')
      {
        output += "\\:";
      }
      else
      {
        output.push_back(c);
      }
    }

    return output;
  }

  std::string render_local_properties()
  {
    const fs::path sdk = detect_android_sdk_directory();

    if (sdk.empty())
    {
      return {};
    }

    return "sdk.dir=" + escape_local_properties_path(sdk) + "\n";
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

  std::string xml_escape(std::string_view value)
  {
    std::string out;

    for (char c : value)
    {
      switch (c)
      {
      case '&':
        out += "&amp;";
        break;

      case '<':
        out += "&lt;";
        break;

      case '>':
        out += "&gt;";
        break;

      case '"':
        out += "&quot;";
        break;

      case '\'':
        out += "\\'";
        break;

      default:
        out.push_back(c);
        break;
      }
    }

    return out;
  }

  std::string java_escape(std::string_view value)
  {
    std::string out;

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
        out.push_back(c);
        break;
      }
    }

    return out;
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

  fs::path java_package_directory(
      const fs::path &javaRoot,
      const std::string &packageName)
  {
    fs::path path = javaRoot;

    std::size_t start = 0;

    while (start < packageName.size())
    {
      const std::size_t dot = packageName.find('.', start);

      const std::string part =
          dot == std::string::npos
              ? packageName.substr(start)
              : packageName.substr(start, dot - start);

      path /= part;

      if (dot == std::string::npos)
      {
        break;
      }

      start = dot + 1;
    }

    return path;
  }

  std::string safe_project_name(std::string value)
  {
    if (value.empty())
    {
      return "VixMobile";
    }

    std::string out;

    for (char ch : value)
    {
      const unsigned char c =
          static_cast<unsigned char>(ch);

      if (std::isalnum(c))
      {
        out.push_back(ch);
      }
      else if (ch == ' ' || ch == '-' || ch == '_')
      {
        out.push_back('_');
      }
    }

    if (out.empty())
    {
      return "VixMobile";
    }

    if (std::isdigit(static_cast<unsigned char>(out.front())))
    {
      out.insert(out.begin(), 'V');
    }

    return out;
  }

  bool write_text_file(
      const fs::path &path,
      const std::string &content,
      std::string &err)
  {
    err.clear();

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    if (ec)
    {
      err = "cannot create directory: " +
            path.parent_path().string() +
            ": " +
            ec.message();

      return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);

    if (!out.is_open())
    {
      err = "cannot write file: " + path.string();
      return false;
    }

    out << content;

    if (!out.good())
    {
      err = "failed while writing file: " + path.string();
      return false;
    }

    return true;
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

  std::string render_settings_gradle(const AndroidMobileOptions &options)
  {
    std::ostringstream out;

    out
        << "pluginManagement {\n"
        << "    repositories {\n"
        << "        google()\n"
        << "        mavenCentral()\n"
        << "        gradlePluginPortal()\n"
        << "    }\n"
        << "}\n\n"
        << "dependencyResolutionManagement {\n"
        << "    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)\n"
        << "    repositories {\n"
        << "        google()\n"
        << "        mavenCentral()\n"
        << "    }\n"
        << "}\n\n"
        << "rootProject.name = '" << safe_project_name(options.name) << "'\n"
        << "include ':app'\n";

    return out.str();
  }

  std::string render_root_build_gradle(
      const AndroidMobileOptions &options)
  {
    std::ostringstream out;

    out
        << "plugins {\n"
        << "    id 'com.android.application' version '"
        << options.androidGradlePluginVersion
        << "' apply false\n"
        << "}\n";

    return out.str();
  }

  std::string render_app_build_gradle(
      const AndroidMobileOptions &options)
  {
    std::ostringstream out;

    out
        << "plugins {\n"
        << "    id 'com.android.application'\n"
        << "}\n\n"
        << "android {\n"
        << "    namespace '" << options.packageName << "'\n"
        << "    compileSdk " << options.compileSdk << "\n\n"
        << "    defaultConfig {\n"
        << "        applicationId '" << options.packageName << "'\n"
        << "        minSdk " << options.minSdk << "\n"
        << "        targetSdk " << options.targetSdk << "\n"
        << "        versionCode " << options.versionCode << "\n"
        << "        versionName '" << options.versionName << "'\n"
        << "    }\n"
        << "}\n";

    return out.str();
  }

  std::string render_manifest(const AndroidMobileOptions &options)
  {
    std::ostringstream out;

    out
        << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
        << "    <uses-permission android:name=\"android.permission.INTERNET\" />\n\n"
        << "    <application\n"
        << "        android:allowBackup=\"true\"\n"
        << "        android:label=\"@string/app_name\"\n"
        << "        android:supportsRtl=\"true\"\n"
        << "        android:theme=\"@style/AppTheme\"";

    if (options.allowCleartext)
    {
      out << "\n        android:usesCleartextTraffic=\"true\"";
    }

    out
        << ">\n"
        << "        <activity\n"
        << "            android:name=\".MainActivity\"\n"
        << "            android:exported=\"true\">\n"
        << "            <intent-filter>\n"
        << "                <action android:name=\"android.intent.action.MAIN\" />\n"
        << "                <category android:name=\"android.intent.category.LAUNCHER\" />\n"
        << "            </intent-filter>\n"
        << "        </activity>\n"
        << "    </application>\n"
        << "</manifest>\n";

    return out.str();
  }

  std::string render_main_activity(const AndroidMobileOptions &options)
  {
    std::ostringstream out;

    out
        << "package " << options.packageName << ";\n\n"
        << "import android.annotation.SuppressLint;\n"
        << "import android.app.Activity;\n"
        << "import android.os.Bundle;\n"
        << "import android.webkit.WebResourceRequest;\n"
        << "import android.webkit.WebSettings;\n"
        << "import android.webkit.WebView;\n"
        << "import android.webkit.WebViewClient;\n\n"
        << "public class MainActivity extends Activity {\n"
        << "    private static final String APP_URL = \""
        << java_escape(options.url)
        << "\";\n\n"
        << "    private WebView webView;\n\n"
        << "    @SuppressLint(\"SetJavaScriptEnabled\")\n"
        << "    @Override\n"
        << "    protected void onCreate(Bundle savedInstanceState) {\n"
        << "        super.onCreate(savedInstanceState);\n\n"
        << "        webView = new WebView(this);\n"
        << "        setContentView(webView);\n\n"
        << "        WebSettings settings = webView.getSettings();\n"
        << "        settings.setJavaScriptEnabled(true);\n"
        << "        settings.setDomStorageEnabled(true);\n"
        << "        settings.setLoadWithOverviewMode(true);\n"
        << "        settings.setUseWideViewPort(true);\n"
        << "        settings.setAllowFileAccess(false);\n"
        << "        settings.setAllowContentAccess(false);\n\n"
        << "        webView.setWebViewClient(new WebViewClient() {\n"
        << "            @Override\n"
        << "            public boolean shouldOverrideUrlLoading(\n"
        << "                    WebView view,\n"
        << "                    WebResourceRequest request) {\n"
        << "                view.loadUrl(request.getUrl().toString());\n"
        << "                return true;\n"
        << "            }\n"
        << "        });\n\n"
        << "        webView.loadUrl(APP_URL);\n"
        << "    }\n\n"
        << "    @Override\n"
        << "    public void onBackPressed() {\n"
        << "        if (webView != null && webView.canGoBack()) {\n"
        << "            webView.goBack();\n"
        << "            return;\n"
        << "        }\n\n"
        << "        super.onBackPressed();\n"
        << "    }\n\n"
        << "    @Override\n"
        << "    protected void onDestroy() {\n"
        << "        if (webView != null) {\n"
        << "            webView.destroy();\n"
        << "            webView = null;\n"
        << "        }\n\n"
        << "        super.onDestroy();\n"
        << "    }\n"
        << "}\n";

    return out.str();
  }

  std::string render_strings_xml(const AndroidMobileOptions &options)
  {
    std::ostringstream out;

    out
        << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<resources>\n"
        << "    <string name=\"app_name\">"
        << xml_escape(options.name)
        << "</string>\n"
        << "</resources>\n";

    return out.str();
  }

  std::string render_colors_xml()
  {
    return R"XML(<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="vix_accent">#f37726</color>
</resources>
)XML";
  }

  std::string render_styles_xml()
  {
    return R"XML(<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="AppTheme" parent="android:style/Theme.Material.Light.NoActionBar">
        <item name="android:fontFamily">sans</item>
        <item name="android:windowLightStatusBar">true</item>
        <item name="android:colorAccent">@color/vix_accent</item>
    </style>
</resources>
)XML";
  }

  std::string render_gradle_properties()
  {
    return R"TXT(org.gradle.jvmargs=-Xmx2048m -Dfile.encoding=UTF-8
android.useAndroidX=false
)TXT";
  }

  std::string render_readme(const AndroidMobileOptions &options)
  {
    std::ostringstream out;

    out
        << "# " << options.name << "\n\n"
        << "Generated by `vix mobile init android`.\n\n"
        << "## App URL\n\n"
        << "```text\n"
        << options.url << "\n"
        << "```\n\n"
        << "## Build\n\n"
        << "```bash\n"
        << "gradle :app:assembleDebug\n"
        << "```\n\n"
        << "Later, `vix mobile build android` will wrap this command.\n";

    return out.str();
  }

  bool write_android_project(
      const AndroidMobileOptions &options,
      std::string &err)
  {
    const fs::path root = options.outputDirectory;
    const fs::path appRoot = root / "app";
    const fs::path mainRoot = appRoot / "src" / "main";
    const fs::path javaRoot = mainRoot / "java";
    const fs::path packageRoot =
        java_package_directory(javaRoot, options.packageName);

    std::vector<std::pair<fs::path, std::string>> files{
        {root / "settings.gradle", render_settings_gradle(options)},
        {root / "build.gradle", render_root_build_gradle(options)},
        {root / "gradle.properties", render_gradle_properties()},
        {root / "README.md", render_readme(options)},
        {appRoot / "build.gradle", render_app_build_gradle(options)},
        {mainRoot / "AndroidManifest.xml", render_manifest(options)},
        {packageRoot / "MainActivity.java", render_main_activity(options)},
        {mainRoot / "res" / "values" / "strings.xml", render_strings_xml(options)},
        {mainRoot / "res" / "values" / "colors.xml", render_colors_xml()},
        {mainRoot / "res" / "values" / "styles.xml", render_styles_xml()}};

    const std::string localProperties = render_local_properties();

    if (!localProperties.empty())
    {
      files.push_back({root / "local.properties", localProperties});
    }

    for (const auto &[path, content] : files)
    {
      if (!write_text_file(path, content, err))
      {
        return false;
      }
    }

    return true;
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

    const std::string task =
        options.release
            ? ":app:assembleRelease"
            : ":app:assembleDebug";

    out.event("build_start", "project", options.projectDirectory.string());

    out.banner("Vix Mobile \u00b7 build");
    out.row("project", options.projectDirectory.string());
    out.row("variant", options.release ? "release" : "debug");

    const int result =
        run_system_command(
            make_gradle_command(
                options.projectDirectory,
                task,
                options.gradleCommand));

    if (result != 0)
    {
      out.error("Android mobile build failed.");
      out.error_hint("Check the Gradle error above.");
      out.error_hint("If it says SDK location not found, create local.properties with sdk.dir.");
      out.error_hint("If it says mipmap/ic_launcher not found, regenerate the project after updating Vix.");
      return result;
    }

    const fs::path apkDir =
        options.release
            ? options.projectDirectory / "app" / "build" / "outputs" / "apk" / "release"
            : options.projectDirectory / "app" / "build" / "outputs" / "apk" / "debug";

    out.event("build_done", "apk", apkDir.string());
    out.success("Android mobile build completed.");
    out.row("apk", apkDir.string(), out.theme.cyan());

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

  int parse_android_init_options(
      const MobileReporter &out,
      const std::vector<std::string> &args,
      AndroidMobileOptions &options)
  {
    options.allowCleartext = starts_with(options.url, "http://");

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

        options.allowCleartext = starts_with(options.url, "http://");
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

      if (arg == "--allow-cleartext")
      {
        options.allowCleartext = true;
        continue;
      }

      if (arg == "--no-cleartext")
      {
        options.allowCleartext = false;
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
        options.allowCleartext = starts_with(options.url, "http://");
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

    if (!write_android_project(options, err))
    {
      out.error("Failed to generate Android mobile shell.");
      out.error_hint(err.empty() ? "Unknown file generation error." : err);
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
        out.error_hint("Usage: vix mobile init android --name \"My App\" --url https://example.com");
        return 1;
      }

      if (args[1] != "android")
      {
        out.error("Unsupported mobile init target: " + args[1]);
        out.error_hint("Supported target: android");
        return 1;
      }

      std::vector<std::string> rest(args.begin() + 2, args.end());
      return run_init_android(rest);
    }

    if (args[0] == "android")
    {
      std::vector<std::string> rest(args.begin() + 1, args.end());
      return run_init_android(rest);
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
        out.error("Unsupported mobile build target: ios");
        out.error_hint("Supported target: android");
        return 1;
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
        out.error("Unsupported mobile run target: ios");
        out.error_hint("Supported target: android");
        return 1;
      }
      else
      {
        rest.assign(args.begin() + 1, args.end());
      }

      return run_run_android(rest);
    }

    out.error("Unknown mobile command: " + args[0]);
    out.error_hint("Usage: vix mobile init android --name \"My App\" --url https://example.com");
    return 1;
  }

  int MobileCommand::help()
  {
    std::cout
        << "Usage:\n"
        << "  vix mobile init android [options]\n"
        << "  vix mobile android [options]\n"
        << "  vix mobile build android [options]\n"
        << "  vix mobile build [android] [options]\n"
        << "  vix mobile run [android] [options]\n\n"
        << "  vix mobile wrapper [android] [options]\n"
        << "  vix mobile devices\n"
        << "  vix mobile run android [options]\n\n"
        << "Description:\n"
        << "  Generate a mobile WebView shell for a Vix web or PWA application.\n"
        << "  The MVP generates an Android project that opens a target URL in WebView.\n"
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
        << "  --allow-cleartext          Allow http:// URLs in Android WebView\n"
        << "  --no-cleartext             Disable cleartext HTTP traffic\n"
        << "  --gradle <command>         Gradle command to use. Default: ./gradlew or gradle\n"
        << "  --force                    Allow writing into a non-empty output directory\n\n"

        << "Wrapper/build/run options:\n"
        << "  --project <dir>            Android project directory. Default: mobile/android\n"
        << "  --package <name>           Android package name used by run\n"
        << "  --debug                    Build/install debug variant (default)\n"
        << "  --release                  Build/install release variant\n"
        << "  --gradle <command>         Gradle command to use. Default: ./gradlew or gradle\n"
        << "  --gradle-version <version> Gradle wrapper version. Default: 8.14.4\n"
        << "  --distribution-type <type> Wrapper distribution type: bin or all. Default: bin\n"
        << "  --no-install               Run without installing first\n\n"

        << "Output options:\n"
        << "  --quiet, -q                Only print errors\n"
        << "  --json                     Emit machine-readable lifecycle events\n"
        << "  --no-color                 Disable ANSI colors (also honors NO_COLOR)\n\n"

        << "Examples:\n"
        << "  vix mobile init android --name \"My App\" --url https://example.com\n"
        << "  vix mobile init android --name \"Vix Note\" --url http://192.168.1.10:5179 --allow-cleartext\n"
        << "  vix mobile build android\n"
        << "  vix mobile run android\n"
        << "  vix mobile run android --project mobile/android --package com.softadastra.app\n"
        << "  vix mobile wrapper android\n"
        << "  vix mobile wrapper android --gradle-version 8.13\n"
        << "  vix mobile devices\n"
        << "  vix mobile init android --name \"Softadastra\" --package com.softadastra.app --url https://softadastra.com\n";

    return 0;
  }
}
