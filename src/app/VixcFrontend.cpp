/**
 * @file VixcFrontend.cpp
 * @brief Experimental VixC source preparation adapter.
 */

#include <vix/cli/app/VixcFrontend.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#if defined(VIX_CLI_ENABLE_VIXC_FRONTEND)
#include <vixc/vixc.hpp>
#endif

namespace vix::cli::app
{
  namespace
  {
    VixcFrontendResult write_vixc_output(
        const std::filesystem::path &generated,
        std::string_view text)
    {
      VixcFrontendResult output;

      std::error_code error;
      std::filesystem::create_directories(generated.parent_path(), error);
      if (error)
      {
        output.diagnostics =
            "Unable to create VixC generated-source directory: " + error.message();
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
          unchanged = existing_text == text;
        }
      }

      if (!unchanged)
      {
        std::ofstream generated_output(
            generated,
            std::ios::binary | std::ios::trunc);
        if (!generated_output)
        {
          output.diagnostics =
              "Unable to write VixC generated source: " + generated.string();
          return output;
        }
        generated_output << text;
      }

      output.success = true;
      output.generated_source = generated;
      return output;
    }

    VixcFrontendResult process_with_vixc_to_path(
        const std::filesystem::path &source,
        const std::filesystem::path &generated)
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

      output = write_vixc_output(generated, result.generated_output());
      if (!output.success)
      {
        output.diagnostics = diagnostics.str() + output.diagnostics;
        return output;
      }

      output.diagnostics = diagnostics.str();
      return output;
#endif
    }
  }

  VixcFrontendResult process_with_vixc(const std::filesystem::path &source)
  {
    const std::filesystem::path generated =
        source.parent_path() / ".vix" / "vixc" /
        (source.filename().string() + ".vixc.cpp");
    return process_with_vixc_to_path(source, generated);
  }

  VixcFrontendResult prepare_vixc_sources(
      AppManifest &manifest,
      const std::filesystem::path &project_directory)
  {
    VixcFrontendResult prepared;
    std::vector<std::string> compile_sources;
    std::vector<std::string> include_directories = manifest.includeDirs;

    for (const std::string &source : manifest.sources)
    {
      const std::filesystem::path relative =
          std::filesystem::path{source}.lexically_normal();
      if (relative.is_absolute() ||
          relative.empty() ||
          *relative.begin() == "..")
      {
        prepared.diagnostics =
            "VixC application sources must be relative to the project directory: " +
            source;
        return prepared;
      }

      const std::filesystem::path original =
          (project_directory / relative).lexically_normal();
      const std::filesystem::path generated =
          project_directory / ".vix" / "generated" / "vixc" /
          (relative.generic_string() + ".vixc.cpp");

      VixcFrontendResult result = process_with_vixc_to_path(original, generated);
      if (!result.success)
        return result;

      compile_sources.push_back(
          generated.lexically_relative(project_directory).generic_string());
      const std::filesystem::path parent = original.parent_path();
      const bool parentAlreadyIncluded = std::any_of(
          include_directories.begin(),
          include_directories.end(),
          [&](const std::string &include_directory)
          {
            return (project_directory / include_directory).lexically_normal() == parent;
          });
      if (!parentAlreadyIncluded)
        include_directories.push_back(parent.generic_string());
    }

    manifest.sources = std::move(compile_sources);
    manifest.includeDirs = std::move(include_directories);
    prepared.success = true;
    return prepared;
  }
}
