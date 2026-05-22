// Tests for CMakeBuildErrors classifier.
//
// Each test feeds a realistic log snippet into handleCMakeBuildError() and
// asserts (a) the handler returned true, (b) the expected diagnostic title
// appears on stderr, and (c) any extracted field appears.  A final group of
// tests confirms the generic fallback does NOT swallow specific cases that
// have a dedicated handler.

#include <vix/cli/errors/build/CMakeBuildErrors.hpp>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

  int g_pass = 0;
  int g_fail = 0;
  std::vector<std::string> g_failures;

  // Capture stderr while calling handleCMakeBuildError.
  struct ClassifyResult
  {
    bool handled;
    std::string stderr_output;
  };

  ClassifyResult classify(std::string_view log)
  {
    // Redirect std::cerr's rdbuf to a stringstream for the duration of the call.
    std::stringstream buf;
    auto *old = std::cerr.rdbuf(buf.rdbuf());
    bool handled = false;
    try
    {
      handled = vix::cli::errors::build::handleCMakeBuildError(log);
    }
    catch (...)
    {
      std::cerr.rdbuf(old);
      throw;
    }
    std::cerr.rdbuf(old);
    return {handled, buf.str()};
  }

  void check(const std::string &name,
             bool cond,
             const std::string &details = {})
  {
    if (cond)
    {
      ++g_pass;
    }
    else
    {
      ++g_fail;
      g_failures.push_back(name + (details.empty() ? "" : " — " + details));
    }
  }

  void expect_handled(const std::string &test_name,
                      std::string_view log,
                      std::initializer_list<std::string_view> expected_substrings,
                      std::initializer_list<std::string_view> forbidden_substrings = {})
  {
    auto r = classify(log);
    check(test_name + " :: handler returned true", r.handled,
          r.handled ? "" : "got false; stderr=" + r.stderr_output);

    for (auto needle : expected_substrings)
    {
      bool found = r.stderr_output.find(needle) != std::string::npos;
      check(test_name + " :: contains \"" + std::string(needle) + "\"",
            found,
            found ? "" : "stderr=" + r.stderr_output);
    }

    for (auto needle : forbidden_substrings)
    {
      bool found = r.stderr_output.find(needle) != std::string::npos;
      check(test_name + " :: does NOT contain \"" + std::string(needle) + "\"",
            !found,
            found ? "stderr=" + r.stderr_output : "");
    }
  }

  void expect_not_swallowed_by_fallback(const std::string &test_name,
                                        std::string_view log,
                                        std::string_view specific_title)
  {
    auto r = classify(log);
    check(test_name + " :: specific title present",
          r.stderr_output.find(specific_title) != std::string::npos,
          "stderr=" + r.stderr_output);
    check(test_name + " :: NOT the generic fallback",
          r.stderr_output.find("CMake configure failed") == std::string::npos ||
              // generic fallback uses exactly this title and a specific hint; the
              // specific_title check above is the real signal
              r.stderr_output.find("vix build --verbose to inspect") == std::string::npos,
          "specific handler should have caught this before fallback");
  }

} // namespace

// ============================================================================
// Tests for new handlers
// ============================================================================

void test_missing_build_tool()
{
  expect_handled(
      "missing_build_tool/ninja-command-not-found",
      "ninja: command not found\n",
      {"build tool not found", "ninja",
       "install the selected build tool"});

  expect_handled(
      "missing_build_tool/make-command-not-found",
      "/bin/sh: 1: make: command not found\n",
      {"build tool not found", "make"});

  expect_handled(
      "missing_build_tool/cmake_make_program-not-set",
      "CMake Error: CMAKE_MAKE_PROGRAM is not set.  You probably need to select a different build tool.\n",
      {"build tool not found"});

  expect_handled(
      "missing_build_tool/program-ninja-not-found",
      "CMake Error: CMake was unable to find a build program corresponding to \"Ninja\".  CMAKE_MAKE_PROGRAM is not set.  Program \"ninja\" not found.\n",
      {"build tool not found", "ninja"});
}

