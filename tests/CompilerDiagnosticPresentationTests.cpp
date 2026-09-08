#include <vix/cli/errors/BuildFailureClassification.hpp>
#include <vix/cli/errors/CompilerDiagnosticPresentation.hpp>

#include <stdexcept>

int main()
{
  using namespace vix::cli::errors;

  const auto concise = compiler_diagnostic_presentation(8, false);
  if (concise.shown != 1 || concise.hidden != 7 || !concise.show_verbose_hint())
    throw std::runtime_error("default diagnostics must remain concise");

  const auto verbose = compiler_diagnostic_presentation(8, true);
  if (verbose.shown != 8 || verbose.hidden != 0 || verbose.show_verbose_hint())
    throw std::runtime_error("verbose diagnostics must show every parsed error");

  const char *compilerThenStaleLinker =
      "/tmp/project/main.cpp:12:9: error: invalid expression\n"
      "collect2: error: ld returned 1 exit status\n";
  if (!contains_source_compiler_diagnostic(compilerThenStaleLinker))
    throw std::runtime_error("source compiler diagnostics must win over stale linker output");

  if (contains_source_compiler_diagnostic("mold: error: undefined symbol: main\n"))
    throw std::runtime_error("a linker-only failure is not a compiler diagnostic");
}
