/**
 *
 *  @file ClassGenerator.cpp
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
#include <vix/cli/commands/make/generators/ClassGenerator.hpp>
#include <vix/cli/commands/make/MakeUtils.hpp>

#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <utility>

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
      std::ostringstream out;
      out << doxygen_file_header(relative_header_path.filename().string());
      out << "#ifndef " << make_include_guard(relative_header_path) << "\n";
      out << "#define " << make_include_guard(relative_header_path) << "\n\n";
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

    [[nodiscard]] std::string trim_copy(std::string s)
    {
      return trim(std::move(s));
    }

    [[nodiscard]] std::string capitalize_first(std::string s)
    {
      if (!s.empty())
      {
        s.front() = static_cast<char>(
            std::toupper(static_cast<unsigned char>(s.front())));
      }

      return s;
    }

    [[nodiscard]] std::string member_name(const std::string &field_name)
    {
      return field_name + "_";
    }

    [[nodiscard]] std::string getter_name(const std::string &field_name)
    {
      return field_name;
    }

    [[nodiscard]] std::string setter_name(const std::string &field_name)
    {
      return "set_" + field_name;
    }

    [[nodiscard]] bool needs_string_include(const ClassSpec &spec)
    {
      for (const auto &field : spec.fields)
      {
        const std::string t = trim_copy(field.type);
        if (t == "std::string")
          return true;
      }

      return false;
    }

    [[nodiscard]] bool needs_utility_include(const ClassSpec &spec)
    {
      if (spec.with_value_ctor)
        return true;

      if (spec.with_getters_setters)
        return true;

      return false;
    }

    [[nodiscard]] std::set<std::string> collect_standard_includes(
        const ClassSpec &spec)
    {
      std::set<std::string> includes;

      if (needs_string_include(spec))
        includes.insert("<string>");

      if (needs_utility_include(spec))
        includes.insert("<utility>");

      return includes;
    }

    [[nodiscard]] std::string make_ctor_param(const ClassField &field)
    {
      return field.type + " " + field.name;
    }

    [[nodiscard]] std::string make_ctor_initializer(const ClassField &field)
    {
      return member_name(field.name) + "(std::move(" + field.name + "))";
    }

    [[nodiscard]] std::string build_value_ctor_signature(const ClassSpec &spec)
    {
      std::ostringstream out;
      out << spec.name << "(";

      for (std::size_t i = 0; i < spec.fields.size(); ++i)
      {
        if (i != 0)
          out << ", ";

        out << make_ctor_param(spec.fields[i]);
      }

      out << ")";
      return out.str();
    }

    [[nodiscard]] bool has_fields(const ClassSpec &spec) noexcept
    {
      return !spec.fields.empty();
    }

    [[nodiscard]] ClassSpec make_default_spec_from_context(const MakeContext &ctx)
    {
      ClassSpec spec;
      spec.name = ctx.name;
      spec.name_space = ctx.name_space;
      spec.header_only = ctx.options.header_only;

      spec.fields.push_back(ClassField{"id", "std::string"});

      spec.with_default_ctor = true;
      spec.with_value_ctor = true;
      spec.with_getters_setters = true;
      spec.with_copy_move = true;
      spec.with_virtual_destructor = true;

      return spec;
    }

    [[nodiscard]] MakeResult validate_spec(const MakeContext &ctx,
                                           const ClassSpec &spec)
    {
      MakeResult result;

      if (trim_copy(spec.name).empty())
      {
        result.ok = false;
        result.error = "Class name is required.";
        return result;
      }

      if (!is_valid_cpp_identifier(spec.name))
      {
        result.ok = false;
        result.error = "Invalid class name: " + spec.name;
        return result;
      }

      if (is_reserved_cpp_keyword(spec.name))
      {
        result.ok = false;
        result.error = "Reserved C++ keyword is not allowed: " + spec.name;
        return result;
      }

      if (!spec.name_space.empty() &&
          !is_valid_namespace_string(spec.name_space))
      {
        result.ok = false;
        result.error = "Invalid namespace: " + spec.name_space;
        return result;
      }

      for (const auto &field : spec.fields)
      {
        const std::string field_name = trim_copy(field.name);
        const std::string field_type = trim_copy(field.type);

        if (field_name.empty())
        {
          result.ok = false;
          result.error = "Field name cannot be empty.";
          return result;
        }

        if (!is_valid_cpp_identifier(field_name))
        {
          result.ok = false;
          result.error = "Invalid field name: " + field_name;
          return result;
        }

        if (is_reserved_cpp_keyword(field_name))
        {
          result.ok = false;
          result.error =
              "Reserved C++ keyword is not allowed as field name: " + field_name;
          return result;
        }

        if (field_type.empty())
        {
          result.ok = false;
          result.error = "Field type cannot be empty for: " + field_name;
          return result;
        }
      }

      if (spec.with_value_ctor && !has_fields(spec))
      {
        result.ok = false;
        result.error =
            "Value constructor requested but no fields were provided.";
        return result;
      }

      result.ok = true;
      (void)ctx;
      return result;
    }

    [[nodiscard]] std::string build_header_content(const MakeContext &ctx,
                                                   const ClassSpec &spec,
                                                   const fs::path &relative_header_path)
    {
      std::ostringstream out;

      out << header_preamble(relative_header_path);

      const auto includes = collect_standard_includes(spec);
      for (const auto &inc : includes)
        out << "#include " << inc << "\n";

      if (!includes.empty())
        out << "\n";

      if (!spec.name_space.empty())
        out << namespace_open(spec.name_space);

      out << "class " << spec.name << "\n";
      out << "{\n";
      out << "public:\n";

      if (spec.with_default_ctor)
        out << "  " << spec.name << "();\n";

      if (spec.with_value_ctor)
        out << "  " << build_value_ctor_signature(spec) << ";\n";

      if (spec.with_virtual_destructor)
        out << "  virtual ~" << spec.name << "() = default;\n";
      else
        out << "  ~" << spec.name << "() = default;\n";

      if (spec.with_copy_move)
      {
        out << "\n";
        out << "  " << spec.name << "(const " << spec.name << " &) = default;\n";
        out << "  " << spec.name << "(" << spec.name << " &&) noexcept = default;\n";
        out << "  " << spec.name << " &operator=(const " << spec.name
            << " &) = default;\n";
        out << "  " << spec.name << " &operator=(" << spec.name
            << " &&) noexcept = default;\n";
      }

      if (spec.with_getters_setters && has_fields(spec))
      {
        out << "\n";

        for (const auto &field : spec.fields)
        {
          out << "  [[nodiscard]] const " << field.type << " &"
              << getter_name(field.name) << "() const noexcept;\n";
          out << "  void " << setter_name(field.name) << "("
              << field.type << " value);\n\n";
        }
      }

      out << "private:\n";

      if (spec.fields.empty())
      {
        out << "  // TODO: add fields\n";
      }
      else
      {
        for (const auto &field : spec.fields)
          out << "  " << field.type << " " << member_name(field.name) << ";\n";
      }

      out << "};\n";

      if (!spec.name_space.empty())
        out << namespace_close(spec.name_space);

      out << header_postamble();
      (void)ctx;
      return out.str();
    }

    [[nodiscard]] std::string build_source_content(const MakeContext &ctx,
                                                   const ClassSpec &spec,
                                                   const fs::path &relative_header_path)
    {
      std::ostringstream out;
      const std::string qname = qualified_name(spec.name_space, spec.name);

      out << source_preamble(spec.name + ".cpp");
      out << "#include \"" << relative_header_path.filename().string() << "\"\n\n";

      if (spec.with_default_ctor)
      {
        out << qname << "::" << spec.name << "() = default;\n\n";
      }

      if (spec.with_value_ctor)
      {
        out << qname << "::" << build_value_ctor_signature(spec);

        if (has_fields(spec))
        {
          out << "\n    : ";

          for (std::size_t i = 0; i < spec.fields.size(); ++i)
          {
            if (i != 0)
              out << ", ";

            out << make_ctor_initializer(spec.fields[i]);
          }
        }

        out << "\n{\n";
        out << "}\n\n";
      }

      if (spec.with_getters_setters && has_fields(spec))
      {
        for (const auto &field : spec.fields)
        {
          out << "const " << field.type << " &" << qname << "::"
              << getter_name(field.name) << "() const noexcept\n";
          out << "{\n";
          out << "  return " << member_name(field.name) << ";\n";
          out << "}\n\n";

          out << "void " << qname << "::" << setter_name(field.name)
              << "(" << field.type << " value)\n";
          out << "{\n";
          out << "  " << member_name(field.name)
              << " = std::move(value);\n";
          out << "}\n\n";
        }
      }

      (void)ctx;
      return out.str();
    }

    [[nodiscard]] std::string build_preview(const ClassSpec &spec,
                                            const fs::path &header_path,
                                            const fs::path &source_path)
    {
      std::ostringstream out;
      out << "kind: class\n";
      out << "name: " << spec.name << "\n";
      out << "namespace: "
          << (spec.name_space.empty() ? "(none)" : spec.name_space) << "\n";
      out << "fields: " << spec.fields.size() << "\n";
      out << "header: " << header_path.string() << "\n";

      if (!spec.header_only)
        out << "source: " << source_path.string() << "\n";

      return out.str();
    }
  } // namespace

  MakeResult generate_class(const MakeContext &ctx)
  {
    return generate_class(ctx, make_default_spec_from_context(ctx));
  }

  MakeResult generate_class(const MakeContext &ctx,
                            const ClassSpec &spec)
  {
    const MakeResult validation = validate_spec(ctx, spec);
    if (!validation.ok)
      return validation;

    MakeResult result;

    const fs::path header_path = ctx.layout.include_dir / (spec.name + ".hpp");
    const fs::path source_path = ctx.layout.src_dir / (spec.name + ".cpp");

    std::error_code ec{};
    const fs::path relative_header_path =
        fs::relative(header_path, header_path.parent_path(), ec);

    const fs::path safe_relative_header_path =
        ec ? fs::path(spec.name + ".hpp") : relative_header_path;

    result.files.push_back(
        MakeFile{
            header_path,
            build_header_content(ctx, spec, safe_relative_header_path)});

    if (!spec.header_only)
    {
      result.files.push_back(
          MakeFile{
              source_path,
              build_source_content(ctx, spec, safe_relative_header_path)});
    }

    result.preview = build_preview(spec, header_path, source_path);
    result.notes.push_back("Generated class from interactive-ready spec.");

    if (spec.with_copy_move)
      result.notes.push_back("Includes default copy/move operations.");

    if (spec.with_getters_setters)
      result.notes.push_back("Includes generated getters and setters.");

    if (spec.header_only)
      result.notes.push_back("header-only mode enabled.");
    else
      result.notes.push_back("Uses a separate .cpp implementation file by default.");

    if (ctx.layout.in_module)
      result.notes.push_back("module: " + ctx.layout.module_name);

    result.ok = true;
    return result;
  }

} // namespace vix::cli::make::generators