void test_compiler_test_failed()
{
  expect_handled(
      "compiler_test_failed/cxx-not-able",
      "-- The CXX compiler identification is GNU 11.4.0\n"
      "CMake Error at /usr/share/cmake/Modules/CMakeTestCXXCompiler.cmake:60 (message):\n"
      "  The C++ compiler\n"
      "    \"/usr/bin/c++\"\n"
      "  is not able to compile a simple test program.\n",
      {"compiler test failed",
       "check compiler installation"});

  expect_handled(
      "compiler_test_failed/c-not-able",
      "CMake Error: The C compiler is not able to compile a simple test program.\n",
      {"compiler test failed"});

  expect_handled(
      "compiler_test_failed/abi-info-failed",
      "-- Detecting CXX compiler ABI info - failed\n"
      "CMake Error in CMakeLists.txt: failed.\n",
      {"compiler test failed"});
}

void test_unsupported_cxx_standard()
{
  expect_handled(
      "unsupported_cxx_standard/dialect-CXX23",
      "CMake Error in CMakeLists.txt:\n"
      "  Target \"foo\" requires the language dialect \"CXX23\" but CMake does not know the compile flags to use to enable it.\n",
      {"unsupported C++ standard", "C++23",
       "upgrade the compiler or lower CMAKE_CXX_STANDARD"});

  expect_handled(
      "unsupported_cxx_standard/no-CXX_STANDARD-support",
      "CMake Error: The compiler does not support CXX_STANDARD 23.\n",
      {"unsupported C++ standard"});

  expect_handled(
      "unsupported_cxx_standard/invalid-c++23",
      "error: invalid value 'c++23' in '-std=c++23'\n",
      {"unsupported C++ standard", "C++23"});
}

void test_missing_header()
{
  expect_handled(
      "missing_header/gcc-no-such-file",
      "/home/foo/src/main.cpp:5:10: fatal error: nlohmann/json.hpp: No such file or directory\n"
      "    5 | #include <nlohmann/json.hpp>\n"
      "      |          ^~~~~~~~~~~~~~~~~~~\n"
      "compilation terminated.\n",
      {"header file not found", "nlohmann/json.hpp",
       "add the correct include directory"});

  expect_handled(
      "missing_header/msvc-C1083",
      "main.cpp(7): fatal error C1083: Cannot open include file: 'boost/asio.hpp': No such file or directory\n",
      {"header file not found", "boost/asio.hpp"});
}

void test_cpp_compile_error()
{
  expect_handled(
      "cpp_compile_error/expected-semicolon",
      "/home/foo/src/foo.cpp:42:5: error: expected ';' before 'int'\n"
      "   42 |     int x = 1\n"
      "      |     ^\n",
      {"C++ compilation failed",
       "foo.cpp",
       "42",
       "fix the C++ source error shown above"});

  expect_handled(
      "cpp_compile_error/no-matching-function",
      "/home/foo/src/main.cc:100:5: error: no matching function for call to 'foo(int)'\n",
      {"C++ compilation failed", "main.cc"});

  expect_handled(
      "cpp_compile_error/use-of-deleted",
      "/tmp/x.cpp:8:9: error: use of deleted function 'Foo::Foo(const Foo&)'\n",
      {"C++ compilation failed"});

  expect_handled(
      "cpp_compile_error/msvc-C2065",
      "main.cpp(15): error C2065: 'foo': undeclared identifier\n",
      {"C++ compilation failed"});
}

void test_missing_linked_library()
{
  expect_handled(
      "missing_linked_library/cannot-find-lssl",
      "/usr/bin/ld: cannot find -lssl: No such file or directory\n"
      "collect2: error: ld returned 1 exit status\n",
      {"linked library not found", "ssl",
       "install the library"});

  expect_handled(
      "missing_linked_library/macos-library-not-found",
      "ld: library not found for -lcrypto\n"
      "clang: error: linker command failed with exit code 1\n",
      {"linked library not found", "crypto"});
}

