/**
 * @file VixcFrontend.cpp
 * @brief Experimental VixC source preparation adapter.
 */

#include <vix/cli/app/VixcFrontend.hpp>

#include <fstream>
#include <sstream>

#if defined(VIX_CLI_ENABLE_VIXC_FRONTEND)
#include <vixc/vixc.hpp>
#endif

namespace vix::cli::app
{
  VixcFrontendResult process_with_vixc(const std::filesystem::path &source)
  {
    VixcFrontendResult output;

#if !defined(VIX_CLI_ENABLE_VIXC_FRONTEND)
    output.diagnostics = "Vix was built without experimental VixC frontend support.";
    return output;
#else
    std::ifstream input(source, std::ios::binary);
    if (!input)
    {
      output.diagnostics = "Unable to read VixC source: " + source.string();
      return output;
    }

    const std::string text{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};

    vixc::FrontendOptions options;
    options.action = vixc::FrontendAction::Emit;
    options.retain_source_map = true;

    const vixc::FrontendResult result =
        vixc::Frontend{}.process(source.string(), text, options);

    std::ostringstream diagnostics;
    for (const vixc::Diagnostic &diagnostic : result.diagnostics())
    {
      diagnostics << "vixc ";
      if (diagnostic.has_code())
        diagnostics << '[' << diagnostic.code() << "] ";
      diagnostics << diagnostic.message();
      if (diagnostic.has_range())
      {
        diagnostics << " (offsets "
                    << diagnostic.range().begin_offset() << '-'
                    << diagnostic.range().end_offset() << ')';
      }
      diagnostics << '\n';
    }

    if (!result.success() || !result.has_generated_output())
    {
      output.diagnostics = diagnostics.str();
      if (output.diagnostics.empty())
        output.diagnostics = "VixC did not produce generated C++ output.";
      return output;
    }

    const std::filesystem::path directory =
        source.parent_path() / ".vix" / "vixc";
    const std::filesystem::path generated =
        directory / (source.filename().string() + ".vixc.cpp");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
      output.diagnostics = "Unable to create VixC generated-source directory: " + error.message();
      return output;
    }

    bool unchanged = false;
    {
      std::ifstream existing(generated, std::ios::binary);
      if (existing)
      {
        const std::string existing_text{
            std::istreambuf_iterator<char>{existing},
            std::istreambuf_iterator<char>{}};
        unchanged = existing_text == result.generated_output();
      }
    }

    if (!unchanged)
    {
      std::ofstream generated_output(generated, std::ios::binary | std::ios::trunc);
      if (!generated_output)
      {
        output.diagnostics = "Unable to write VixC generated source: " + generated.string();
        return output;
      }
      generated_output << result.generated_output();
    }

    output.success = true;
    output.generated_source = generated;
    output.diagnostics = diagnostics.str();
    return output;
#endif
  }
}
