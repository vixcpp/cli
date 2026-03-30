/**
 *
 *  @file FunctionGenerator.cpp
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
#include <vix/cli/commands/make/generators/FunctionGenerator.hpp>
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

    [[nodiscard]] std::string source_preamble(const std::string &filename)
    {
      return doxygen_file_header(filename);
    }

    [[nodiscard]] std::string build_header_content(const MakeContext &ctx,
                                                   const fs::path &relative_header_path)
    {
      std::ostringstream out;

      out << header_preamble(relative_header_path);
      out << "#include <string_view>\n\n";

      if (!ctx.name_space.empty())
        out << namespace_open(ctx.name_space);

      out << "[[nodiscard]] bool " << ctx.name << "(std::string_view value)";
      if (ctx.options.header_only)
        out << "\n{\n";
      else
        out << ";\n";

      if (ctx.options.header_only)
      {
        out << "  return !value.empty();\n";
        out << "}\n";
      }

      if (!ctx.name_space.empty())
        out << namespace_close(ctx.name_space);

      out << header_postamble();
      return out.str();
    }

    [[nodiscard]] std::string build_source_content(const MakeContext &ctx,
                                                   const fs::path &relative_header_path)
    {
      std::ostringstream out;
      const std::string qname = qualified_name(ctx.name_space, ctx.name);

      out << source_preamble(ctx.name + ".cpp");
      out << "#include <" << relative_header_path.generic_string() << ">\n\n";

      out << "bool " << qname << "(std::string_view value)\n";
      out << "{\n";
      out << "  return !value.empty();\n";
      out << "}\n";

      return out.str();
    }

    [[nodiscard]] std::string build_preview(const MakeContext &ctx,
                                            const fs::path &header_path,
                                            const fs::path &source_path)
    {
      std::ostringstream out;
      out << "kind: function\n";
      out << "name: " << ctx.name << "\n";
      out << "namespace: "
          << (ctx.name_space.empty() ? "(none)" : ctx.name_space) << "\n";
      out << "header: " << header_path.string() << "\n";

      if (ctx.options.header_only)
        out << "mode: header-only\n";
      else
        out << "source: " << source_path.string() << "\n";

      return out.str();
    }
  } // namespace

  MakeResult generate_function(const MakeContext &ctx)
  {
    MakeResult result;

    const fs::path header_path = ctx.layout.include_dir / (ctx.name + ".hpp");
    const fs::path source_path = ctx.layout.src_dir / (ctx.name + ".cpp");

    std::error_code ec{};
    const fs::path relative_header_path =
        fs::relative(header_path, ctx.layout.root, ec);

    const fs::path safe_relative_header_path =
        ec ? fs::path("include") / (ctx.name + ".hpp") : relative_header_path;

    result.files.push_back(
        MakeFile{
            header_path,
            build_header_content(ctx, safe_relative_header_path)});

    if (!ctx.options.header_only)
    {
      result.files.push_back(
          MakeFile{
              source_path,
              build_source_content(ctx, safe_relative_header_path)});
    }

    result.preview = build_preview(ctx, header_path, source_path);
    result.notes.push_back("Generated free function skeleton.");
    result.notes.push_back("Uses std::string_view for lightweight input.");

    if (ctx.options.header_only)
      result.notes.push_back("header-only mode enabled.");
    else
      result.notes.push_back("Uses a separate .cpp implementation file by default.");

    if (ctx.layout.in_module)
      result.notes.push_back("module: " + ctx.layout.module_name);

    result.ok = true;
    return result;
  }

} // namespace vix::cli::make::generators