void test_multiple_definition()
{
  expect_handled(
      "multiple_definition/gnu-ld",
      "/usr/bin/ld: foo.o: in function `bar()':\n"
      "foo.cpp:(.text+0x0): multiple definition of `bar()'; main.o:main.cpp:(.text+0x0): first defined here\n"
      "collect2: error: ld returned 1 exit status\n",
      {"duplicate symbol",
       "move definitions from headers to source files"});

  expect_handled(
      "multiple_definition/clang-mach-o",
      "duplicate symbol '_main' in:\n"
      "    foo.o\n"
      "    bar.o\n"
      "ld: 1 duplicate symbol for architecture x86_64\n",
      {"duplicate symbol"});
}

void test_architecture_mismatch()
{
  expect_handled(
      "arch_mismatch/wrong-format",
      "/usr/bin/ld: skipping incompatible /opt/x86/libfoo.a when searching for -lfoo\n"
      "/usr/bin/ld: /opt/x86/libfoo.a: file in wrong format\n",
      {"binary architecture mismatch",
       "clean the build"});

  expect_handled(
      "arch_mismatch/wrong-ELF-class",
      "/usr/bin/ld: /usr/lib/x86_64-linux-gnu/libfoo.so: wrong ELF class: ELFCLASS64\n",
      {"binary architecture mismatch"});

  expect_handled(
      "arch_mismatch/incompatible",
      "ld: warning: ignoring file libfoo.dylib, building for macOS-arm64 but attempting to link with file built for macOS-x86_64. is incompatible with arm64\n",
      {"binary architecture mismatch"});
}

void test_missing_generated_file()
{
  expect_handled(
      "missing_generated/no-rule",
      "ninja: error: 'generated/foo.cpp', needed by 'CMakeFiles/foo.dir/main.cpp.o', missing and no known rule to make it\n",
      {"generated build file missing"});

  expect_handled(
      "missing_generated/no-rule-to-make-target",
      "make[2]: *** No rule to make target 'generated/proto/foo.pb.cc', needed by 'CMakeFiles/foo.dir/foo.cc.o'.  Stop.\n",
      {"generated build file missing", "generated/proto/foo.pb.cc"});
}

void test_install_path_error()
{
  expect_handled(
      "install_path/cannot-make-directory",
      "CMake Error at cmake_install.cmake:42 (file):\n"
      "  file INSTALL cannot make directory \"/usr/local/include/foo\": Permission denied.\n",
      {"install path is not writable",
       "/usr/local/include/foo",
       "writable CMAKE_INSTALL_PREFIX"});
}

void test_dependency_version_mismatch()
{
  expect_handled(
      "dep_version/package-version-unsuitable",
      "CMake Error at /usr/share/cmake/Modules/FindPackageHandleStandardArgs.cmake:230 (message):\n"
      "  Could not find a configuration file for package \"Boost\" that is compatible\n"
      "  with requested version \"1.85\".\n"
      "\n"
      "  The following configuration files were considered but not accepted:\n"
      "    /usr/lib/x86_64-linux-gnu/cmake/Boost-1.74.0/BoostConfig.cmake, version: 1.74.0 (64bit)\n"
      "    but it set PACKAGE_VERSION_UNSUITABLE to TRUE.\n",
      {"dependency version mismatch", "Boost", "1.85"});
}

void test_broken_imported_target()
{
  expect_handled(
      "broken_imported_target/missing-file",
      "CMake Error at CMakeLists.txt:10 (add_executable):\n"
      "  The imported target \"Foo::Foo\" references the file\n"
      "     \"/opt/foo/lib/libfoo.so\"\n"
      "  but this file does not exist.  Possible reasons include:\n"
      "  ...\n",
      {"broken imported CMake target",
       "Foo::Foo",
       "/opt/foo/lib/libfoo.so",
       "reinstall the package"});
}

