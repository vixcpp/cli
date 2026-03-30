/**
 *
 *  @file EnumGenerator.cpp
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
#include <vix/cli/commands/make/generators/EnumGenerator.hpp>
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

    [[nodiscard]] std::string build_header_content(const MakeContext &ctx,
                                                   const fs::path &relative_header_path)
    {
      std::ostringstream out;

      out << header_preamble(relative_header_path);
      out << "#include <string_view>\n\n";

      if (!ctx.name_space.empty())
        out << namespace_open(ctx.name_space);

      out << "enum class " << ctx.name << "\n";
      out << "{\n";
      out << "  Unknown = 0,\n";
      out << "  Active,\n";
      out << "  Disabled,\n";
      out << "};\n\n";

      out << "[[nodiscard]] constexpr std::string_view to_string(" << ctx.name
          << " value) noexcept\n";
      out << "{\n";
      out << "  switch (value)\n";
      out << "  {\n";
      out << "    case " << ctx.name << "::Unknown:\n";
      out << "      return \"unknown\";\n";
      out << "    case " << ctx.name << "::Active:\n";
      out << "      return \"active\";\n";
      out << "    case " << ctx.name << "::Disabled:\n";
      out << "      return \"disabled\";\n";
      out << "  }\n\n";
      out << "  return \"unknown\";\n";
      out << "}\n\n";

      out << "[[nodiscard]] constexpr bool is_known(" << ctx.name
          << " value) noexcept\n";
      out << "{\n";
      out << "  return value == " << ctx.name << "::Active ||\n";
      out << "         value == " << ctx.name << "::Disabled;\n";
      out << "}\n";

      if (!ctx.name_space.empty())
        out << namespace_close(ctx.name_space);

      out << header_postamble();
      return out.str();
    }

    [[nodiscard]] std::string build_preview(const MakeContext &ctx,
                                            const fs::path &header_path)
    {
      std::ostringstream out;
      out << "kind: enum\n";
      out << "name: " << ctx.name << "\n";
      out << "namespace: "
          << (ctx.name_space.empty() ? "(none)" : ctx.name_space) << "\n";
      out << "header: " << header_path.string() << "\n";
      out << "mode: header-only\n";
      return out.str();
    }
  } // namespace

  MakeResult generate_enum(const MakeContext &ctx)
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

    result.preview = build_preview(ctx, header_path);
    result.notes.push_back("Generated enum class skeleton.");
    result.notes.push_back("Enum generation is header-only by design.");
    result.notes.push_back("Includes constexpr to_string(...) helper.");
    result.notes.push_back("Includes constexpr is_known(...) helper.");

    if (ctx.layout.in_module)
      result.notes.push_back("module: " + ctx.layout.module_name);

    result.ok = true;
    return result;
  }

} // namespace vix::cli::make::generators
