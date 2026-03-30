/**
 *
 *  @file TestGenerator.cpp
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
#include <vix/cli/commands/make/generators/TestGenerator.hpp>
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

    [[nodiscard]] std::string normalize_test_suite_name(const std::string &name)
    {
      std::string out;
      out.reserve(name.size());

      for (char c : name)
      {
        if (is_identifier_char(c))
          out.push_back(c);
      }

      if (out.empty())
        return "GeneratedTest";

      if (!is_identifier_start(out.front()))
        out.insert(out.begin(), 'T');

      return out;
    }

    [[nodiscard]] std::string build_test_content(const MakeContext &ctx,
                                                 const std::string &suite_name,
                                                 const std::string &test_name,
                                                 const fs::path &file_path)
    {
      std::ostringstream out;
      out << doxygen_file_header(file_path.filename().string());
      out << "#include <gtest/gtest.h>\n\n";
      out << "TEST(" << suite_name << ", " << test_name << ")\n";
      out << "{\n";
      out << "  EXPECT_TRUE(true);\n";
      out << "}\n";
      return out.str();
    }

    [[nodiscard]] std::string build_preview(const MakeContext &ctx,
                                            const fs::path &test_path)
    {
      std::ostringstream out;
      out << "kind: test\n";
      out << "name: " << ctx.name << "\n";
      out << "namespace: "
          << (ctx.name_space.empty() ? "(none)" : ctx.name_space) << "\n";
      out << "test: " << test_path.string() << "\n";
      return out.str();
    }
  } // namespace

  MakeResult generate_test(const MakeContext &ctx)
  {
    MakeResult result;

    const std::string suite_name = normalize_test_suite_name(ctx.name);
    const std::string test_name = "DefaultCase";
    const fs::path test_path =
        ctx.layout.tests_dir / ("test_" + snake_case(ctx.name) + ".cpp");

    result.files.push_back(
        MakeFile{
            test_path,
            build_test_content(ctx, suite_name, test_name, test_path)});

    result.preview = build_preview(ctx, test_path);
    result.notes.push_back("Generated GoogleTest skeleton.");
    result.notes.push_back("Test files are generated in tests/.");
    result.notes.push_back("You can now replace EXPECT_TRUE(true) with real assertions.");

    if (ctx.layout.in_module)
      result.notes.push_back("module context detected: " + ctx.layout.module_name);

    result.ok = true;
    return result;
  }

} // namespace vix::cli::make::generators