void test_missing_submodule_source()
{
  expect_handled(
      "submodule/add-subdirectory",
      "CMake Error at CMakeLists.txt:25 (add_subdirectory):\n"
      "  add_subdirectory given source \"deps/spdlog\" which is not an existing\n"
      "  directory.\n",
      {"dependency source directory missing",
       "deps/spdlog",
       "initialize submodules"});
}

void test_invalid_preset()
{
  expect_handled(
      "preset/no-such-preset",
      "CMake Error: No such preset in CMakePresets.json: 'release-fast'\n",
      {"invalid CMake preset", "release-fast",
       "check CMakePresets.json"});

  expect_handled(
      "preset/could-not-read",
      "CMake Error: Could not read presets from /home/foo/proj: JSON parse error\n",
      {"invalid CMake preset"});
}

void test_corrupted_cache()
{
  expect_handled(
      "corrupted_cache/parse-error",
      "CMake Error: Error parsing CMakeCache.txt at line 17\n",
      {"corrupted CMake cache",
       "vix build --clean"});

  expect_handled(
      "corrupted_cache/could-not-load",
      "CMake Error: could not load cache from /home/foo/build/CMakeCache.txt\n",
      {"corrupted CMake cache"});
}

// ============================================================================
// Regression tests: existing handlers must still work
// ============================================================================

void test_existing_handlers_still_work()
{
  // Stale cache (mismatch, NOT corrupted)
  expect_handled(
      "existing/stale-cache",
      "CMake Error: The current CMakeCache.txt directory /home/foo/build is different than the directory /tmp/foo/build where CMakeCache.txt was created.\n"
      "This may result in binaries being created in the wrong place.\n",
      {"stale CMake build cache"});

  // Unknown Ninja target
  expect_handled(
      "existing/ninja-unknown-target",
      "ninja: error: unknown target 'foo_bar'\n",
      {"Build target not found"});

  // Compiler not found
  expect_handled(
      "existing/no-cxx-compiler",
      "CMake Error at CMakeLists.txt:1 (project):\n"
      "  No CMAKE_CXX_COMPILER could be found.\n",
      {"compiler not found"});

  // Package not found
  expect_handled(
      "existing/package-not-found",
      "CMake Error at /usr/share/cmake/Modules/FindPackageHandleStandardArgs.cmake:230:\n"
      "  Could NOT find OpenSSL (missing: OPENSSL_LIBRARIES)\n",
      {"required package not found", "OpenSSL"});

  // install(EXPORT ...) dependency
  expect_handled(
      "existing/install-export",
      "CMake Error: install(EXPORT \"MyTargets\") includes target \"my_lib\" which requires target \"some_dep\" that is not in any export set.\n",
      {"CMake export dependency missing", "MyTargets", "my_lib", "some_dep"});
}

// ============================================================================
// Order tests: make sure specific handlers run before generic fallback
// ============================================================================

