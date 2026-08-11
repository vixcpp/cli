#include <vix/cli/errors/RawLogDetectors.hpp>
#include <vix/cli/errors/ErrorContext.hpp>
#include <vix/cli/errors/ErrorPipeline.hpp>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
  std::string diagnose(
      const std::string &log,
      const std::filesystem::path &source = {})
  {
    std::ostringstream captured;
    auto *const previous = std::cerr.rdbuf(captured.rdbuf());

    const bool handled =
        vix::cli::errors::RawLogDetectors::handleKnownRunFailure(
            log,
            source);

    std::cerr.rdbuf(previous);

    if (!handled)
      throw std::runtime_error("runtime log was not handled");

    return captured.str();
  }

  std::string diagnose_compiler(
      const std::string &message,
      const std::filesystem::path &source)
  {
    vix::cli::errors::CompilerError error;
    error.file = source.string();
    error.line = 1;
    error.column = 1;
    error.message = message;

    vix::cli::errors::ErrorContext context;
    context.sourceFile = source;

    vix::cli::errors::ErrorPipeline pipeline;
    std::ostringstream captured;
    auto *const previous = std::cerr.rdbuf(captured.rdbuf());
    const bool handled = pipeline.tryHandle({error}, context);
    std::cerr.rdbuf(previous);

    if (!handled)
      throw std::runtime_error("compiler diagnostic was not handled");

    return captured.str();
  }

  void expect_contains(
      const std::string &text,
      const std::string &needle)
  {
    if (text.find(needle) == std::string::npos)
      throw std::runtime_error("missing diagnostic text: " + needle);
  }

  void expect_not_contains(
      const std::string &text,
      const std::string &needle)
  {
    if (text.find(needle) != std::string::npos)
      throw std::runtime_error("unexpected diagnostic text: " + needle);
  }
} // namespace

int main()
{
  try
  {
    const std::string addressInUse = diagnose(
        "13:16:41 [vix] [error] [http] listener init failed on port 8081: Address already in use\n"
        "terminate called after throwing an instance of 'std::runtime_error'\n"
        "  what(): Server startup failed on port 8081: Address already in use\n"
        "Aborted (core dumped)\n");
    expect_contains(addressInUse, "port is already in use");
    expect_contains(addressInUse, "port 8081");
    expect_not_contains(addressInUse, "port 16");

    expect_contains(diagnose("ERROR: AddressSanitizer: heap-buffer-overflow\n"),
                    "heap-buffer-overflow");
    expect_contains(diagnose("free(): double free detected in tcache 2\n"),
                    "double free");
    expect_contains(diagnose("free(): invalid pointer\n"),
                    "invalid free");
    expect_contains(diagnose("ERROR: AddressSanitizer: heap-use-after-free\n"),
                    "use-after-free");

    const std::string useAfterFreeCollision = diagnose(
        "ERROR: AddressSanitizer: heap-use-after-free\n"
        "SEGV on unknown address\n"
        "==123==ABORTING\n");
    expect_contains(useAfterFreeCollision, "use-after-free");
    expect_not_contains(useAfterFreeCollision, "segmentation fault");
    expect_not_contains(useAfterFreeCollision, "application stopped");

    const std::string uninitialized = diagnose(
        "WARNING: MemorySanitizer: use-of-uninitialized-value\n");
    expect_contains(uninitialized, "uninitialized memory read");
    expect_not_contains(uninitialized, "log:");

    setenv("VIX_LOG_LEVEL", "debug", 1);
    const std::string debugUninitialized = diagnose(
        "WARNING: MemorySanitizer: use-of-uninitialized-value\n");
    unsetenv("VIX_LOG_LEVEL");
    expect_contains(debugUninitialized, "log:");
    expect_contains(diagnose("Segmentation fault (core dumped)\n"),
                    "segmentation fault");
    expect_contains(diagnose("Aborted (core dumped)\n"),
                    "application stopped");
    expect_contains(diagnose(
                        "terminate called after throwing an instance of 'std::runtime_error'\n"
                        "  what(): service startup failed\n"),
                    "operation failed");
    expect_contains(diagnose(
                        "main.cpp:7:3: runtime error: signed integer overflow\n"),
                    "signed integer overflow");

    const auto source =
        std::filesystem::temp_directory_path() /
        "vix-runtime-diagnostics-location.cpp";
    {
      std::ofstream output(source);
      output << "int main() { return 0; }\n";
      output << "int value = 1;\n";
    }

    const std::string sanitizer = diagnose(
        "ERROR: AddressSanitizer: heap-buffer-overflow\n"
        "    #1 0x123 in main " +
            source.string() + ":2:7\n",
        {});
    expect_contains(sanitizer, source.string() + ":2");
    expect_not_contains(sanitizer, "at: source:");

    const std::string uniquePtr = diagnose_compiler(
        "use of deleted function 'std::unique_ptr<int>::unique_ptr(const std::unique_ptr<int>&)'",
        source);
    expect_contains(uniquePtr, "std::unique_ptr cannot be copied");

    const std::string substitution = diagnose_compiler(
        "template argument deduction/substitution failed",
        source);
    expect_contains(substitution, "template substitution failed");

    const std::string awaitReady = diagnose_compiler(
        "no member named 'await_ready' in 'Task'",
        source);
    expect_contains(awaitReady, "missing await_ready");

    std::error_code error;
    std::filesystem::remove(source, error);
  }
  catch (const std::exception &error)
  {
    std::cerr << "RuntimeDiagnosticsTests: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
