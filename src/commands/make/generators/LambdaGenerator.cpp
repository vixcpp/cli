/**
 *
 *  @file LambdaGenerator.cpp
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
#include <vix/cli/commands/make/generators/LambdaGenerator.hpp>
#include <vix/cli/commands/make/MakeUtils.hpp>

#include <sstream>
#include <string>

namespace vix::cli::make::generators
{
  namespace fs = std::filesystem;

  namespace
  {
    [[nodiscard]] std::string doxygen_file_header(const std::string &filename)
    {
      std::ostringstream out;
      out << "/**\n";
      out << " *\n";
      out << " *  @file " << filename << "\n";
      out << " *\n";
      out << " */\n";
      return out.str();
    }

    [[nodiscard]] std::string header_preamble(const fs::path &relative_header_path)
    {
      const std::string guard = make_include_guard(relative_header_path);

      std::ostringstream out;
      out << doxygen_file_header(relative_header_path.filename().string());
      out << "#ifndef " << guard << "\n";
      out << "#define " << guard << "\n\n";
      return out.str();
    }

    [[nodiscard]] std::string header_postamble()
    {
      return "\n#endif\n";
    }

    [[nodiscard]] std::string build_lambda_snippet(const MakeContext &ctx)
    {
      std::ostringstream out;

      out << "inline constexpr auto " << ctx.name
          << " = []<typename T>(const T &value)\n";
      out << "{\n";
      out << "  return value;\n";
      out << "};\n";

      return out.str();
    }

    [[nodiscard]] std::string build_header_content(const MakeContext &ctx,
                                                   const fs::path &relative_header_path)
    {
      std::ostringstream out;

      out << header_preamble(relative_header_path);

      if (!ctx.name_space.empty())
        out << namespace_open(ctx.name_space);

      out << build_lambda_snippet(ctx);

      if (!ctx.name_space.empty())
        out << namespace_close(ctx.name_space);

      out << header_postamble();
      return out.str();
    }

    [[nodiscard]] std::string build_preview(const MakeContext &ctx,
                                            const fs::path &header_path)
    {
      std::ostringstream out;
      out << "kind: lambda\n";
      out << "name: " << ctx.name << "\n";
      out << "namespace: "
          << (ctx.name_space.empty() ? "(none)" : ctx.name_space) << "\n";
      out << "header: " << header_path.string() << "\n";
      out << "mode: header-only\n";
      return out.str();
    }
  } // namespace

  MakeResult generate_lambda(const MakeContext &ctx)
  {
    MakeResult result;

    const fs::path header_path = ctx.layout.include_dir / (ctx.name + ".hpp");

    std::error_code ec{};
    const fs::path relative_header_path =
        fs::relative(header_path, ctx.layout.root, ec);

    const fs::path safe_relative_header_path =
        ec ? fs::path("include") / (ctx.name + ".hpp") : relative_header_path;

    result.files.push_back(
        MakeFile{
            header_path,
            build_header_content(ctx, safe_relative_header_path)});

    result.preview = build_lambda_snippet(ctx);
    result.notes.push_back("Generated modern generic lambda.");
    result.notes.push_back("Lambda generation is header-only by design.");
    result.notes.push_back("Also works well with --print for snippet-style usage.");

    if (ctx.layout.in_module)
      result.notes.push_back("module: " + ctx.layout.module_name);

    result.ok = true;
    return result;
  }

} // namespace vix::cli::make::generators