void test_order_specific_before_fallback()
{
  // A "CMake Error" + "cannot find -lssl" log must be caught by the
  // missing-library handler, not the generic CMake fallback.
  {
    std::string_view log =
        "CMake Error: Build step for foo failed\n"
        "/usr/bin/ld: cannot find -lssl\n";
    auto r = classify(log);
    check("order/missing-library beats generic CMake fallback",
          r.stderr_output.find("linked library not found") != std::string::npos,
          "stderr=" + r.stderr_output);
  }

  // A header-missing log should be caught by missing-header, not
  // by the generic compile-error handler.
  {
    std::string_view log =
        "/foo.cpp:1:10: fatal error: bar.hpp: No such file or directory\n";
    auto r = classify(log);
    check("order/missing-header beats cpp-compile-error",
          r.stderr_output.find("header file not found") != std::string::npos,
          "stderr=" + r.stderr_output);
    check("order/missing-header does not say 'C++ compilation failed'",
          r.stderr_output.find("C++ compilation failed") == std::string::npos,
          "stderr=" + r.stderr_output);
  }

  // A duplicate-symbol log mixed with undefined-reference noise should be
  // caught by the multiple-definition handler.
  {
    std::string_view log =
        "/usr/bin/ld: foo.o:(.text+0x0): multiple definition of `bar()'; main.o:(.text+0x0): first defined here\n"
        "collect2: error: ld returned 1 exit status\n";
    auto r = classify(log);
    check("order/multiple-definition beats undefined-symbol",
          r.stderr_output.find("duplicate symbol") != std::string::npos,
          "stderr=" + r.stderr_output);
  }

  // Corrupted cache should win over stale cache.
  {
    std::string_view log =
        "CMake Error: Error parsing CMakeCache.txt at line 17\n";
    auto r = classify(log);
    check("order/corrupted-cache beats stale-cache",
          r.stderr_output.find("corrupted CMake cache") != std::string::npos &&
              r.stderr_output.find("stale CMake build cache") == std::string::npos,
          "stderr=" + r.stderr_output);
  }

  // Dependency version mismatch should win over package-not-found.
  {
    std::string_view log =
        "CMake Error at FindPackageHandleStandardArgs.cmake:230:\n"
        "  Could not find a configuration file for package \"Boost\" that is compatible with requested version \"1.85\".\n"
        "  but it set PACKAGE_VERSION_UNSUITABLE to TRUE.\n";
    auto r = classify(log);
    check("order/version-mismatch beats package-not-found",
          r.stderr_output.find("dependency version mismatch") != std::string::npos,
          "stderr=" + r.stderr_output);
  }
}

// ============================================================================
// False positive tests: handlers should NOT fire on unrelated logs
// ============================================================================

void test_no_false_positives()
{
  // A successful build log must not trigger any handler.
  {
    std::string_view log =
        "-- The CXX compiler identification is GNU 11.4.0\n"
        "-- Configuring done\n"
        "-- Generating done\n"
        "-- Build files have been written to: /home/foo/build\n"
        "[100%] Built target foo\n";
    auto r = classify(log);
    check("no-false-positive/successful-build",
          !r.handled,
          "should be unhandled; stderr=" + r.stderr_output);
  }

  // Mentioning "SSL" in a non-fetch context should not trigger fetch-content handler.
  {
    std::string_view log =
        "-- Found OpenSSL: /usr/lib/x86_64-linux-gnu/libssl.so (TLS)\n";
    auto r = classify(log);
    check("no-false-positive/SSL-mention-without-fetch",
          !r.handled,
          "should be unhandled; stderr=" + r.stderr_output);
  }
}

// ============================================================================
// main
// ============================================================================

int main()
{
  test_missing_build_tool();
  test_compiler_test_failed();
  test_unsupported_cxx_standard();
  test_missing_header();
  test_cpp_compile_error();
  test_missing_linked_library();
  test_multiple_definition();
  test_architecture_mismatch();
  test_missing_generated_file();
  test_install_path_error();
  test_dependency_version_mismatch();
  test_broken_imported_target();
  test_missing_submodule_source();
  test_invalid_preset();
  test_corrupted_cache();
  test_existing_handlers_still_work();
  test_order_specific_before_fallback();
  test_no_false_positives();

  std::cout << "\n=== CMakeBuildErrors classifier tests ===\n";
  std::cout << "Passed: " << g_pass << "\n";
  std::cout << "Failed: " << g_fail << "\n";

  if (g_fail > 0)
  {
    std::cout << "\nFailures:\n";
    for (const auto &f : g_failures)
      std::cout << "  - " << f << "\n";
    return 1;
  }

  return 0;
}
